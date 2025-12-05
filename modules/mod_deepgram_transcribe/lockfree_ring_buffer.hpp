/*
 * lockfree_ring_buffer.hpp - Lock-free SPSC Ring Buffer
 * 
 * HIGH SCALE ARCHITECTURE:
 * - Single Producer Single Consumer (SPSC) design
 * - Zero mutex contention
 * - Cache-line aligned to avoid false sharing
 * - Designed for 5K+ concurrent calls at 50fps each
 * 
 * Usage:
 *   Producer (frame callback): push() audio chunks
 *   Consumer (LWS callback): pop() to get audio for WebSocket write
 */

#ifndef __LOCKFREE_RING_BUFFER_HPP__
#define __LOCKFREE_RING_BUFFER_HPP__

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <utility>

namespace deepgram {

/**
 * Lock-free Single Producer Single Consumer Ring Buffer
 * 
 * @tparam Capacity Total buffer capacity in bytes (should be power of 2 for efficiency)
 * 
 * Thread Safety:
 *   - Exactly ONE thread may call push() (producer)
 *   - Exactly ONE thread may call pop() (consumer)
 *   - No external synchronization needed
 */
template<size_t Capacity>
class LockFreeRingBuffer {
public:
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    static constexpr size_t CACHE_LINE_SIZE = 64;
    
    LockFreeRingBuffer() : m_head(0), m_tail(0), m_cached_head(0), m_cached_tail(0) {
        // Zero-initialize buffer
        std::memset(m_buffer, 0, Capacity);
    }
    
    /**
     * Push data into the ring buffer (producer side)
     * 
     * @param data Source data pointer
     * @param len Number of bytes to push
     * @return Number of bytes actually pushed (may be less if buffer full)
     */
    size_t push(const void* data, size_t len) noexcept {
        if (len == 0 || data == nullptr) return 0;
        
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        
        // Calculate available space using cached tail first
        size_t available_space = Capacity - (current_head - m_cached_tail);
        
        // If space is insufficient, refresh the cached tail
        if (available_space < len) {
            m_cached_tail = m_tail.load(std::memory_order_acquire);
            available_space = Capacity - (current_head - m_cached_tail);
            if (available_space == 0) return 0; // Still full
        }
        
        // Calculate how much we can actually write
        const size_t to_write = std::min(len, available_space);
        
        if (to_write == 0) return 0;
        
        // Write data (may wrap around)
        const size_t head_idx = current_head & (Capacity - 1);
        const size_t first_chunk = std::min(to_write, Capacity - head_idx);
        
        std::memcpy(m_buffer + head_idx, data, first_chunk);
        
        if (to_write > first_chunk) {
            // Wrap around to beginning
            std::memcpy(m_buffer, static_cast<const uint8_t*>(data) + first_chunk, 
                       to_write - first_chunk);
        }
        
        // Update head with release semantics (ensures writes are visible)
        m_head.store(current_head + to_write, std::memory_order_release);
        
        return to_write;
    }
    
    /**
     * Pop data from the ring buffer (consumer side)
     * 
     * @param dest Destination buffer
     * @param max_len Maximum bytes to pop
     * @return Number of bytes actually popped
     */
    size_t pop(void* dest, size_t max_len) noexcept {
        if (max_len == 0 || dest == nullptr) return 0;
        
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        
        // Calculate available data using cached head first
        size_t available = m_cached_head - current_tail;
        
        // If data is insufficient, refresh the cached head
        if (available < max_len) {
            m_cached_head = m_head.load(std::memory_order_acquire);
            available = m_cached_head - current_tail;
            if (available == 0) return 0; // Still empty
        }
        
        const size_t to_read = std::min(max_len, available);
        
        if (to_read == 0) return 0;
        
        // Read data (may wrap around)
        const size_t tail_idx = current_tail & (Capacity - 1);
        const size_t first_chunk = std::min(to_read, Capacity - tail_idx);
        
        std::memcpy(dest, m_buffer + tail_idx, first_chunk);
        
        if (to_read > first_chunk) {
            // Wrap around to beginning
            std::memcpy(static_cast<uint8_t*>(dest) + first_chunk, m_buffer, 
                       to_read - first_chunk);
        }
        
        // Update tail with release semantics
        m_tail.store(current_tail + to_read, std::memory_order_release);
        
        return to_read;
    }
    
