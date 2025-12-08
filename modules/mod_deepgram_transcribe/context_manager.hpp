/*
 * context_manager.hpp - Thread-Local LWS Context Manager for mod_deepgram_transcribe
 * 
 * PHASE 1 OPTIMIZATION - MINIMUM THREADS & NON-BLOCKING I/O:
 * - Thread-local storage eliminates mutex contention entirely
 * - Each service thread gets its own LWS context (zero contention)
 * - Atomic counters for monitoring without locks
 * - Designed for 10,000+ concurrent calls with minimal threads
 * 
 * Usage:
 *   // In service thread:
 *   auto* context = ContextManager::getThreadContext(); // Zero mutex overhead
 *   
 *   // Monitoring:
 *   uint32_t total = ContextManager::getTotalContexts();
 */

#ifndef __DG_CONTEXT_MANAGER_HPP__
#define __DG_CONTEXT_MANAGER_HPP__

#include <libwebsockets.h>
#include <atomic>
#include <memory>

namespace deepgram {

/**
 * Thread-Local LWS Context Manager (Phase 1 Optimization)
 * 
 * Eliminates the primary mutex bottleneck in mod_deepgram_transcribe by giving
 * each service thread its own LWS context stored in thread-local storage.
 * 
 * Benefits vs Original Mutex-Based Approach:
 * - Zero mutex contention (eliminates AudioPipe::mutex_connects bottleneck)
 * - Better CPU cache locality (each thread owns its context)  
 * - Simplified error handling (no shared state corruption)
 * - Linear scalability (performance scales with thread count)
 */
class ContextManager {
public:
    /**
     * Get thread-local LWS context (zero contention)
     * 
     * @return LWS context for current thread, or nullptr on error
     */
    static struct lws_context* getThreadContext();
    
    /**
     * Get total number of active contexts across all threads
     * 
     * @return Atomic count of contexts (safe for monitoring)
     */
    static uint32_t getTotalContexts() { 
        return g_total_contexts.load(std::memory_order_relaxed); 
    }
    
    /**
     * Initialize context manager with protocol configuration
     * 
     * @param protocol_name Protocol name for LWS
     * @param callback LWS callback function
     * @return true if successful
     */
    static bool initialize(const char* protocol_name, 
                          int (*callback)(struct lws*, enum lws_callback_reasons, void*, void*, size_t));
    
    /**
     * Shutdown thread-local context (called automatically on thread exit)
     */
    static void shutdownThreadContext();
    
    /**
     * Shutdown all contexts (module cleanup)
     */
    static void shutdownAll();

private:
    /**
     * Create new LWS context for current thread
     * 
     * @return New context or nullptr on error
     */
    static struct lws_context* createThreadContext();
    
    // Thread-local context storage (Phase 1: eliminates mutex contention)
    thread_local static struct lws_context* t_lws_context;
    
    // Atomic counters for monitoring (Phase 1: lock-free statistics)
    static std::atomic<uint32_t> g_total_contexts;
    
    // Context configuration (set during initialize)
    static const char* s_protocol_name;
    static int (*s_lws_callback)(struct lws*, enum lws_callback_reasons, void*, void*, size_t);
    static bool s_initialized;
};

} // namespace deepgram

#endif // __DG_CONTEXT_MANAGER_HPP__