/*
 * async_http.c - Non-blocking HTTP client implementation
 * 
 * Part of FreeSWITCH Speech AI high-scale architecture
 * Uses libcurl multi interface for non-blocking HTTP
 * 
 * Key design decisions:
 * - Single CURLM handle for all requests (connection pooling)
 * - Bounded queue to prevent memory explosion
 * - Lock-free where possible using atomics
 * - Minimal allocations in hot path
 */

#include "async_http.h"
#include <string.h>
#include <stdlib.h>

/* Request node in the queue */
typedef struct async_http_request {
    char* url;
    char* body;
    size_t body_len;
    struct curl_slist* headers;
    uint32_t timeout_ms;
    async_http_callback_t callback;
    void* user_data;
    async_http_priority_t priority;
    
    /* Response storage */
    char* response;
    size_t response_len;
    size_t response_alloc;
    
    /* CURL handle (when active) */
    CURL* easy;
    
    /* Queue linkage */
    struct async_http_request* next;
} async_http_request_t;

/* Module state */
static struct {
    CURLM* multi;
    switch_mutex_t* queue_mutex;
    switch_memory_pool_t* pool;
    
    /* Request queues (by priority) */
    async_http_request_t* queue_head[3];
    async_http_request_t* queue_tail[3];
    
    /* Background pump thread */
    switch_thread_t* pump_thread;
    switch_bool_t pump_thread_running;
    
    /* Statistics (using atomics for lock-free reads) */
    volatile uint64_t requests_queued;
    volatile uint64_t requests_completed;
    volatile uint64_t requests_failed;
    volatile uint64_t requests_timeout;
    volatile uint64_t queue_size;
    volatile uint64_t active_count;
    volatile uint64_t queue_high_watermark;
    volatile uint64_t total_bytes_sent;
    volatile uint64_t total_bytes_received;
    
    switch_bool_t initialized;
    switch_bool_t shutting_down;
} g_state = {0};

/* CURL write callback - stores response in request struct */
static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    async_http_request_t* req = (async_http_request_t*)userdata;
    size_t realsize = size * nmemb;
    
    /* Grow response buffer if needed */
    if (req->response_len + realsize + 1 > req->response_alloc) {
        size_t new_size = req->response_alloc ? req->response_alloc * 2 : 4096;
        while (new_size < req->response_len + realsize + 1) {
            new_size *= 2;
        }
        char* new_buf = realloc(req->response, new_size);
        if (!new_buf) return 0;
        req->response = new_buf;
        req->response_alloc = new_size;
    }
    
    memcpy(req->response + req->response_len, ptr, realsize);
    req->response_len += realsize;
    req->response[req->response_len] = '\0';
    
    return realsize;
}

/* Free a request and all its resources */
static void free_request(async_http_request_t* req) {
    if (!req) return;
    
    if (req->easy) {
        curl_easy_cleanup(req->easy);
    }
    if (req->headers) {
        curl_slist_free_all(req->headers);
    }
    free(req->url);
    free(req->body);
    free(req->response);
    free(req);
}

/* Dequeue next request (highest priority first) */
static async_http_request_t* dequeue_request(void) {
    async_http_request_t* req = NULL;
    
    switch_mutex_lock(g_state.queue_mutex);
    
    /* Check queues from highest to lowest priority */
    for (int p = ASYNC_HTTP_PRIORITY_HIGH; p >= ASYNC_HTTP_PRIORITY_LOW; p--) {
        if (g_state.queue_head[p]) {
            req = g_state.queue_head[p];
            g_state.queue_head[p] = req->next;
            if (!g_state.queue_head[p]) {
                g_state.queue_tail[p] = NULL;
            }
            req->next = NULL;
            __sync_fetch_and_sub(&g_state.queue_size, 1);
            break;
        }
    }
    
    switch_mutex_unlock(g_state.queue_mutex);
    return req;
}