    /**
     * Peek at available data without consuming (consumer side only)
     * 
     * @param dest Destination buffer
     * @param max_len Maximum bytes to peek
     * @return Number of bytes peeked
     */
    size_t peek(void* dest, size_t max_len) const noexcept {
        if (max_len == 0 || dest == nullptr) return 0;
        
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        const size_t current_head = m_head.load(std::memory_order_acquire);
        
        const size_t available = current_head - current_tail;
        const size_t to_read = std::min(max_len, available);
        
        if (to_read == 0) return 0;
        
        const size_t tail_idx = current_tail & (Capacity - 1);
        const size_t first_chunk = std::min(to_read, Capacity - tail_idx);
        
        std::memcpy(dest, m_buffer + tail_idx, first_chunk);
        
        if (to_read > first_chunk) {
            std::memcpy(static_cast<uint8_t*>(dest) + first_chunk, m_buffer,
                       to_read - first_chunk);
        }
        
        return to_read;
    }
    
    /**
     * Get number of bytes available for reading (consumer perspective)
     */
    size_t size() const noexcept {
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        return head - tail;
    }
    
    /**
     * Get number of bytes available for writing (producer perspective)
     */
    size_t space_available() const noexcept {
        const size_t head = m_head.load(std::memory_order_acquire);
        const size_t tail = m_tail.load(std::memory_order_acquire);
        return Capacity - (head - tail);
    }
    
    /**
     * Check if buffer is empty
     */
    bool empty() const noexcept {
        return size() == 0;
    }
    
    /**
     * Check if buffer is full
     */
    bool full() const noexcept {
        return space_available() == 0;
    }
    
    /**
     * Reset buffer to empty state (NOT thread-safe - call only when no concurrent access)
     */
    void reset() noexcept {
        m_head.store(0, std::memory_order_release);
        m_tail.store(0, std::memory_order_release);
        m_cached_head = 0;
        m_cached_tail = 0;
    }
    
    /**
     * Get buffer capacity
     */
    static constexpr size_t capacity() noexcept {
        return Capacity;
    }
    
    /* ============================================================================
     * ZERO-COPY API (HIGH SCALE OPTIMIZATION)
     * 
     * These methods allow direct writes to the ring buffer without intermediate
     * copies. The pattern is:
     *   1. reserve_write() - get direct pointer(s) into buffer
     *   2. Write data directly to the returned pointer(s)
     *   3. commit_write() - make the data visible to consumer
     * 
     * This eliminates one memcpy in the hot path (50fps * 5K calls = 250K/sec).
     * ============================================================================ */
    
    /**
     * Reserve contiguous write region for zero-copy writes
     * 
     * @param requested_len Requested number of bytes
     * @param ptr1 OUT: First write pointer (always set if returns > 0)
     * @param len1 OUT: Length available at ptr1
     * @param ptr2 OUT: Second write pointer (for wrap-around, may be nullptr)
     * @param len2 OUT: Length available at ptr2 (0 if no wrap)
     * @return Total bytes reserved (len1 + len2), or 0 if no space
     * 
     * Usage:
     *   uint8_t *p1, *p2;
     *   size_t l1, l2;
     *   size_t reserved = buffer.reserve_write(320, &p1, &l1, &p2, &l2);
     *   if (reserved >= 320) {
     *       memcpy(p1, data, l1);       // or write directly
     *       if (l2 > 0) memcpy(p2, data + l1, l2);
     *       buffer.commit_write(320);
     *   }
     */
    size_t reserve_write(size_t requested_len, 
                         uint8_t** ptr1, size_t* len1,
                         uint8_t** ptr2, size_t* len2) noexcept {
        if (requested_len == 0) {
            *ptr1 = nullptr; *len1 = 0;
            *ptr2 = nullptr; *len2 = 0;
            return 0;
        }
        
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        
        // Calculate available space
        size_t available_space = Capacity - (current_head - m_cached_tail);
        if (available_space < requested_len) {
            m_cached_tail = m_tail.load(std::memory_order_acquire);
            available_space = Capacity - (current_head - m_cached_tail);
        }
        
        const size_t to_reserve = std::min(requested_len, available_space);
        if (to_reserve == 0) {
            *ptr1 = nullptr; *len1 = 0;
            *ptr2 = nullptr; *len2 = 0;
            return 0;
        }
        
        // Calculate write positions
        const size_t head_idx = current_head & (Capacity - 1);
        const size_t first_chunk = std::min(to_reserve, Capacity - head_idx);
        
        *ptr1 = m_buffer + head_idx;
        *len1 = first_chunk;
        
        if (to_reserve > first_chunk) {
            // Wrap around - need second pointer
            *ptr2 = m_buffer;
            *len2 = to_reserve - first_chunk;
        } else {
            *ptr2 = nullptr;
            *len2 = 0;
        }
        
        return to_reserve;
    }
    
