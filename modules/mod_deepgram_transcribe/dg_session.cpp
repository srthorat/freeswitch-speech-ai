/*
 * dg_session.cpp - DgSession and PrivateDataPool Implementation
 * 
 * Implements crash-safe session lifecycle and memory pooling.
 */

#include "dg_session.hpp"
#include <switch_json.h>
#include <sstream>
#include <regex>

namespace deepgram {

// Static member initialization
std::atomic<uint32_t> DgSession::s_id_counter{0};
std::mutex DgSession::s_registry_mutex;
std::unordered_map<std::string, std::weak_ptr<DgSession>> DgSession::s_registry;

// PrivateDataPool static members
std::atomic<PrivateDataPool::PoolNode*> PrivateDataPool::s_head{nullptr};
PrivateDataPool::PoolNode* PrivateDataPool::s_nodes{nullptr};
size_t PrivateDataPool::s_capacity{0};
std::atomic<bool> PrivateDataPool::s_initialized{false};
PrivateDataPool::Stats PrivateDataPool::s_stats;
std::mutex PrivateDataPool::s_init_mutex;

// ============================================================================
// PrivateDataPool Implementation
// ============================================================================

bool PrivateDataPool::Initialize(size_t capacity) {
    std::lock_guard<std::mutex> lock(s_init_mutex);
    
    if (s_initialized.load(std::memory_order_acquire)) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "[POOL] PrivateDataPool already initialized\n");
        return true;
    }
    
    // Allocate pool nodes
    s_nodes = new (std::nothrow) PoolNode[capacity];
    if (!s_nodes) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "[POOL] Failed to allocate PrivateDataPool with capacity %zu\n", capacity);
        return false;
    }
    
    s_capacity = capacity;
    s_stats.capacity = capacity;
    
    // Build free list
    for (size_t i = 0; i < capacity; i++) {
        s_nodes[i].next = (i + 1 < capacity) ? &s_nodes[i + 1] : nullptr;
        s_nodes[i].in_use = false;
        std::memset(&s_nodes[i].data, 0, sizeof(private_t));
    }
    
    s_head.store(&s_nodes[0], std::memory_order_release);
    s_initialized.store(true, std::memory_order_release);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "[POOL] PrivateDataPool initialized: capacity=%zu, size=%.1fMB\n",
        capacity, (capacity * sizeof(PoolNode)) / (1024.0 * 1024.0));
    
    return true;
}

void PrivateDataPool::Shutdown() {
    std::lock_guard<std::mutex> lock(s_init_mutex);
    
    if (!s_initialized.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    
    LogStats();
    
    delete[] s_nodes;
    s_nodes = nullptr;
    s_head.store(nullptr, std::memory_order_release);
    s_capacity = 0;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "[POOL] PrivateDataPool shutdown complete\n");
}

private_t* PrivateDataPool::Acquire() {
    if (!s_initialized.load(std::memory_order_acquire)) {
        // Pool not initialized - fall back to malloc
        s_stats.acquires.fetch_add(1, std::memory_order_relaxed);
        s_stats.pool_misses.fetch_add(1, std::memory_order_relaxed);
        
        private_t* p = static_cast<private_t*>(malloc(sizeof(private_t)));
        if (p) {
            std::memset(p, 0, sizeof(private_t));
        }
        return p;
    }
    
    s_stats.acquires.fetch_add(1, std::memory_order_relaxed);
    
    // Lock-free pop from free list
    PoolNode* node = s_head.load(std::memory_order_acquire);
    while (node) {
        PoolNode* next = node->next;
        if (s_head.compare_exchange_weak(node, next,
                std::memory_order_release,
                std::memory_order_relaxed)) {
            // Successfully acquired
            node->in_use = true;
            node->next = nullptr;
            
            s_stats.pool_hits.fetch_add(1, std::memory_order_relaxed);
            uint64_t in_use = s_stats.current_in_use.fetch_add(1, std::memory_order_relaxed) + 1;
            
            // Update high water mark
            uint64_t hwm = s_stats.high_water_mark.load(std::memory_order_relaxed);
            while (in_use > hwm) {
                if (s_stats.high_water_mark.compare_exchange_weak(hwm, in_use,
                        std::memory_order_relaxed)) {
                    break;
                }
            }
            
            // Zero the data
            std::memset(&node->data, 0, sizeof(private_t));
            return &node->data;
        }
        // CAS failed, retry
    }
    
    // Pool exhausted - fall back to malloc
    s_stats.pool_misses.fetch_add(1, std::memory_order_relaxed);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
        "[POOL] PrivateDataPool exhausted! capacity=%zu, in_use=%lu. Consider increasing pool size.\n",
        s_capacity, (unsigned long)s_stats.current_in_use.load());
    
    private_t* p = static_cast<private_t*>(malloc(sizeof(private_t)));
    if (p) {
        std::memset(p, 0, sizeof(private_t));
    }
    return p;
}

