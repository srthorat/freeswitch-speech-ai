/*
 * lockfree_mpsc_queue.hpp - Lock-free Multiple Producer Single Consumer Queue
 * 
 * HIGH SCALE ARCHITECTURE:
 * - Multiple threads can push concurrently (producers)
 * - Single thread pulls items (consumer)
 * - Uses atomic CAS operations - no mutexes
 * - Intrusive design for zero allocation in hot path
 * 
 * Used for:
 * - pendingConnects queue (frame callbacks -> LWS thread)
 * - pendingDisconnects queue (frame callbacks -> LWS thread)
 * - pendingWrites queue (frame callbacks -> LWS thread)
 * 
 * At 5K calls with 50fps, this eliminates ~250K mutex operations per second!
 */

#ifndef __LOCKFREE_MPSC_QUEUE_HPP__
#define __LOCKFREE_MPSC_QUEUE_HPP__

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace deepgram {

/**
 * Intrusive node for MPSC queue
 * 
 * Items that will be queued must inherit from this or contain it as a member.
 * The 'next' pointer is managed by the queue.
 */
template<typename T>
struct MPSCNode {
    std::atomic<T*> next{nullptr};
};

/**
 * Lock-free Multiple Producer Single Consumer Queue
 * 
 * Based on Dmitry Vyukov's MPSC queue algorithm.
 * 
 * Thread Safety:
 * - push() can be called from multiple threads concurrently (producers)
 * - pop() must be called from a single thread only (consumer)
 * - The consumer thread should be the LWS service thread
 * 
 * @tparam T Type of elements (must have a 'std::atomic<T*> mpsc_next' member)
 */
template<typename T>
class LockFreeMPSCQueue {
public:
    LockFreeMPSCQueue() {
        // Initialize with a stub node to simplify the algorithm
        m_stub.mpsc_next.store(nullptr, std::memory_order_relaxed);
        m_head.store(&m_stub, std::memory_order_relaxed);
        m_tail = &m_stub;
    }
    
    /**
     * Push an item to the queue (lock-free, multiple producers)
     * 
     * @param item Item to push (must have mpsc_next member)
     * 
     * Thread-safe: Can be called from multiple threads concurrently
     */
    void push(T* item) noexcept {
        if (!item) return;
        
        // Clear the next pointer
        item->mpsc_next.store(nullptr, std::memory_order_relaxed);
        
        // Atomically exchange with head - we become the new head
        // The old head's next will point to us
        T* prev = m_head.exchange(item, std::memory_order_acq_rel);
        
        // Link the previous head to this item
        // This is the linearization point for the push
        prev->mpsc_next.store(item, std::memory_order_release);
    }
    
    /**
     * Pop an item from the queue (single consumer only)
     * 
     * @return Item pointer, or nullptr if queue is empty
     * 
     * NOT thread-safe: Must be called from single consumer thread only
     */
    T* pop() noexcept {
        T* tail = m_tail;
        T* next = tail->mpsc_next.load(std::memory_order_acquire);
        
        // Check if we're at the stub node
        if (tail == reinterpret_cast<T*>(&m_stub)) {
            if (!next) {
                // Queue is empty
                return nullptr;
            }
            // Skip the stub node
            m_tail = next;
            tail = next;
            next = next->mpsc_next.load(std::memory_order_acquire);
        }
        
        if (next) {
            // Normal case: advance tail
            m_tail = next;
            return tail;
        }
        
        // Check if this is the last item
        T* head = m_head.load(std::memory_order_acquire);
        if (tail != head) {
            // Producer is in the middle of push() - next pointer not set yet
            // We could spin here, but for our use case, returning null is fine
            // The item will be picked up on the next pop() call
            return nullptr;
        }
        
        // Re-add stub to allow more pushes
        push(reinterpret_cast<T*>(&m_stub));
        
        next = tail->mpsc_next.load(std::memory_order_acquire);
        if (next) {
            m_tail = next;
            return tail;
        }
        
        return nullptr;
    }
    
    /**
     * Check if queue appears empty
     * 
     * Note: This is approximate due to concurrent pushes
     */
    bool empty() const noexcept {
        T* tail = m_tail;
        T* next = tail->mpsc_next.load(std::memory_order_acquire);
        
        if (tail == reinterpret_cast<const T*>(&m_stub) && !next) {
            return true;
        }
        return false;
    }
    
    /**
     * Pop all available items into a vector
     * Useful for batch processing
     * 
     * @param out Vector to append items to
     * @param max_items Maximum items to pop (0 = unlimited)
     * @return Number of items popped
     */
    template<typename Container>
    size_t pop_all(Container& out, size_t max_items = 0) noexcept {
        size_t count = 0;
        T* item;
        
        while ((item = pop()) != nullptr) {
            out.push_back(item);
            ++count;
            
            if (max_items > 0 && count >= max_items) {
                break;
            }
        }
        
        return count;
    }

private:
    // Stub node to simplify algorithm (no null checks needed)
    // We use a struct that mimics T's mpsc_next member
    struct StubNode {
        std::atomic<T*> mpsc_next{nullptr};
    };
    
    // Cache-line padding to prevent false sharing
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    // Head pointer - producers push here (atomically exchanged)
    alignas(CACHE_LINE_SIZE) std::atomic<T*> m_head;
    
    // Tail pointer - consumer pops from here (single-threaded access)
    alignas(CACHE_LINE_SIZE) T* m_tail;
    
    // Stub node for algorithm simplification
    alignas(CACHE_LINE_SIZE) StubNode m_stub;
};

/**
 * Bounded Lock-free MPSC Queue (array-based)
 * 
 * For cases where we want predictable memory usage and can accept
 * bounded capacity. Uses a pre-allocated array instead of linked list.
 * 
 * @tparam T Pointer type to store
 * @tparam Capacity Maximum number of items (must be power of 2)
 */
template<typename T, size_t Capacity>
class BoundedMPSCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
public:
    BoundedMPSCQueue() : m_head(0), m_tail(0) {
        for (size_t i = 0; i < Capacity; i++) {
            m_buffer[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    
    /**
     * Push an item (lock-free, multiple producers)
     * 
     * @param item Item to push
     * @return true if pushed, false if queue is full
     */
    bool push(T* item) noexcept {
        if (!item) return false;
        
        size_t head = m_head.load(std::memory_order_relaxed);
        
        while (true) {
            size_t tail = m_tail.load(std::memory_order_acquire);
            
            // Check if full
            if (head - tail >= Capacity) {
                return false;  // Queue full
            }
            
            // Try to claim this slot
            if (m_head.compare_exchange_weak(head, head + 1,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed)) {
                // Slot claimed, store the item
                size_t idx = head & (Capacity - 1);
                m_buffer[idx].store(item, std::memory_order_release);
                return true;
            }
            // CAS failed, retry with updated head
        }
    }
    
    /**
     * Pop an item (single consumer only)
     * 
     * @return Item pointer, or nullptr if empty
     */
    T* pop() noexcept {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        size_t head = m_head.load(std::memory_order_acquire);
        
        if (tail >= head) {
            return nullptr;  // Empty
        }
        
        size_t idx = tail & (Capacity - 1);
        T* item = m_buffer[idx].load(std::memory_order_acquire);
        
        // Item might not be written yet if producer is slow
        if (!item) {
            return nullptr;
        }
        
        // Clear the slot and advance tail
        m_buffer[idx].store(nullptr, std::memory_order_relaxed);
        m_tail.store(tail + 1, std::memory_order_release);
        
        return item;
    }
    
    /**
     * Check if empty (approximate)
     */
    bool empty() const noexcept {
        return m_tail.load(std::memory_order_acquire) >= 
               m_head.load(std::memory_order_acquire);
    }
    
    /**
     * Get approximate size
     */
    size_t size() const noexcept {
        size_t head = m_head.load(std::memory_order_acquire);
        size_t tail = m_tail.load(std::memory_order_acquire);
        return (head > tail) ? (head - tail) : 0;
    }
    
    /**
     * Get capacity
     */
    static constexpr size_t capacity() noexcept {
        return Capacity;
    }

private:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_head;  // Push position
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_tail;  // Pop position
    alignas(CACHE_LINE_SIZE) std::atomic<T*> m_buffer[Capacity];
};

} // namespace deepgram

#endif // __LOCKFREE_MPSC_QUEUE_HPP__
