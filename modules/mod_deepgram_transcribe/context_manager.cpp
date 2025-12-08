/*
 * context_manager.cpp - Implementation of Thread-Local Context Manager
 * 
 * PHASE 1 OPTIMIZATION IMPLEMENTATION:
 * This replaces the mutex-based context management in mod_deepgram_transcribe
 * with thread-local storage, eliminating contention entirely.
 */

#include "context_manager.hpp"
#include <cstring>

namespace deepgram {

// Static member initialization
thread_local struct lws_context* ContextManager::t_lws_context = nullptr;
std::atomic<uint32_t> ContextManager::g_total_contexts{0};
const char* ContextManager::s_protocol_name = nullptr;
int (*ContextManager::s_lws_callback)(struct lws*, enum lws_callback_reasons, void*, void*, size_t) = nullptr;
bool ContextManager::s_initialized = false;

bool ContextManager::initialize(const char* protocol_name,
                               int (*callback)(struct lws*, enum lws_callback_reasons, void*, void*, size_t)) {
    if (s_initialized) {
        return true; // Already initialized
    }
    
    s_protocol_name = protocol_name;
    s_lws_callback = callback;
    s_initialized = true;
    
    return true;
}

struct lws_context* ContextManager::getThreadContext() {
    if (!s_initialized) {
        return nullptr;
    }
    
    // Thread-local context - no synchronization needed (Phase 1 optimization)
    if (!t_lws_context) {
        t_lws_context = createThreadContext();
        if (t_lws_context) {
            g_total_contexts.fetch_add(1, std::memory_order_relaxed);
        }
    }
    
    return t_lws_context;
}

struct lws_context* ContextManager::createThreadContext() {
    if (!s_initialized || !s_protocol_name || !s_lws_callback) {
        return nullptr;
    }
    
    // LWS context creation info
    struct lws_context_creation_info info;
    std::memset(&info, 0, sizeof(info));
    
    // Protocol definition for this context
    static thread_local struct lws_protocols protocols[2];
    protocols[0].name = s_protocol_name;
    protocols[0].callback = s_lws_callback;
    protocols[0].per_session_data_size = sizeof(void*);
    protocols[0].rx_buffer_size = 8192;
    protocols[0].id = 0;
    protocols[0].user = nullptr;
    protocols[0].tx_packet_size = 8192;
    
    protocols[1].name = nullptr;
    protocols[1].callback = nullptr;
    protocols[1].per_session_data_size = 0;
    protocols[1].rx_buffer_size = 0;
    protocols[1].id = 0;
    protocols[1].user = nullptr;
    protocols[1].tx_packet_size = 0;
    
    // Context configuration optimized for high concurrency
    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.gid = -1;
    info.uid = -1;
    info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
    
    // Performance tuning for high-scale deployment
    info.ka_time = 55;                    // TCP keep-alive timer (55 seconds)
    info.ka_probes = 4;                   // Keep-alive probes before close
    info.ka_interval = 5;                 // Interval between probes
    info.timeout_secs = 10;               // Network operation timeout
    info.keepalive_timeout = 5;           // HTTP/1.1 idle connection timeout
    info.timeout_secs_ah_idle = 10;       // Idle connection timeout
    
    return lws_create_context(&info);
}

void ContextManager::shutdownThreadContext() {
    if (t_lws_context) {
        lws_context_destroy(t_lws_context);
        t_lws_context = nullptr;
        g_total_contexts.fetch_sub(1, std::memory_order_relaxed);
    }
}

void ContextManager::shutdownAll() {
    // Each thread will clean up its own context via thread_local destructor
    // Just mark as no longer initialized
    s_initialized = false;
    s_protocol_name = nullptr;
    s_lws_callback = nullptr;
}

} // namespace deepgram