/* Start processing a request (add to multi handle) */
static switch_status_t start_request(async_http_request_t* req) {
    CURL* easy = curl_easy_init();
    if (!easy) {
        return SWITCH_STATUS_FALSE;
    }
    
    req->easy = easy;
    
    curl_easy_setopt(easy, CURLOPT_URL, req->url);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDS, req->body);
    curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, (long)req->body_len);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, req);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, req->timeout_ms ? req->timeout_ms : ASYNC_HTTP_DEFAULT_TIMEOUT_MS);
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(easy, CURLOPT_TCP_KEEPINTVL, 30L);
    
    /* Connection pooling - reuse connections */
    curl_easy_setopt(easy, CURLOPT_FORBID_REUSE, 0L);
    curl_easy_setopt(easy, CURLOPT_FRESH_CONNECT, 0L);
    
    /* LATENCY OPTIMIZATIONS: Reduce 100-300ms delay on new connections */
    /* DNS caching: cache DNS for 5 minutes (avoid 50-200ms DNS lookups) */
    curl_easy_setopt(easy, CURLOPT_DNS_CACHE_TIMEOUT, 300L);
    /* Fast connect timeout: fail fast on unreachable hosts (saves 75s default) */
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
    /* TCP_NODELAY: disable Nagle algorithm (saves 40ms on small packets) */
    curl_easy_setopt(easy, CURLOPT_TCP_NODELAY, 1L);
    /* Happy Eyeballs: prefer IPv4 for faster connection (avoid IPv6 fallback delay) */
    curl_easy_setopt(easy, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    
    if (req->headers) {
        curl_easy_setopt(easy, CURLOPT_HTTPHEADER, req->headers);
    }
    
    /* Store request pointer for retrieval on completion */
    curl_easy_setopt(easy, CURLOPT_PRIVATE, req);
    
    CURLMcode mc = curl_multi_add_handle(g_state.multi, easy);
    if (mc != CURLM_OK) {
        curl_easy_cleanup(easy);
        req->easy = NULL;
        return SWITCH_STATUS_FALSE;
    }
    
    __sync_fetch_and_add(&g_state.active_count, 1);
    __sync_fetch_and_add(&g_state.total_bytes_sent, req->body_len);
    
    return SWITCH_STATUS_SUCCESS;
}

/* Complete a request (remove from multi, invoke callback) */
static void complete_request(async_http_request_t* req, CURLcode result) {
    long http_code = 0;
    
    if (req->easy) {
        curl_easy_getinfo(req->easy, CURLINFO_RESPONSE_CODE, &http_code);
        curl_multi_remove_handle(g_state.multi, req->easy);
    }
    
    __sync_fetch_and_sub(&g_state.active_count, 1);
    
    /* Update statistics */
    if (result == CURLE_OK && http_code >= 200 && http_code < 300) {
        __sync_fetch_and_add(&g_state.requests_completed, 1);
    } else if (result == CURLE_OPERATION_TIMEDOUT) {
        __sync_fetch_and_add(&g_state.requests_timeout, 1);
    } else {
        __sync_fetch_and_add(&g_state.requests_failed, 1);
    }
    
    if (req->response_len) {
        __sync_fetch_and_add(&g_state.total_bytes_received, req->response_len);
    }
    
    /* Invoke callback if provided */
    if (req->callback) {
        req->callback(req->url, http_code, req->response, req->response_len, req->user_data);
    }
    
    /* Log errors for debugging */
    if (result != CURLE_OK || http_code < 200 || http_code >= 300) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "async_http: request to %s failed: curl=%d http=%ld response=%s\n",
            req->url, result, http_code, req->response ? req->response : "(none)");
    }
    
    free_request(req);
}

/* Background pump thread - processes HTTP requests continuously */
static void* SWITCH_THREAD_FUNC pump_thread_func(switch_thread_t* thread, void* obj) {
    (void)thread;
    (void)obj;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "async_http: pump thread started\n");
    
    while (g_state.pump_thread_running && !g_state.shutting_down) {
        /* Process pending requests */
        async_http_pump();
        
        /* Sleep for 5ms before next pump cycle (lower = faster delivery, more CPU) */
        switch_sleep(5000); /* microseconds */
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "async_http: pump thread exiting\n");
    
    return NULL;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

