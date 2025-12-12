/*
 * context_manager.cpp - Implementation of Thread-Local Context Manager
 * 
 * PHASE 1 OPTIMIZATION IMPLEMENTATION:
 * This replaces the mutex-based context management in mod_deepgram_transcribe
 * with thread-local storage, eliminating contention entirely.
 */

#include "context_manager.hpp"
#include <cstring>
#include <vector>
#include <cstdlib>

namespace deepgram {

// Static member initialization
thread_local struct lws_context* ContextManager::t_lws_context = nullptr;
thread_local void* ContextManager::t_vhd = nullptr;
std::atomic<uint32_t> ContextManager::g_total_contexts{0};
const char* ContextManager::s_protocol_name = nullptr;
int (*ContextManager::s_lws_callback)(struct lws*, enum lws_callback_reasons, void*, void*, size_t) = nullptr;
bool ContextManager::s_initialized = false;
std::map<struct lws_context*, void*> ContextManager::s_context_vhd_map;
std::mutex ContextManager::s_vhd_map_mutex;

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
    /* Environment-based tuning */
    const char* env_ka_time = std::getenv("MOD_DEEPGRAM_KA_TIME");
    int ka_time = env_ka_time ? std::atoi(env_ka_time) : 55;
    info.ka_time = ka_time > 0 ? ka_time : 55;
    if (info.ka_time > 0) {
        info.ka_interval = 10;
        info.ka_probes = 3;
    }

    const char* env_timeout = std::getenv("MOD_DEEPGRAM_TIMEOUT");
    int timeout = env_timeout ? std::atoi(env_timeout) : 10;
    info.timeout_secs = timeout > 0 ? timeout : 10;               // Network operation timeout
    info.keepalive_timeout = 5;           // HTTP/1.1 idle connection timeout
    info.timeout_secs_ah_idle = 10;       // Idle connection timeout
    
    // Serialize context creation to prevent OpenSSL race conditions
    // LWS/OpenSSL global init is not thread-safe by default
    static std::mutex s_creation_mutex;
    std::lock_guard<std::mutex> lock(s_creation_mutex);
    
    return lws_create_context(&info);
}

void ContextManager::shutdownThreadContext() {
    if (t_lws_context) {
        lws_context_destroy(t_lws_context);
        t_lws_context = nullptr;
        g_total_contexts.fetch_sub(1, std::memory_order_relaxed);
    }
}

void* ContextManager::getThreadVhd() {
    return t_vhd;
}

void ContextManager::setThreadVhd(void* vhd) {
    t_vhd = vhd;
}

void* ContextManager::getContextVhd(struct lws_context* context) {
    if (!context) return nullptr;
    
    std::lock_guard<std::mutex> lock(s_vhd_map_mutex);
    auto it = s_context_vhd_map.find(context);
    return (it != s_context_vhd_map.end()) ? it->second : nullptr;
}

void ContextManager::setContextVhd(struct lws_context* context, void* vhd) {
    if (!context) return;
    
    std::lock_guard<std::mutex> lock(s_vhd_map_mutex);
    s_context_vhd_map[context] = vhd;
}

void ContextManager::shutdownAll() {
    // Each thread will clean up its own context via thread_local destructor
    // Just mark as no longer initialized
    s_initialized = false;
    s_protocol_name = nullptr;
    s_lws_callback = nullptr;
}

void ContextManager::wakeAllServiceThreads() {
    // CRITICAL FIX: Wake up all sleeping service threads
    // Without this, threads sleep in poll() for up to 1ms per iteration,
    // accumulating 18+ second delays while connection requests wait in queues
    
    // SAFETY: Copy context pointers while holding mutex, then release mutex
    // before calling lws_cancel_service() to avoid deadlock
    std::vector<struct lws_context*> contexts;
    {
        std::lock_guard<std::mutex> lock(s_vhd_map_mutex);
        contexts.reserve(s_context_vhd_map.size());
        for (const auto& pair : s_context_vhd_map) {
            // Only wake contexts that are fully initialized (have vhd)
            if (pair.first && pair.second) {
                contexts.push_back(pair.first);
            }
        }
    }
    
    // Wake all contexts WITHOUT holding mutex (avoid deadlock)
    for (auto* ctx : contexts) {
        if (ctx) {
            lws_cancel_service(ctx);
        }
    }
}

} // namespace deepgram