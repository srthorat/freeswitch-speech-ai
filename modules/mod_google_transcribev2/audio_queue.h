/*
 * audio_queue.h - Thread-Safe Lock-Free Audio Queue
 *
 * High-performance SPSC (Single Producer, Single Consumer) queue for
 * passing audio data from the FreeSWITCH media bug callback to the
 * gRPC writer without blocking.
 *
 * Features:
 * - Lock-free enqueue/dequeue for the common case
 * - Bounded size with configurable behavior (drop old/drop new)
 * - Pre-connection buffering support
 * - Memory pooling to avoid allocations in hot path
 *
 * Thread Safety:
 * - One producer thread (FreeSWITCH audio callback)
 * - One consumer thread (gRPC writer)
 * - Safe for SPSC pattern
 */

#ifndef __AUDIO_QUEUE_H__
#define __AUDIO_QUEUE_H__

#include <cstdint>
#include <cstring>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>

namespace google_transcribe {

/**
 * AudioChunk - Fixed-size audio buffer
 */
struct AudioChunk {
    static const size_t MAX_CHUNK_SIZE = 8192;  // Max bytes per chunk (enough for ~250ms at 16kHz)

    uint8_t data[MAX_CHUNK_SIZE];
    size_t size = 0;

    AudioChunk() = default;

    AudioChunk(const void* src, size_t len) {
        size = std::min(len, MAX_CHUNK_SIZE);
        std::memcpy(data, src, size);
    }

    void set(const void* src, size_t len) {
        size = std::min(len, MAX_CHUNK_SIZE);
        std::memcpy(data, src, size);
    }

    void clear() {
        size = 0;
    }
};

/**
 * AudioQueue - Thread-safe audio buffer queue
 *
 * Uses a circular buffer with atomic head/tail pointers for lock-free
 * operation in the common case. Falls back to mutex when needed.
 */
class AudioQueue {
public:
    /**
     * Overflow policy when queue is full
     */
    enum class OverflowPolicy {
        DROP_NEWEST,  // Drop incoming audio (safest for real-time)
        DROP_OLDEST   // Drop old audio to make room (preserves recent audio)
    };

    /**
     * Constructor
     *
     * @param capacity Maximum number of chunks in queue
     * @param policy What to do when queue is full
     */
    explicit AudioQueue(size_t capacity = 256, OverflowPolicy policy = OverflowPolicy::DROP_OLDEST);

    ~AudioQueue() = default;

    /**
     * Enqueue audio data
     * Non-blocking, thread-safe for single producer.
     *
     * @param data Audio data
     * @param len Data length in bytes
     * @return true if enqueued, false if dropped
     */
    bool enqueue(const void* data, size_t len);

    /**
     * Dequeue audio data
     * Non-blocking, thread-safe for single consumer.
     *
     * @param chunk Output chunk
     * @return true if data available, false if queue empty
     */
    bool dequeue(AudioChunk& chunk);

    /**
     * Try to dequeue with wait
     *
     * @param chunk Output chunk
     * @param timeout_ms Maximum time to wait (0 = non-blocking)
     * @return true if data available
     */
    bool dequeueWait(AudioChunk& chunk, unsigned int timeout_ms = 0);

    /**
     * Get current queue size
     */
    size_t size() const;

    /**
     * Check if queue is empty
     */
    bool empty() const;

    /**
     * Check if queue is full
     */
    bool full() const;

    /**
     * Get total bytes currently queued
     */
    size_t bytesQueued() const;

    /**
     * Clear all data
     */
    void clear();

    /**
     * Wake up any waiting consumer
     */
    void notify();

    /**
     * Get stats
     */
    uint64_t getEnqueueCount() const { return m_enqueue_count.load(); }
    uint64_t getDequeueCount() const { return m_dequeue_count.load(); }
    uint64_t getDropCount() const { return m_drop_count.load(); }
    uint64_t getBytesEnqueued() const { return m_bytes_enqueued.load(); }

    // Delete copy/move
    AudioQueue(const AudioQueue&) = delete;
    AudioQueue& operator=(const AudioQueue&) = delete;

private:
    std::vector<AudioChunk> m_buffer;
    size_t m_capacity;
    OverflowPolicy m_policy;

    // Lock-free indices (for SPSC pattern)
    std::atomic<size_t> m_head{0};  // Next write position (producer)
    std::atomic<size_t> m_tail{0};  // Next read position (consumer)

    // For blocking wait
    std::mutex m_mutex;
    std::condition_variable m_cv;

    // Stats
    std::atomic<uint64_t> m_enqueue_count{0};
    std::atomic<uint64_t> m_dequeue_count{0};
    std::atomic<uint64_t> m_drop_count{0};
    std::atomic<uint64_t> m_bytes_enqueued{0};
    std::atomic<size_t> m_bytes_queued{0};
};

/**
 * PreConnectBuffer - Buffers audio before gRPC connection is established
 *
 * When a transcription session starts, there's a delay (100-500ms) before
 * the gRPC stream is ready. This buffer captures audio during that window
 * to prevent losing the start of speech.
 */
class PreConnectBuffer {
public:
    /**
     * Constructor
     *
     * @param max_bytes Maximum bytes to buffer (default: 32KB = ~1 second at 16kHz)
     */
    explicit PreConnectBuffer(size_t max_bytes = 32000);

    /**
     * Add audio to the buffer
     *
     * @param data Audio data
     * @param len Data length
     * @return true if added, false if buffer full (oldest data was dropped)
     */
    bool add(const void* data, size_t len);

    /**
     * Flush buffer contents to an AudioQueue
     *
     * @param queue Target queue
     * @return Number of chunks flushed
     */
    size_t flushTo(AudioQueue& queue);

    /**
     * Get current buffer size in bytes
     */
    size_t size() const { return m_total_bytes; }

    /**
     * Check if buffer is empty
     */
    bool empty() const { return m_chunks.empty(); }

    /**
     * Clear the buffer
     */
    void clear();

private:
    std::vector<AudioChunk> m_chunks;
    size_t m_max_bytes;
    size_t m_total_bytes = 0;
    mutable std::mutex m_mutex;
};

} // namespace google_transcribe

#endif // __AUDIO_QUEUE_H__