void PrivateDataPool::Release(private_t* p) {
    if (!p) return;
    
    s_stats.releases.fetch_add(1, std::memory_order_relaxed);
    
    // Check if this came from the pool
    if (s_initialized.load(std::memory_order_acquire) && s_nodes) {
        // Check if pointer is within our pool
        uintptr_t ptr = reinterpret_cast<uintptr_t>(p);
        uintptr_t pool_start = reinterpret_cast<uintptr_t>(s_nodes);
        uintptr_t pool_end = pool_start + (s_capacity * sizeof(PoolNode));
        
        if (ptr >= pool_start && ptr < pool_end) {
            // Calculate the node from the data pointer
            // node->data is at offset offsetof(PoolNode, data) from node
            constexpr size_t data_offset = offsetof(PoolNode, data);
            PoolNode* node = reinterpret_cast<PoolNode*>(
                reinterpret_cast<uint8_t*>(p) - data_offset);
            
            // Verify this is actually a valid node
            uintptr_t node_ptr = reinterpret_cast<uintptr_t>(node);
            if ((node_ptr - pool_start) % sizeof(PoolNode) == 0) {
                node->in_use = false;
                
                // Lock-free push to free list
                PoolNode* head = s_head.load(std::memory_order_relaxed);
                do {
                    node->next = head;
                } while (!s_head.compare_exchange_weak(head, node,
                        std::memory_order_release,
                        std::memory_order_relaxed));
                
                s_stats.current_in_use.fetch_sub(1, std::memory_order_relaxed);
                return;
            }
        }
    }
    
    // Not from pool - free normally
    free(p);
}

bool PrivateDataPool::IsInitialized() {
    return s_initialized.load(std::memory_order_acquire);
}

void PrivateDataPool::LogStats() {
    const auto& st = s_stats;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "[POOL-STATS] PrivateDataPool: capacity=%zu, in_use=%lu, high_water=%lu, "
        "acquires=%lu, releases=%lu, hits=%lu, misses=%lu\n",
        s_capacity,
        (unsigned long)st.current_in_use.load(),
        (unsigned long)st.high_water_mark.load(),
        (unsigned long)st.acquires.load(),
        (unsigned long)st.releases.load(),
        (unsigned long)st.pool_hits.load(),
        (unsigned long)st.pool_misses.load());
}

const PrivateDataPool::Stats& PrivateDataPool::GetStats() {
    return s_stats;
}

// ============================================================================
// DgSession Implementation
// ============================================================================

std::shared_ptr<DgSession> DgSession::Create(
    switch_core_session_t* fs_session,
    uint32_t rate,
    int channels,
    const char* lang,
    int interim,
    const char* bugname,
    const char* metadata,
    responseHandler_t handler
) {
    if (!fs_session) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "DgSession::Create: null fs_session\n");
        return nullptr;
    }
    
    // Create session using make_shared for efficiency
    // DgSessionKey{} can only be created here (friend class)
    auto session = std::make_shared<DgSession>(
        DgSessionKey{}, fs_session, rate, channels, lang, interim, bugname, metadata, handler
    );
    
    if (!session) {
        return nullptr;
    }
    
    // Register in session registry for callback dispatch
    {
        std::lock_guard<std::mutex> lock(s_registry_mutex);
        s_registry[session->m_uuid] = session;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "DgSession::Create: created session %s (id=%u)\n",
        session->m_uuid.c_str(), session->m_id);
    
    return session;
}

