/*
 * memory_pool.hpp - High-Scale Object Pool for mod_audio_fork
 * 
 * HIGH SCALE ARCHITECTURE:
 * - Pre-allocates private_t objects to eliminate malloc/free in hot path
 * - Lock-free acquisition/release using atomic operations
 * - Designed for 5K+ concurrent calls
 * - Objects are recycled, not destroyed
 * 
 * Usage:
 *   // At module load:
 *   audiofork::PrivateDataPool::initialize(5000);
 *   
 *   // Per call:
 *   private_t* pvt = audiofork::PrivateDataPool::acquire();
 *   // ... use pvt ...
 *   audiofork::PrivateDataPool::release(pvt);
 *   
 *   // At module unload:
 *   audiofork::PrivateDataPool::shutdown();
 */

#ifndef __MEMORY_POOL_HPP__
#define __MEMORY_POOL_HPP__

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace audiofork {

/**
 * Lock-free free-list node for O(1) acquire/release
 */
struct PoolNode {
    PoolNode* next;
    void* object;
    bool in_use;
};

/**
 * High-performance object pool with lock-free fast path
 * 
 * Design:
 * - Uses a lock-free stack for the free list (fast path)
 * - Pre-allocates all memory at initialization
 * - Tracks statistics for monitoring
 */
template<typename T>
class ObjectPool {
public:
    // Pool statistics for monitoring
    struct Stats {
        std::atomic<uint64_t> acquires{0};         // Total acquire calls
        std::atomic<uint64_t> releases{0};         // Total release calls
        std::atomic<uint64_t> pool_hits{0};        // Acquired from pool
        std::atomic<uint64_t> pool_misses{0};      // Pool was empty
        std::atomic<uint64_t> current_in_use{0};   // Currently checked out
        std::atomic<uint64_t> high_water_mark{0};  // Peak concurrent usage
        size_t pool_capacity{0};                   // Total pool size
    };

    ObjectPool() : m_head(nullptr), m_nodes(nullptr), 
                   m_capacity(0), m_initialized(false) {}
    
    ~ObjectPool() {
        shutdown();
    }
    
    /**
     * Initialize the pool with pre-allocated objects
     * 
     * @param capacity Number of objects to pre-allocate
     * @return true if successful
     */
    bool initialize(size_t capacity) {
        if (m_initialized.exchange(true)) {
            return false;  // Already initialized
        }
        
        m_capacity = capacity;
        m_stats.pool_capacity = capacity;
        
        // Allocate contiguous memory for all nodes
        m_nodes = new PoolNode[capacity];
        if (!m_nodes) {
            m_initialized = false;
            return false;
        }
        
        // Build the free list - all nodes start as free
        // We pre-allocate the actual T objects for better performance
        for (size_t i = 0; i < capacity; i++) {
            m_nodes[i].object = new T();  // Pre-allocate
            m_nodes[i].in_use = false;
            m_nodes[i].next = (i + 1 < capacity) ? &m_nodes[i + 1] : nullptr;
        }
        
        m_head.store(&m_nodes[0], std::memory_order_release);
        
        return true;
    }
    
    /**
     * Shutdown the pool and free all memory
     */
    void shutdown() {
        if (!m_initialized.exchange(false)) {
            return;  // Already shutdown or never initialized
        }
        
        // Delete all allocated objects
        if (m_nodes) {
            for (size_t i = 0; i < m_capacity; i++) {
                if (m_nodes[i].object) {
                    delete static_cast<T*>(m_nodes[i].object);
                    m_nodes[i].object = nullptr;
                }
            }
            delete[] m_nodes;
            m_nodes = nullptr;
        }
        
        m_head.store(nullptr, std::memory_order_release);
        m_capacity = 0;
    }
    
    /**
     * Acquire an object from the pool (lock-free fast path)
     * 
     * @return Pointer to object, or nullptr if pool exhausted
     */
    T* acquire() {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return nullptr;
        }
        
        m_stats.acquires.fetch_add(1, std::memory_order_relaxed);
        
        // Lock-free pop from free list
        PoolNode* node = m_head.load(std::memory_order_acquire);
        while (node) {
            PoolNode* next = node->next;
            if (m_head.compare_exchange_weak(node, next,
                    std::memory_order_release,
                    std::memory_order_relaxed)) {
                // Successfully acquired
                node->in_use = true;
                node->next = nullptr;
                
                m_stats.pool_hits.fetch_add(1, std::memory_order_relaxed);
                uint64_t in_use = m_stats.current_in_use.fetch_add(1, std::memory_order_relaxed) + 1;
                
                // Update high water mark
                uint64_t hwm = m_stats.high_water_mark.load(std::memory_order_relaxed);
                while (in_use > hwm) {
                    if (m_stats.high_water_mark.compare_exchange_weak(hwm, in_use,
                            std::memory_order_relaxed)) {
                        break;
                    }
                }
                
                return static_cast<T*>(node->object);
            }
            // CAS failed, node was updated, retry
        }
        
        // Pool exhausted
        m_stats.pool_misses.fetch_add(1, std::memory_order_relaxed);
        return nullptr;
    }
    
    /**
     * Release an object back to the pool (lock-free)
     * 
     * @param obj The object to release
     */
    void release(T* obj) {
        if (!obj || !m_initialized.load(std::memory_order_acquire)) {
            return;
        }
        
        // Find the node for this object
        PoolNode* node = find_node(obj);
        if (!node) {
            // Not from pool - shouldn't happen, but handle gracefully
            return;
        }
        
        m_stats.releases.fetch_add(1, std::memory_order_relaxed);
        m_stats.current_in_use.fetch_sub(1, std::memory_order_relaxed);
        
        node->in_use = false;
        
        // Lock-free push to free list
        PoolNode* head = m_head.load(std::memory_order_relaxed);
        do {
            node->next = head;
        } while (!m_head.compare_exchange_weak(head, node,
                std::memory_order_release,
                std::memory_order_relaxed));
    }
    
    /**
     * Get pool statistics
     */
    const Stats& stats() const { return m_stats; }
    
    /**
     * Check if pool is initialized
     */
    bool is_initialized() const { 
        return m_initialized.load(std::memory_order_acquire); 
    }
    
    /**
     * Get pool capacity
     */
    size_t capacity() const { return m_capacity; }
    
    /**
     * Get number of available slots
     */
    size_t available() const {
        return m_capacity - m_stats.current_in_use.load(std::memory_order_relaxed);
    }

private:
    /**
     * Find the PoolNode for a given object
     * Linear search, but only called at release (not hot path)
     */
    PoolNode* find_node(T* obj) {
        if (!m_nodes) return nullptr;
        
        for (size_t i = 0; i < m_capacity; i++) {
            if (m_nodes[i].object == obj) {
                return &m_nodes[i];
            }
        }
        return nullptr;
    }
    
    std::atomic<PoolNode*> m_head;      // Head of free list
    PoolNode* m_nodes;                   // Array of pool nodes
    size_t m_capacity;                   // Total pool size
    std::atomic<bool> m_initialized;     // Initialization state
    Stats m_stats;                       // Pool statistics
};

} // namespace audiofork

#endif // __MEMORY_POOL_HPP__
