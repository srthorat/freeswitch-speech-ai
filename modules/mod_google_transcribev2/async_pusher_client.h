/*
 * async_pusher_client.h - Non-blocking Pusher HTTP Client
 *
 * This class provides asynchronous delivery of transcription events to Pusher.
 * It uses a dedicated thread pool and queue to ensure the main transcription
 * pipeline is never blocked by network I/O.
 *
 * Features:
 * - Thread-safe event queue with bounded size
 * - Configurable worker thread pool (default: 2 workers)
 * - Automatic retry with exponential backoff
 * - Connection pooling via CURL multi interface
 * - Circuit breaker pattern for failing endpoints
 *
 * Thread Safety:
 * - enqueue() is thread-safe, can be called from any thread
 * - Workers process events in order per call_id
 */

#ifndef __ASYNC_PUSHER_CLIENT_H__
#define __ASYNC_PUSHER_CLIENT_H__

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <functional>
#include <curl/curl.h>

namespace google_transcribe {

/**
 * Pusher event payload
 */
struct PusherEvent {
    std::string json_payload;
    std::string call_id;          // FreeSWITCH UUID
    std::string sip_call_id;      // SIP Call-ID header (for tracking)
    std::string channel_name;
    std::string event_name;
    bool is_final;

    // Pusher credentials (per-event to support multi-tenant)
    std::string app_id;
    std::string app_key;
    std::string app_secret;
    std::string cluster;
    
    // Call metadata (included in events)
    std::string caller_number;
    std::string caller_name;
    std::string callee_number;
    std::string callee_name;

    // Retry tracking
    int retry_count = 0;
    static const int MAX_RETRIES = 3;
};

/**
 * AsyncPusherClient - Non-blocking Pusher delivery
 *
 * Usage:
 *   AsyncPusherClient::getInstance().start();  // On module load
 *   AsyncPusherClient::getInstance().enqueue(event);  // Non-blocking
 *   AsyncPusherClient::getInstance().shutdown();  // On module unload
 */
class AsyncPusherClient {
public:
    /**
     * Get the singleton instance
     */
    static AsyncPusherClient& getInstance();

    /**
     * Start the worker thread pool
     *
     * @param num_workers Number of HTTP worker threads (default: 2)
     * @param max_queue_size Maximum queue size before dropping events (default: 10000)
     */
    void start(unsigned int num_workers = 2, size_t max_queue_size = 10000);

    /**
     * Shutdown and wait for pending events to be sent (with timeout)
     *
     * @param timeout_ms Maximum time to wait for pending events (default: 5000ms)
     */
    void shutdown(unsigned int timeout_ms = 5000);

    /**
     * Enqueue an event for async delivery
     * This is non-blocking and safe to call from any thread.
     *
     * @param event The event to send
     * @return true if enqueued, false if queue is full (event dropped)
     */
    bool enqueue(PusherEvent event);

    /**
     * Get current queue depth (for monitoring)
     */
    size_t getQueueDepth() const;

    /**
     * Get stats
     */
    uint64_t getEventsSent() const { return m_events_sent.load(); }
    uint64_t getEventsFailed() const { return m_events_failed.load(); }
    uint64_t getEventsDropped() const { return m_events_dropped.load(); }

    /**
     * Check if client is running
     */
    bool isRunning() const { return m_running.load(); }

    // Delete copy/move
    AsyncPusherClient(const AsyncPusherClient&) = delete;
    AsyncPusherClient& operator=(const AsyncPusherClient&) = delete;

private:
    AsyncPusherClient();
    ~AsyncPusherClient();

    /**
     * Worker thread function
     */
    void workerLoop(int worker_id);

    /**
     * Send a single event (blocking, called by worker)
     *
     * @param event The event to send
     * @return true if sent successfully
     */
    bool sendEvent(const PusherEvent& event);

    /**
     * Build Pusher request body
     */
    std::string buildRequestBody(const PusherEvent& event);

    /**
     * Build signed Pusher URL
     */
    std::string buildSignedUrl(const PusherEvent& event, const std::string& body);

    /**
     * Compute HMAC-SHA256 and return hex string
     */
    std::string hmacSha256Hex(const std::string& key, const std::string& data);

    /**
     * Compute MD5 and return hex string
     */
    std::string md5Hex(const std::string& data);

    /**
     * Escape JSON string for embedding
     */
    std::string escapeJson(const std::string& input);

    // Queue and synchronization
    std::queue<PusherEvent> m_queue;
    mutable std::mutex m_queue_mutex;
    std::condition_variable m_queue_cv;
    size_t m_max_queue_size = 10000;

    // Workers
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdown_requested{false};

    // CURL handles per worker (reused for connection pooling)
    std::vector<CURL*> m_curl_handles;

    // Stats
    std::atomic<uint64_t> m_events_sent{0};
    std::atomic<uint64_t> m_events_failed{0};
    std::atomic<uint64_t> m_events_dropped{0};
};

/**
 * Call metadata for Pusher events
 */
struct CallMetadata {
    std::string sip_call_id;
    std::string caller_number;
    std::string caller_name;
    std::string callee_number;
    std::string callee_name;
};

/**
 * Helper function to create a PusherEvent from transcription result
 */
PusherEvent createTranscriptionEvent(
    const std::string& json_result,
    const std::string& call_id,
    const CallMetadata& metadata,
    bool is_final,
    const std::string& app_id,
    const std::string& app_key,
    const std::string& app_secret,
    const std::string& cluster = "us2"
);

/**
 * Helper function to create a session start event
 */
PusherEvent createSessionStartEvent(
    const std::string& call_id,
    const CallMetadata& metadata,
    const std::string& app_id,
    const std::string& app_key,
    const std::string& app_secret,
    const std::string& cluster = "us2"
);

/**
 * Helper function to create a session stop event
 */
PusherEvent createSessionStopEvent(
    const std::string& call_id,
    const CallMetadata& metadata,
    const std::string& app_id,
    const std::string& app_key,
    const std::string& app_secret,
    const std::string& cluster = "us2"
);

} // namespace google_transcribe

#endif // __ASYNC_PUSHER_CLIENT_H__