DgSession::DgSession(
    DgSessionKey /* key */,
    switch_core_session_t* fs_session,
    uint32_t rate,
    int channels,
    const char* lang,
    int interim,
    const char* bugname,
    const char* metadata,
    responseHandler_t handler
) : m_fs_session(fs_session),
    m_rate(rate),
    m_channels(channels),
    m_interim(interim),
    m_handler(handler)
{
    // Acquire read lock on FreeSWITCH session
    switch_core_session_read_lock(fs_session);
    
    // Get UUID
    const char* uuid = switch_core_session_get_uuid(fs_session);
    m_uuid = uuid ? uuid : "unknown";
    
    // Generate unique session ID
    m_id = s_id_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    
    // Copy strings
    m_bugname = bugname ? bugname : MY_BUG_NAME;
    m_metadata = metadata ? metadata : "";
    m_lang = lang ? lang : "en-US";
    
    // Initialize stats
    m_stats.start_time = switch_time_now();
    m_stats.last_log_time = m_stats.start_time;
    m_stats.target_rate = rate;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "DgSession[%u]: constructed for %s\n", m_id, m_uuid.c_str());
}

DgSession::~DgSession() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "DgSession[%u]: destroying session %s\n", m_id, m_uuid.c_str());
    
    // Unregister from registry
    {
        std::lock_guard<std::mutex> lock(s_registry_mutex);
        s_registry.erase(m_uuid);
    }
    
    // Stop if not already stopped
    if (!m_stopping.load(std::memory_order_acquire)) {
        Stop();
    }
    
    // Clean up resampler
    if (m_resampler) {
        speex_resampler_destroy(m_resampler);
        m_resampler = nullptr;
    }
    
    // AudioPipe cleanup is automatic via unique_ptr
    
    // Release FreeSWITCH session read lock
    if (m_fs_session) {
        switch_core_session_rwunlock(m_fs_session);
        m_fs_session = nullptr;
    }
    
    // Log final stats
    if (m_stats.frames_processed > 0) {
        double elapsed = (switch_time_now() - m_stats.start_time) / 1000000.0;
        double fps = m_stats.frames_processed / elapsed;
        double mb = m_stats.bytes_sent / (1024.0 * 1024.0);
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "DgSession[%u]: final stats - frames=%lu, bytes=%.2fMB, fps=%.1f, duration=%.1fs\n",
            m_id,
            (unsigned long)m_stats.frames_processed,
            mb, fps, elapsed);
    }
}

bool DgSession::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_connected.load(std::memory_order_acquire) || 
        m_stopping.load(std::memory_order_acquire)) {
        return false;
    }
    
    // Get codec rate for resampler initialization
    switch_codec_implementation_t read_impl = {};
    switch_core_session_get_read_impl(m_fs_session, &read_impl);
    m_codec_rate = !strcasecmp(read_impl.iananame, "g722") ?
        read_impl.actual_samples_per_second : read_impl.samples_per_second;
    
    // Initialize resampler if needed
    if (!InitResampler(m_codec_rate)) {
        return false;
    }
    
    // Build WebSocket path
    std::ostringstream path;
    path << "/v1/listen?";
    path << "language=" << m_lang;
    path << "&encoding=linear16";
    path << "&sample_rate=" << m_rate;
    
    if (m_channels == 2) {
        path << "&multichannel=true&channels=2";
    }
    
    if (m_interim) {
        path << "&interim_results=true";
    }
    
    // Get API key
    switch_channel_t* channel = switch_core_session_get_channel(m_fs_session);
    const char* apiKey = switch_channel_get_variable(channel, "DEEPGRAM_API_KEY");
    if (!apiKey) {
        apiKey = std::getenv("DEEPGRAM_API_KEY");
    }
    
    if (!apiKey) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "DgSession[%u]: no DEEPGRAM_API_KEY\n", m_id);
        return false;
    }
    
    // Calculate buffer size
    size_t buflen = 65536;  // 64KB default
    
    // Create AudioPipe
    m_audio_pipe = std::make_unique<AudioPipe>(
        m_uuid.c_str(),
        "api.deepgram.com",
        443,
        path.str().c_str(),
        buflen,
        320,  // min freespace
        apiKey,
        AudioPipeCallback
    );
    
    if (!m_audio_pipe) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "DgSession[%u]: failed to create AudioPipe\n", m_id);
        return false;
    }
    
    // Self-anchor before starting async connect
    m_connect_anchor = shared_from_this();
    
    // Start connection
    m_audio_pipe->connect();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "DgSession[%u]: starting connection to Deepgram\n", m_id);
    
    return true;
}

