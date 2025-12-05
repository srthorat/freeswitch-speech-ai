/*
 * async_http.h - Non-blocking HTTP client using libcurl multi interface
 * 
 * Part of FreeSWITCH Speech AI high-scale architecture
 * Designed for 5K+ concurrent calls without blocking
 * 
 * Usage:
 *   1. Call async_http_init() at module load
 *   2. Call async_http_post() to queue HTTP requests (non-blocking)
 *   3. Call async_http_pump() periodically (from timer or event loop)
 *   4. Call async_http_shutdown() at module unload
 */

#ifndef __ASYNC_HTTP_H__
#define __ASYNC_HTTP_H__

#include <switch.h>
#include <curl/curl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Configuration constants */
#define ASYNC_HTTP_MAX_PARALLEL_REQUESTS 100
#define ASYNC_HTTP_MAX_QUEUE_SIZE 10000
#define ASYNC_HTTP_DEFAULT_TIMEOUT_MS 5000
#define ASYNC_HTTP_PUMP_INTERVAL_MS 5

/* Request priority levels */
typedef enum {
    ASYNC_HTTP_PRIORITY_LOW = 0,
    ASYNC_HTTP_PRIORITY_NORMAL = 1,
    ASYNC_HTTP_PRIORITY_HIGH = 2
} async_http_priority_t;

/* Callback for completed requests (optional) */
typedef void (*async_http_callback_t)(
    const char* url,
    long http_code,
    const char* response_body,
    size_t response_len,
    void* user_data
);

/* Statistics structure */
typedef struct {
    uint64_t requests_queued;
    uint64_t requests_completed;
    uint64_t requests_failed;
    uint64_t requests_timeout;
    uint64_t queue_high_watermark;
    uint64_t current_queue_size;
    uint64_t current_active;
    uint64_t total_bytes_sent;
    uint64_t total_bytes_received;
} async_http_stats_t;

/**
 * Initialize the async HTTP subsystem
 * Must be called once at module load
 * 
 * @return SWITCH_STATUS_SUCCESS on success
 */
switch_status_t async_http_init(void);

/**
 * Shutdown the async HTTP subsystem
 * Must be called at module unload
 * Waits for pending requests to complete (with timeout)
 */
void async_http_shutdown(void);

/**
 * Queue an HTTP POST request (non-blocking)
 * Returns immediately - request is processed asynchronously
 * 
 * @param url         Full URL to POST to
 * @param body        Request body (will be copied)
 * @param body_len    Length of body (0 = strlen)
 * @param headers     NULL-terminated array of headers, or NULL
 * @param timeout_ms  Request timeout in milliseconds (0 = default)
 * @param callback    Optional callback when complete (can be NULL)
 * @param user_data   User data passed to callback
 * @param priority    Request priority
 * 
 * @return SWITCH_STATUS_SUCCESS if queued, SWITCH_STATUS_FALSE if queue full
 */
switch_status_t async_http_post(
    const char* url,
    const char* body,
    size_t body_len,
    const char** headers,
    uint32_t timeout_ms,
    async_http_callback_t callback,
    void* user_data,
    async_http_priority_t priority
);

/**
 * Queue an HTTP POST with JSON content type (convenience function)
 */
switch_status_t async_http_post_json(
    const char* url,
    const char* json_body,
    async_http_callback_t callback,
    void* user_data
);

/**
 * Process pending HTTP requests
 * Must be called periodically (every 10-50ms recommended)
 * Can be called from timer callback or dedicated thread
 * 
 * This function is non-blocking and returns quickly
 * 
 * @return Number of requests completed in this pump cycle
 */
int async_http_pump(void);

/**
 * Get current statistics
 */
void async_http_get_stats(async_http_stats_t* stats);

/**
 * Reset statistics counters
 */
void async_http_reset_stats(void);

/**
 * Check if async HTTP is initialized and ready
 */
switch_bool_t async_http_is_ready(void);

/**
 * Get current queue depth
 */
size_t async_http_queue_depth(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASYNC_HTTP_H__ */
