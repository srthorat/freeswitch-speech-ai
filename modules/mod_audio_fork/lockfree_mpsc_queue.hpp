/*
 * lockfree_mpsc_queue.hpp - Lock-free Multi-Producer Single-Consumer Queue
 * 
 * HIGH SCALE ARCHITECTURE:
 * - Multiple producers (FreeSWITCH threads) can push concurrently
 * - Single consumer (LWS service thread) pops
 * - Lock-free using atomic operations
 * - Bounded capacity with overflow detection
 * 
 * Usage:
 *   BoundedMPSCQueue<AudioPipe, 16384> queue;
 *   queue.push(audio_pipe_ptr);  // Producer threads
 *   AudioPipe* ap = queue.pop(); // Consumer thread
 */

#ifndef __LOCKFREE_MPSC_QUEUE_HPP__
#define __LOCKFREE_MPSC_QUEUE_HPP__

#include <atomic>
#include <cstddef>
#include <array>

template<typename T, size_t Capacity>
class BoundedMPSCQueue {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    
    BoundedMPSCQueue() : m_head(0), m_tail(0) {
        // Initialize all slots to nullptr
        for (size_t i = 0; i < Capacity; i++) {
            m_buffer[i].store(nullptr, std::memory_order_relaxed);
        }
    }
    
    /**
     * Push item to queue (multi-producer safe)
     * 
     * @param item Pointer to item to push
     * @return true if successful, false if queue full
     */
    bool push(T* item) noexcept {
        if (!item) return false;
        
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        const size_t next_head = (current_head + 1) & (Capacity - 1);
        
        // Check if queue is full
        if (next_head == m_tail.load(std::memory_order_acquire)) {
            return false; // Queue full
        }
        
        // Try to claim the slot
        T* expected = nullptr;
        if (m_buffer[current_head].compare_exchange_weak(expected, item, 
                std::memory_order_release, std::memory_order_relaxed)) {
            
            // Successfully stored item, advance head
            size_t expected_head = current_head;
            while (!m_head.compare_exchange_weak(expected_head, next_head,
                    std::memory_order_release, std::memory_order_relaxed)) {
                // CAS failed, retry with updated expected value
                // This handles race condition where multiple producers
                // are trying to advance head simultaneously
            }
            return true;
        }
        
        return false; // Slot was taken by another producer
    }
    
    /**
     * Pop item from queue (single-consumer only)
     * 
     * @return Pointer to item, or nullptr if queue empty
     */
    T* pop() noexcept {
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        
        // Check if queue is empty
        if (current_tail == m_head.load(std::memory_order_acquire)) {
            return nullptr;
        }
        
        // Load item from current tail position
        T* item = m_buffer[current_tail].load(std::memory_order_acquire);
        if (!item) {
            return nullptr; // Slot not ready yet
        }
        
        // Clear the slot
        m_buffer[current_tail].store(nullptr, std::memory_order_relaxed);
        
        // Advance tail
        m_tail.store((current_tail + 1) & (Capacity - 1), std::memory_order_release);
        
        return item;
    }
    
    /**
     * Check if queue is empty (approximate)
     */
    bool empty() const noexcept {
        return m_tail.load(std::memory_order_acquire) == 
               m_head.load(std::memory_order_acquire);
    }
    
    /**
     * Get approximate size
     */
    size_t size() const noexcept {
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        return (head - tail) & (Capacity - 1);
    }
    
    /**
     * Get capacity
     */
    static constexpr size_t capacity() { return Capacity; }

private:
    // Cache-line align atomics to avoid false sharing
    alignas(64) std::atomic<size_t> m_head;
    alignas(64) std::atomic<size_t> m_tail;
    
    // Array of atomic pointers
    std::array<std::atomic<T*>, Capacity> m_buffer;
};

#endif // __LOCKFREE_MPSC_QUEUE_HPP__