void DgSession::SendAudio(const void* data, size_t len) {
    if (!m_audio_pipe || !m_connected.load(std::memory_order_acquire) ||
        m_stopping.load(std::memory_order_acquire)) {
        return;
    }
    
    size_t pushed = m_audio_pipe->pushAudio(data, len);
    if (pushed > 0) {
        m_stats.bytes_sent += pushed;
    }
}

switch_bool_t DgSession::ProcessFrame(switch_media_bug_t* bug) {
    if (!m_audio_pipe || !m_connected.load(std::memory_order_acquire) ||
        m_stopping.load(std::memory_order_acquire)) {
        return SWITCH_TRUE;
    }
    
    // Check buffer space
    size_t available = m_audio_pipe->audioSpaceAvailable();
    if (available < m_audio_pipe->binaryMinSpace()) {
        if (!m_buffer_overrun_notified.exchange(true, std::memory_order_acq_rel)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "DgSession[%u]: buffer overrun, dropping audio\n", m_id);
            FireTranscriptEvent(TRANSCRIBE_EVENT_BUFFER_OVERRUN, nullptr, false);
        }
        return SWITCH_TRUE;
    }
    
    uint8_t frame_buffer[SWITCH_RECOMMENDED_BUFFER_SIZE];
    switch_frame_t frame = {};
    frame.data = frame_buffer;
    frame.buflen = sizeof(frame_buffer);
    
    while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
        if (frame.datalen == 0) break;
        
        m_stats.frames_processed++;
        
        if (m_resampler) {
            // Resample audio
            uint8_t resample_out[SWITCH_RECOMMENDED_BUFFER_SIZE];
            spx_uint32_t in_len = frame.samples;
            spx_uint32_t out_len = sizeof(resample_out) / (2 * m_channels);
            
            m_stats.samples_in += in_len;
            
            speex_resampler_process_interleaved_int(
                m_resampler,
                reinterpret_cast<const spx_int16_t*>(frame.data),
                &in_len,
                reinterpret_cast<spx_int16_t*>(resample_out),
                &out_len
            );
            
            m_stats.samples_out += out_len;
            
            if (out_len > 0) {
                size_t bytes = out_len * 2 * m_channels;
                SendAudio(resample_out, bytes);
            }
        } else {
            // Direct send (passthrough)
            m_stats.samples_in += frame.datalen / 2;
            m_stats.samples_out += frame.datalen / 2;
            SendAudio(frame.data, frame.datalen);
        }
    }
    
    return SWITCH_TRUE;
}

void DgSession::Stop() {
    bool expected = false;
    if (!m_stopping.compare_exchange_strong(expected, true,
            std::memory_order_acq_rel)) {
        return;  // Already stopping
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "DgSession[%u]: stopping\n", m_id);
    
    if (m_audio_pipe) {
        m_audio_pipe->finish();
        m_audio_pipe->waitForClose();
    }
    
    m_connected.store(false, std::memory_order_release);
    
    // Fire session stop event
    FireTranscriptEvent(TRANSCRIBE_EVENT_SESSION_STOP, m_metadata.c_str(), true);
}

