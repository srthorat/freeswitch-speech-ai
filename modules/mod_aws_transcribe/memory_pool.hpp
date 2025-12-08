/*
 * memory_pool.hpp - High-Scale Object Pool for AudioPipe
 * 
 * HIGH SCALE ARCHITECTURE:
 * - Pre-allocates objects to eliminate malloc/free in hot path
 * - Lock-free acquisition/release using atomic operations
 * - Designed for 5K+ concurrent calls
 * - Objects are recycled, not destroyed
 */

#ifndef __MEMORY_POOL_HPP__
#define __MEMORY_POOL_HPP__

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace deepgram {

// Forward declarations
class AwsPipe;
class AwsInternalPipe;

/**
 * High-performance object pool with lock-free fast path
 * Design:
 * - Uses a lock-free stack (implemented as a linked list of nodes) for the free list.
 * - Pre-allocates all objects and nodes at initialization to eliminate runtime malloc.
 * - Returns std::shared_ptr with a custom deleter to automatically recycle objects.
 * - Extended to support PIMPL pattern objects for complete memory pool coverage.
 */
template<typename T>
class ObjectPool {
private:
    struct PoolNode {
        T object;
        std::atomic<PoolNode*> next;
    };

public:
    // Pool statistics for monitoring
    struct Stats {
        std::atomic<uint64_t> acquires{0};
        std::atomic<uint64_t> releases{0};
        std::atomic<uint64_t> pool_hits{0};
        std::atomic<uint64_t> pool_misses{0};
        std::atomic<uint64_t> current_in_use{0};
        std::atomic<uint64_t> high_water_mark{0};
        size_t pool_capacity{0};
    };

    ObjectPool() : m_head(nullptr), m_nodes(nullptr), m_capacity(0), m_initialized(false) {}
    
    ~ObjectPool() {
        shutdown();
    }
    
    bool initialize(size_t capacity) {
        if (m_initialized.exchange(true)) {
            return false; // Already initialized
        }
        
        m_capacity = capacity;
        m_stats.pool_capacity = capacity;
        
        // Allocate a contiguous block for all nodes
        m_nodes = new PoolNode[capacity];
        if (!m_nodes) {
            m_initialized = false;
            return false;
        }
        
        // Link nodes to form the initial free list
        for (size_t i = 0; i < capacity - 1; ++i) {
            m_nodes[i].next.store(&m_nodes[i+1], std::memory_order_relaxed);
        }
        m_nodes[capacity - 1].next.store(nullptr, std::memory_order_relaxed);
        
        m_head.store(&m_nodes[0], std::memory_order_release);
        
        return true;
    }
    
    void shutdown() {
        if (!m_initialized.exchange(false)) {
            return;
        }
        delete[] m_nodes;
        m_nodes = nullptr;
        m_head.store(nullptr, std::memory_order_relaxed);
    }

    std::shared_ptr<T> acquire() {
        if (!m_initialized.load(std::memory_order_acquire)) {
            return nullptr;
        }

        m_stats.acquires++;
        PoolNode* node = pop();

        if (!node) {
            m_stats.pool_misses++;
            return nullptr; // Pool is exhausted
        }

        m_stats.pool_hits++;
        uint64_t in_use = m_stats.current_in_use.fetch_add(1) + 1;
        if (in_use > m_stats.high_water_mark.load()) {
            m_stats.high_water_mark.store(in_use);
        }
        
        // Return a shared_ptr with a custom deleter that pushes the node back to the pool
        return std::shared_ptr<T>(&node->object, [this, node](T* p){
            push(node);
        });
    }

    const Stats& get_stats() const {
        return m_stats;
    }

    size_t capacity() const {
        return m_capacity;
    }

private:
    void push(PoolNode* node) {
        m_stats.releases++;
        m_stats.current_in_use--;
        
        PoolNode* old_head = m_head.load(std::memory_order_relaxed);
        do {
            node->next.store(old_head, std::memory_order_relaxed);
        } while (!m_head.compare_exchange_weak(old_head, node, std::memory_order_release, std::memory_order_relaxed));
    }

    PoolNode* pop() {
        PoolNode* old_head = m_head.load(std::memory_order_acquire);
        while (old_head) {
            PoolNode* next = old_head->next.load(std::memory_order_relaxed);
            if (m_head.compare_exchange_weak(old_head, next, std::memory_order_release, std::memory_order_relaxed)) {
                return old_head;
            }
        }
        return nullptr;
    }

    std::atomic<bool> m_initialized;
    size_t m_capacity;
    Stats m_stats;
    PoolNode* m_nodes;
    std::atomic<PoolNode*> m_head;
};

}; // namespace deepgram

// Global object pools for high-scale performance
// extern deepgram::ObjectPool<AwsPipe> g_pipe_pool; // TODO: Fix namespace
// extern deepgram::ObjectPool<AwsInternalPipe> g_internal_pipe_pool; // TODO: Fix namespace

#endif // __MEMORY_POOL_HPP__
