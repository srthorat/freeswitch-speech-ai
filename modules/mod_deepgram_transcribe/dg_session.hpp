/*
 * dg_session.hpp - Crash-safe Session Lifecycle using shared_ptr
 * 
 * HIGH SCALE ARCHITECTURE:
 * - Uses std::enable_shared_from_this for safe async callbacks
 * - Self-anchoring pattern prevents use-after-free
 * - Modeled after mod_google_transcribe_async's Session class
 * - Integrates with AudioPipe for WebSocket handling
 * 
 * The self-anchoring pattern:
 *   1. Before starting async operation, session holds shared_ptr to itself
 *   2. Async callback completes, releases the anchor
 *   3. Session is destroyed only when all anchors are released
 * 
 * This eliminates crashes from:
 *   - Session destroyed while WebSocket callback pending
 *   - Race between call hangup and transcription callback
 *   - Frame callback accessing destroyed session
 */

#ifndef __DG_SESSION_HPP__
#define __DG_SESSION_HPP__

#include <switch.h>
#include <speex/speex_resampler.h>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>

#include "audio_pipe.hpp"
#include "mod_deepgram_transcribe.h"

namespace deepgram {

// Forward declarations
class DgSession;

/**
 * Passkey for DgSession constructor
 * Only Create() can provide this, making constructor effectively private
 */
class DgSessionKey {
    friend class DgSession;
    DgSessionKey() = default;
};

/**
 * Session statistics for monitoring
 */
struct SessionStats {
    uint64_t frames_processed{0};
    uint64_t samples_in{0};
    uint64_t samples_out{0};
    uint64_t bytes_sent{0};
    uint32_t source_rate{0};
    uint32_t target_rate{0};
    switch_time_t start_time{0};
    switch_time_t last_log_time{0};
};

/**
 * DgSession - Crash-safe transcription session
 * 
 * Uses shared_ptr and enable_shared_from_this to ensure the session
 * stays alive while async operations are pending.
 */
class DgSession : public std::enable_shared_from_this<DgSession> {
public:
    /**
     * Factory method - creates session wrapped in shared_ptr
     * 
     * @param fs_session FreeSWITCH session
     * @param rate Target sample rate
     * @param channels Number of audio channels
     * @param lang Language code
     * @param interim Enable interim results
     * @param bugname Media bug name
     * @param metadata Session metadata JSON
     * @param handler Response callback
     * @return shared_ptr to new session, or nullptr on failure
     */
    static std::shared_ptr<DgSession> Create(
        switch_core_session_t* fs_session,
        uint32_t rate,
        int channels,
        const char* lang,
        int interim,
        const char* bugname,
        const char* metadata,
        responseHandler_t handler
    );
    
    /**
     * Destructor - cleans up all resources
     */
    ~DgSession();
    
    /**
     * Start the transcription session
     * Connects to Deepgram WebSocket
     * 
     * @return true if started successfully
     */
    bool Start();
    
    /**
     * Send audio data to Deepgram
     * Lock-free, returns immediately
     * 
     * @param data Audio data
     * @param len Data length in bytes
     */
    void SendAudio(const void* data, size_t len);
    
    /**
     * Process a frame from the media bug (with resampling if needed)
     * 
     * @param bug Media bug handle
     * @return SWITCH_TRUE to continue, SWITCH_FALSE to stop
     */
    switch_bool_t ProcessFrame(switch_media_bug_t* bug);
    
    /**
     * Stop the transcription session gracefully
     */
    void Stop();
    
    /**
     * Check if session is connected
     */
    bool IsConnected() const {
        return m_connected.load(std::memory_order_acquire);
    }
    
    /**
     * Check if session is stopping
     */
    bool IsStopping() const {
        return m_stopping.load(std::memory_order_acquire);
    }
    
    /**
     * Get session UUID
     */
    const char* GetUUID() const {
        return m_uuid.c_str();
    }
    
    /**
     * Get session ID (numeric)
     */
    uint32_t GetId() const {
        return m_id;
    }
    
    /**
     * Get bug name
     */
    const char* GetBugname() const {
        return m_bugname.c_str();
    }
    
    /**
     * Get metadata
     */
    const char* GetMetadata() const {
        return m_metadata.c_str();
    }
    
    /**
     * Get the FreeSWITCH session (with read lock held)
     * Returns nullptr if session is no longer valid
     */
    switch_core_session_t* GetFsSession() const {
        return m_fs_session;
    }
    
    /**
     * Get session statistics
     */
    const SessionStats& GetStats() const {
        return m_stats;
    }
    
    // Prevent copying
    DgSession(const DgSession&) = delete;
    DgSession& operator=(const DgSession&) = delete;