    /**
     * Commit a previously reserved write
     * 
     * @param len Number of bytes to commit (must be <= reserved amount)
     * 
     * IMPORTANT: Call this AFTER writing data to the reserved region.
     * This makes the data visible to the consumer.
     */
    void commit_write(size_t len) noexcept {
        if (len == 0) return;
        
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        m_head.store(current_head + len, std::memory_order_release);
    }
    
    /**
     * Get direct write pointer for simple non-wrapping case
     * 
     * For small writes that fit in contiguous space, this is simpler than
     * the full reserve_write() API.
     * 
     * @param requested_len Requested bytes
     * @param ptr OUT: Write pointer
     * @return Contiguous bytes available (may be < requested if near wrap point)
     */
    size_t get_write_ptr(size_t requested_len, uint8_t** ptr) noexcept {
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        
        // Check available space
        size_t available_space = Capacity - (current_head - m_cached_tail);
        if (available_space < requested_len) {
            m_cached_tail = m_tail.load(std::memory_order_acquire);
            available_space = Capacity - (current_head - m_cached_tail);
        }
        
        if (available_space == 0) {
            *ptr = nullptr;
            return 0;
        }
        
        // Calculate contiguous space (without wrapping)
        const size_t head_idx = current_head & (Capacity - 1);
        const size_t contiguous = std::min(available_space, Capacity - head_idx);
        
        *ptr = m_buffer + head_idx;
        return std::min(requested_len, contiguous);
    }

    /* ============================================================================
     * Zero-Copy API - Higher Level
     * 
     * Simplified zero-copy interface using result structs.
     * ============================================================================ */

    /**
     * Result of reserve_contiguous() call
     */
    struct ReserveResult {
        uint8_t* ptr;       // Pointer to write location (nullptr if failed)
        size_t contiguous;  // Contiguous bytes available
        size_t total;       // Total bytes available (may include wrap-around)
        bool success;       // True if any space was reserved
    };
    
    /**
     * Reserve contiguous write space (simplified API)
     * 
     * @param requested Number of bytes requested
     * @return ReserveResult with pointer and available sizes
     */
    ReserveResult reserve_contiguous(size_t requested) noexcept {
        ReserveResult result = {nullptr, 0, 0, false};
        
        if (requested == 0) return result;
        
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        
        // Calculate available space
        size_t available_space = Capacity - (current_head - m_cached_tail);
        if (available_space < requested) {
            m_cached_tail = m_tail.load(std::memory_order_acquire);
            available_space = Capacity - (current_head - m_cached_tail);
        }
        
        if (available_space == 0) return result;
        
        // Calculate contiguous space
        const size_t head_idx = current_head & (Capacity - 1);
        const size_t contiguous = std::min(available_space, Capacity - head_idx);
        
        result.ptr = m_buffer + head_idx;
        result.contiguous = contiguous;
        result.total = available_space;
        
        // success=true if we have enough total space (caller checks contiguous separately)
        // This allows the caller to use the fallback path (pushAudio) when contiguous < requested
        result.success = (available_space >= requested);
        
        return result;
    }
    
    /**
     * Commit written data (after zero-copy write)
     */
    void commit(size_t len) noexcept {
        if (len == 0) return;
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        m_head.store(current_head + len, std::memory_order_release);
    }
    
