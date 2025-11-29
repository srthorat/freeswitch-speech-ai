/*
 * audio_queue.cpp - Implementation of AudioQueue and PreConnectBuffer
 */

#include "audio_queue.h"
#include <switch.h>
#include <chrono>

namespace google_transcribe {

// AudioQueue implementation

AudioQueue::AudioQueue(size_t capacity, OverflowPolicy policy)
    : m_capacity(capacity)
    , m_policy(policy) {
    m_buffer.resize(capacity);
}

bool AudioQueue::enqueue(const void* data, size_t len) {
    if (len == 0 || len > AudioChunk::MAX_CHUNK_SIZE) {
        return false;
    }

    size_t head = m_head.load(std::memory_order_relaxed);
    size_t tail = m_tail.load(std::memory_order_acquire);

    size_t next_head = (head + 1) % m_capacity;

    // Check if full
    if (next_head == tail) {
        if (m_policy == OverflowPolicy::DROP_NEWEST) {
            m_drop_count++;
            return false;
        } else {
            // DROP_OLDEST: advance tail to make room
            m_tail.store((tail + 1) % m_capacity, std::memory_order_release);
            m_drop_count++;
        }
    }

    // Write to buffer
    m_buffer[head].set(data, len);

    // Commit write
    m_head.store(next_head, std::memory_order_release);

    m_enqueue_count++;
    m_bytes_enqueued += len;
    m_bytes_queued += len;

    // Notify waiting consumer
    m_cv.notify_one();

    return true;
}

bool AudioQueue::dequeue(AudioChunk& chunk) {
    size_t tail = m_tail.load(std::memory_order_relaxed);
    size_t head = m_head.load(std::memory_order_acquire);

    // Check if empty
    if (tail == head) {
        return false;
    }

    // Read from buffer
    chunk = m_buffer[tail];

    // Commit read
    m_tail.store((tail + 1) % m_capacity, std::memory_order_release);

    m_dequeue_count++;
    m_bytes_queued -= chunk.size;

    return true;
}

bool AudioQueue::dequeueWait(AudioChunk& chunk, unsigned int timeout_ms) {
    // First try non-blocking
    if (dequeue(chunk)) {
        return true;
    }

    if (timeout_ms == 0) {
        return false;
    }

    // Wait for data
    std::unique_lock<std::mutex> lock(m_mutex);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(timeout_ms);

    while (!m_cv.wait_until(lock, deadline, [this] {
        return m_head.load(std::memory_order_acquire) !=
               m_tail.load(std::memory_order_acquire);
    })) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
    }

    lock.unlock();
    return dequeue(chunk);
}

size_t AudioQueue::size() const {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_acquire);

    if (head >= tail) {
        return head - tail;
    } else {
        return m_capacity - tail + head;
    }
}

bool AudioQueue::empty() const {
    return m_head.load(std::memory_order_acquire) ==
           m_tail.load(std::memory_order_acquire);
}

bool AudioQueue::full() const {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_acquire);
    return ((head + 1) % m_capacity) == tail;
}

size_t AudioQueue::bytesQueued() const {
    return m_bytes_queued.load();
}

void AudioQueue::clear() {
    m_head.store(0, std::memory_order_release);
    m_tail.store(0, std::memory_order_release);
    m_bytes_queued.store(0);
}

void AudioQueue::notify() {
    m_cv.notify_all();
}

// PreConnectBuffer implementation

PreConnectBuffer::PreConnectBuffer(size_t max_bytes)
    : m_max_bytes(max_bytes) {
    // Pre-allocate for typical buffer size
    m_chunks.reserve(max_bytes / 320 + 1);  // ~20ms chunks
}

bool PreConnectBuffer::add(const void* data, size_t len) {
    if (len == 0 || len > AudioChunk::MAX_CHUNK_SIZE) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if we need to drop old chunks
    while (m_total_bytes + len > m_max_bytes && !m_chunks.empty()) {
        m_total_bytes -= m_chunks.front().size;
        m_chunks.erase(m_chunks.begin());
    }

    // Add new chunk
    m_chunks.emplace_back(data, len);
    m_total_bytes += len;

    return true;
}

size_t PreConnectBuffer::flushTo(AudioQueue& queue) {
    std::lock_guard<std::mutex> lock(m_mutex);

    size_t count = 0;
    for (const auto& chunk : m_chunks) {
        if (queue.enqueue(chunk.data, chunk.size)) {
            count++;
        }
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "PreConnectBuffer: Flushed %zu chunks (%zu bytes) to queue\n",
        count, m_total_bytes);

    m_chunks.clear();
    m_total_bytes = 0;

    return count;
}

void PreConnectBuffer::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_chunks.clear();
    m_total_bytes = 0;
}

} // namespace google_transcribe