    /**
     * Constructor - requires passkey, use Create() factory method
     * Public for make_shared but passkey ensures only Create() can call it
     */
    DgSession(
        DgSessionKey key,
        switch_core_session_t* fs_session,
        uint32_t rate,
        int channels,
        const char* lang,
        int interim,
        const char* bugname,
        const char* metadata,
        responseHandler_t handler
    );

private:
    
    /**
     * Initialize resampler if needed
     * 
     * @param codec_rate Codec sample rate
     * @return true if successful
     */
    bool InitResampler(uint32_t codec_rate);
    
    /**
     * AudioPipe event callback (static, dispatches to instance)
     */
    static void AudioPipeCallback(
        const char* sessionId,
        AudioPipe::NotifyEvent_t event,
        const char* message,
        bool finished
    );
    
    /**
     * Handle AudioPipe events
     */
    void OnAudioPipeEvent(
        AudioPipe::NotifyEvent_t event,
        const char* message,
        bool finished
    );
    
    /**
     * Fire FreeSWITCH event with transcription result
     */
    void FireTranscriptEvent(const char* eventName, const char* json, bool finished);

    // FreeSWITCH session (with read lock)
    switch_core_session_t* m_fs_session;
    
    // Session identification
    std::string m_uuid;
    uint32_t m_id;
    std::string m_bugname;
    std::string m_metadata;
    
    // Audio configuration
    uint32_t m_rate;
    int m_channels;
    std::string m_lang;
    int m_interim;
    
    // Resampler
    SpeexResamplerState* m_resampler{nullptr};
    uint32_t m_codec_rate{0};
    
    // Response handler
    responseHandler_t m_handler;
    
    // AudioPipe for WebSocket communication
    std::unique_ptr<AudioPipe> m_audio_pipe;
    
    // State flags (atomic for thread safety)
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_stopping{false};
    std::atomic<bool> m_buffer_overrun_notified{false};
    
    // Self-anchors for async operations
    // These prevent destruction while callbacks are pending
    std::shared_ptr<DgSession> m_connect_anchor;
    std::shared_ptr<DgSession> m_operation_anchor;
    
    // Mutex for state changes
    std::mutex m_mutex;
    
    // Statistics
    SessionStats m_stats;
    
    // Session counter for unique IDs
    static std::atomic<uint32_t> s_id_counter;
    
    // Session registry for callback dispatch
    static std::mutex s_registry_mutex;
    static std::unordered_map<std::string, std::weak_ptr<DgSession>> s_registry;
};

/**
 * BugData - Data attached to media bug
 * 
 * Holds shared_ptr to DgSession, ensuring session stays alive
 * while media bug is active.
 */
struct BugData {
    std::shared_ptr<DgSession> session;
    
    BugData(std::shared_ptr<DgSession> s) : session(std::move(s)) {}
};

/**
 * PrivateDataPool - Memory pool for private_t structures
 * 
 * Pre-allocates private_t structures to avoid malloc in hot path.
 * Uses lock-free operations for acquire/release.
 */
class PrivateDataPool {
public:
    /**
     * Initialize the pool
     * 
     * @param capacity Number of structures to pre-allocate
     * @return true if successful
     */
    static bool Initialize(size_t capacity = 2000);
    
    /**
     * Shutdown the pool and free all memory
     */
    static void Shutdown();
    
    /**
     * Acquire a private_t structure from the pool
     * 
     * @return Pointer to structure, or nullptr if pool exhausted
     */
    static private_t* Acquire();
    
    /**
     * Release a private_t structure back to the pool
     * 
     * @param p Structure to release
     */
    static void Release(private_t* p);
    
    /**
     * Check if pool is initialized
     */
    static bool IsInitialized();
    
    /**
     * Log pool statistics
     */
    static void LogStats();
    
    /**
     * Pool statistics
     */
    struct Stats {
        std::atomic<uint64_t> acquires{0};
        std::atomic<uint64_t> releases{0};
        std::atomic<uint64_t> pool_hits{0};
        std::atomic<uint64_t> pool_misses{0};
        std::atomic<uint64_t> current_in_use{0};
        std::atomic<uint64_t> high_water_mark{0};
        size_t capacity{0};
    };
    
    static const Stats& GetStats();
    
private:
    // Pool node for free list
    struct PoolNode {
        PoolNode* next;
        private_t data;
        bool in_use;
    };
    
    static std::atomic<PoolNode*> s_head;
    static PoolNode* s_nodes;
    static size_t s_capacity;
    static std::atomic<bool> s_initialized;
    static Stats s_stats;
    static std::mutex s_init_mutex;
};

} // namespace deepgram

#endif // __DG_SESSION_HPP__