switch_status_t async_http_init(void) {
    if (g_state.initialized) {
        return SWITCH_STATUS_SUCCESS;
    }
    
    /* Initialize libcurl */
    curl_global_init(CURL_GLOBAL_ALL);
    
    g_state.multi = curl_multi_init();
    if (!g_state.multi) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "async_http: failed to create curl multi handle\n");
        return SWITCH_STATUS_FALSE;
    }
    
    /* Configure multi handle for high concurrency */
    curl_multi_setopt(g_state.multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, ASYNC_HTTP_MAX_PARALLEL_REQUESTS);
    curl_multi_setopt(g_state.multi, CURLMOPT_MAX_HOST_CONNECTIONS, 50);
    curl_multi_setopt(g_state.multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
    
    /* Create memory pool and mutex */
    if (switch_core_new_memory_pool(&g_state.pool) != SWITCH_STATUS_SUCCESS) {
        curl_multi_cleanup(g_state.multi);
        return SWITCH_STATUS_FALSE;
    }
    
    switch_mutex_init(&g_state.queue_mutex, SWITCH_MUTEX_NESTED, g_state.pool);
    
    g_state.initialized = SWITCH_TRUE;
    g_state.shutting_down = SWITCH_FALSE;
    
    /* Start background pump thread */
    switch_threadattr_t* thd_attr = NULL;
    switch_threadattr_create(&thd_attr, g_state.pool);
    switch_threadattr_detach_set(thd_attr, 0); /* joinable */
    switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
    
    g_state.pump_thread_running = SWITCH_TRUE;
    if (switch_thread_create(&g_state.pump_thread, thd_attr, pump_thread_func, NULL, g_state.pool) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "async_http: failed to create pump thread\n");
        /* Continue anyway - pump can be called manually */
        g_state.pump_thread_running = SWITCH_FALSE;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "async_http: initialized (max_parallel=%d, max_queue=%d)\n",
        ASYNC_HTTP_MAX_PARALLEL_REQUESTS, ASYNC_HTTP_MAX_QUEUE_SIZE);
    
    return SWITCH_STATUS_SUCCESS;
}

void async_http_shutdown(void) {
    if (!g_state.initialized) {
        return;
    }
    
    g_state.shutting_down = SWITCH_TRUE;
    
    /* Stop pump thread */
    if (g_state.pump_thread_running) {
        g_state.pump_thread_running = SWITCH_FALSE;
        switch_status_t status;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "async_http: waiting for pump thread to exit...\n");
        switch_thread_join(&status, g_state.pump_thread);
        g_state.pump_thread = NULL;
    }
    
    /* Drain remaining requests with timeout */
    int timeout_ms = 5000;
    while (g_state.active_count > 0 && timeout_ms > 0) {
        async_http_pump();
        switch_sleep(10000); /* 10ms */
        timeout_ms -= 10;
    }
    
    if (g_state.active_count > 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "async_http: shutdown with %lu active requests\n",
            (unsigned long)g_state.active_count);
    }
    
    /* Clean up queued requests */
    for (int p = 0; p < 3; p++) {
        async_http_request_t* req = g_state.queue_head[p];
        while (req) {
            async_http_request_t* next = req->next;
            free_request(req);
            req = next;
        }
        g_state.queue_head[p] = NULL;
        g_state.queue_tail[p] = NULL;
    }
    
    if (g_state.multi) {
        curl_multi_cleanup(g_state.multi);
        g_state.multi = NULL;
    }
    
    curl_global_cleanup();
    
    if (g_state.pool) {
        switch_core_destroy_memory_pool(&g_state.pool);
        g_state.pool = NULL;
    }
    
    g_state.initialized = SWITCH_FALSE;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "async_http: shutdown complete (queued=%lu completed=%lu failed=%lu)\n",
        (unsigned long)g_state.requests_queued,
        (unsigned long)g_state.requests_completed,
        (unsigned long)g_state.requests_failed);
}