bool DgSession::InitResampler(uint32_t codec_rate) {
    m_stats.source_rate = codec_rate;
    
    if (codec_rate == m_rate) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "DgSession[%u]: passthrough mode (rate=%u)\n", m_id, m_rate);
        return true;
    }
    
    // Get resampler quality from environment
    const char* quality_env = std::getenv("MOD_DEEPGRAM_RESAMPLE_QUALITY");
    int quality = quality_env ? std::atoi(quality_env) : 2;
    quality = std::max(0, std::min(quality, 10));
    
    int err = 0;
    m_resampler = speex_resampler_init(m_channels, codec_rate, m_rate, quality, &err);
    
    if (err != 0 || !m_resampler) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "DgSession[%u]: failed to init resampler: %s\n", 
            m_id, speex_resampler_strerror(err));
        return false;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "DgSession[%u]: resampler %uHz -> %uHz (quality=%d)\n",
        m_id, codec_rate, m_rate, quality);
    
    return true;
}

void DgSession::AudioPipeCallback(
    const char* sessionId,
    AudioPipe::NotifyEvent_t event,
    const char* message,
    bool finished
) {
    if (!sessionId) return;
    
    // Look up session in registry
    std::shared_ptr<DgSession> session;
    {
        std::lock_guard<std::mutex> lock(s_registry_mutex);
        auto it = s_registry.find(sessionId);
        if (it != s_registry.end()) {
            session = it->second.lock();
        }
    }
    
    if (session) {
        session->OnAudioPipeEvent(event, message, finished);
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "AudioPipeCallback: session %s not found in registry\n", sessionId);
    }
}

void DgSession::OnAudioPipeEvent(
    AudioPipe::NotifyEvent_t event,
    const char* message,
    bool finished
) {
    switch (event) {
        case AudioPipe::CONNECT_SUCCESS:
            m_connected.store(true, std::memory_order_release);
            // Release connect anchor - connection complete
            m_connect_anchor.reset();
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                "DgSession[%u]: connected to Deepgram\n", m_id);
            FireTranscriptEvent(TRANSCRIBE_EVENT_CONNECT_SUCCESS, nullptr, false);
            FireTranscriptEvent(TRANSCRIBE_EVENT_SESSION_START, m_metadata.c_str(), false);
            break;
            
        case AudioPipe::CONNECT_FAIL:
            m_connected.store(false, std::memory_order_release);
            m_connect_anchor.reset();
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "DgSession[%u]: connection failed: %s\n", m_id, message ? message : "unknown");
            {
                std::ostringstream json;
                json << "{\"reason\":\"" << (message ? message : "unknown") << "\"}";
                FireTranscriptEvent(TRANSCRIBE_EVENT_CONNECT_FAIL, json.str().c_str(), true);
            }
            break;
            
        case AudioPipe::CONNECTION_DROPPED:
            m_connected.store(false, std::memory_order_release);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "DgSession[%u]: connection dropped\n", m_id);
            FireTranscriptEvent(TRANSCRIBE_EVENT_DISCONNECT, nullptr, true);
            break;
            
        case AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
            m_connected.store(false, std::memory_order_release);
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                "DgSession[%u]: connection closed gracefully\n", m_id);
            break;
            
        case AudioPipe::MESSAGE:
            if (message && strlen(message) > 0) {
                FireTranscriptEvent(TRANSCRIBE_EVENT_RESULTS, message, finished);
            }
            break;
            
        default:
            break;
    }
}

void DgSession::FireTranscriptEvent(const char* eventName, const char* json, bool finished) {
    if (!m_fs_session || !m_handler) return;
    
    // Call the response handler
    m_handler(m_fs_session, eventName, json, m_bugname.c_str(), finished ? 1 : 0);
}

} // namespace deepgram