    /**
     * Get maximum contiguous write space
     */
    size_t max_contiguous_write() const noexcept {
        const size_t current_head = m_head.load(std::memory_order_relaxed);
        const size_t current_tail = m_tail.load(std::memory_order_acquire);
        const size_t available = Capacity - (current_head - current_tail);
        const size_t head_idx = current_head & (Capacity - 1);
        return std::min(available, Capacity - head_idx);
    }
    
    /**
     * Struct for split-write (wrap-around case)
     */
    struct ReserveSplit {
        uint8_t* ptr1;      // First segment pointer
        size_t len1;        // First segment length
        uint8_t* ptr2;      // Second segment (wrap) pointer
        size_t len2;        // Second segment length
        bool success;
    };
    
    /**
     * Reserve space handling wrap-around explicitly
     */
    ReserveSplit reserve_split(size_t requested) noexcept {
        uint8_t *p1, *p2;
        size_t l1, l2;
        size_t reserved = reserve_write(requested, &p1, &l1, &p2, &l2);
        return {p1, l1, p2, l2, reserved > 0};
    }
    
    /* ============================================================================
     * Zero-Copy Read API
     * 
     * For consumer (LWS thread) to read directly without intermediate buffer.
     * ============================================================================ */
    
    /**
     * Peek at contiguous readable data without consuming
     * 
     * @return Pair of (pointer, size) for contiguous readable region
     */
    std::pair<const uint8_t*, size_t> peek_contiguous() noexcept {
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        
        // Always refresh cached head to get latest data from producer
        // This is critical for the consumer to see new data promptly
        m_cached_head = m_head.load(std::memory_order_acquire);
        size_t data_size = m_cached_head - current_tail;
        
        if (data_size == 0) {
            return {nullptr, 0};
        }
        
        // Calculate contiguous readable region
        const size_t tail_idx = current_tail & (Capacity - 1);
        const size_t contiguous = std::min(data_size, Capacity - tail_idx);
        
        return {m_buffer + tail_idx, contiguous};
    }
    
    /**
     * Consume data after reading (zero-copy read)
     * 
     * @param len Number of bytes to consume
     */
    void consume(size_t len) noexcept {
        if (len == 0) return;
        const size_t current_tail = m_tail.load(std::memory_order_relaxed);
        m_tail.store(current_tail + len, std::memory_order_release);
    }

private:
    // Cache-line aligned to prevent false sharing between producer and consumer
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_head;  // Written by producer
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> m_tail;  // Written by consumer
    
    // Local cache to reduce atomic loads
    alignas(CACHE_LINE_SIZE) size_t m_cached_head;  // Consumer's cached view of head
    alignas(CACHE_LINE_SIZE) size_t m_cached_tail;  // Producer's cached view of tail
    
    // Buffer storage
    alignas(CACHE_LINE_SIZE) uint8_t m_buffer[Capacity];
};

/**
 * Audio-specific ring buffer with LWS_PRE space handling
 * 
 * For WebSocket writes, libwebsockets requires LWS_PRE bytes before the payload.
 * This wrapper handles that automatically.
 */
template<size_t Capacity>
class AudioRingBuffer : public LockFreeRingBuffer<Capacity> {
public:
    using Base = LockFreeRingBuffer<Capacity>;
    
    AudioRingBuffer() : Base(), m_lws_pre(0) {}
    
    /**
     * Set LWS_PRE value (call once after knowing LWS_PRE)
     */
    void set_lws_pre(size_t lws_pre) {
        m_lws_pre = lws_pre;
    }
    
    /**
     * Pop audio data into a buffer with LWS_PRE space reserved
     * 
     * @param dest Buffer with at least LWS_PRE + max_len bytes
     * @param max_len Maximum audio bytes to pop
     * @return Number of audio bytes popped (written starting at dest + LWS_PRE)
     */
    size_t pop_for_websocket(void* dest, size_t max_len) noexcept {
        return Base::pop(static_cast<uint8_t*>(dest) + m_lws_pre, max_len);
    }
    
private:
    size_t m_lws_pre;
};

} // namespace deepgram

#endif // __LOCKFREE_RING_BUFFER_HPP__