switch_status_t async_http_post(
    const char* url,
    const char* body,
    size_t body_len,
    const char** headers,
    uint32_t timeout_ms,
    async_http_callback_t callback,
    void* user_data,
    async_http_priority_t priority
) {
    if (!g_state.initialized || g_state.shutting_down) {
        return SWITCH_STATUS_FALSE;
    }
    
    if (!url || !body) {
        return SWITCH_STATUS_FALSE;
    }
    
    /* Check queue limit */
    if (g_state.queue_size >= ASYNC_HTTP_MAX_QUEUE_SIZE) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "async_http: queue full, dropping request to %s\n", url);
        return SWITCH_STATUS_FALSE;
    }
    
    /* Allocate request */
    async_http_request_t* req = calloc(1, sizeof(async_http_request_t));
    if (!req) {
        return SWITCH_STATUS_FALSE;
    }
    
    req->url = strdup(url);
    req->body_len = body_len ? body_len : strlen(body);
    req->body = malloc(req->body_len + 1);
    if (!req->url || !req->body) {
        free_request(req);
        return SWITCH_STATUS_FALSE;
    }
    memcpy(req->body, body, req->body_len);
    req->body[req->body_len] = '\0';
    
    req->timeout_ms = timeout_ms;
    req->callback = callback;
    req->user_data = user_data;
    req->priority = priority;
    
    /* Copy headers */
    if (headers) {
        for (int i = 0; headers[i]; i++) {
            req->headers = curl_slist_append(req->headers, headers[i]);
        }
    }
    
    /* Enqueue */
    switch_mutex_lock(g_state.queue_mutex);
    
    if (g_state.queue_tail[priority]) {
        g_state.queue_tail[priority]->next = req;
    } else {
        g_state.queue_head[priority] = req;
    }
    g_state.queue_tail[priority] = req;
    
    uint64_t new_size = __sync_add_and_fetch(&g_state.queue_size, 1);
    __sync_fetch_and_add(&g_state.requests_queued, 1);
    
    /* Update high watermark */
    uint64_t hwm = g_state.queue_high_watermark;
    while (new_size > hwm) {
        if (__sync_bool_compare_and_swap(&g_state.queue_high_watermark, hwm, new_size)) {
            break;
        }
        hwm = g_state.queue_high_watermark;
    }
    
    switch_mutex_unlock(g_state.queue_mutex);
    
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t async_http_post_json(
    const char* url,
    const char* json_body,
    async_http_callback_t callback,
    void* user_data
) {
    const char* headers[] = {
        "Content-Type: application/json",
        NULL
    };
    
    return async_http_post(url, json_body, 0, headers, 0, callback, user_data, ASYNC_HTTP_PRIORITY_NORMAL);
}

int async_http_pump(void) {
    if (!g_state.initialized || !g_state.multi) {
        return 0;
    }
    
    int completed = 0;
    
    /* Start new requests if we have capacity */
    while (g_state.active_count < ASYNC_HTTP_MAX_PARALLEL_REQUESTS) {
        async_http_request_t* req = dequeue_request();
        if (!req) break;
        
        if (start_request(req) != SWITCH_STATUS_SUCCESS) {
            /* Failed to start - invoke callback with error */
            if (req->callback) {
                req->callback(req->url, 0, NULL, 0, req->user_data);
            }
            __sync_fetch_and_add(&g_state.requests_failed, 1);
            free_request(req);
        }
    }
    
    /* Process active requests */
    int running;
    CURLMcode mc;
    
    do {
        mc = curl_multi_perform(g_state.multi, &running);
    } while (mc == CURLM_CALL_MULTI_PERFORM);
    
    /* Check for completed requests */
    CURLMsg* msg;
    int msgs_left;
    
    while ((msg = curl_multi_info_read(g_state.multi, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL* easy = msg->easy_handle;
            async_http_request_t* req = NULL;
            curl_easy_getinfo(easy, CURLINFO_PRIVATE, &req);
            
            if (req) {
                complete_request(req, msg->data.result);
                completed++;
            }
        }
    }
    
    return completed;
}

void async_http_get_stats(async_http_stats_t* stats) {
    if (!stats) return;
    
    stats->requests_queued = g_state.requests_queued;
    stats->requests_completed = g_state.requests_completed;
    stats->requests_failed = g_state.requests_failed;
    stats->requests_timeout = g_state.requests_timeout;
    stats->queue_high_watermark = g_state.queue_high_watermark;
    stats->current_queue_size = g_state.queue_size;
    stats->current_active = g_state.active_count;
    stats->total_bytes_sent = g_state.total_bytes_sent;
    stats->total_bytes_received = g_state.total_bytes_received;
}

void async_http_reset_stats(void) {
    g_state.requests_queued = 0;
    g_state.requests_completed = 0;
    g_state.requests_failed = 0;
    g_state.requests_timeout = 0;
    g_state.queue_high_watermark = 0;
    g_state.total_bytes_sent = 0;
    g_state.total_bytes_received = 0;
}

switch_bool_t async_http_is_ready(void) {
    return g_state.initialized && !g_state.shutting_down;
}

size_t async_http_queue_depth(void) {
    return (size_t)g_state.queue_size;
}
