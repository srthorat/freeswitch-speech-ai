/*
 * google_streamer_session.h - Async gRPC Streaming Session
 *
 * This class manages a single Google Speech-to-Text v2 streaming session
 * using asynchronous gRPC operations driven by the shared CompletionQueue.
 *
 * Key Features:
 * - Async state machine (no dedicated per-session threads)
 * - Pre-connection audio buffering (captures first ~1 second)
 * - Thread-safe audio queue for non-blocking writes
 * - Automatic reconnection on transient failures
 * - Clean shutdown with TryCancel
 *
 * Lifecycle:
 * 1. Create session: auto session = GoogleStreamerSession::create(...)
 * 2. Start streaming: session->start()
 * 3. Write audio: session->writeAudio(data, len)  // Non-blocking
 * 4. Stop: session->stop()  // Clean shutdown
 *
 * Thread Safety:
 * - writeAudio() can be called from any thread (FreeSWITCH audio callback)
 * - State machine callbacks run on GrpcManager worker threads
 * - Response handler is called on worker thread (should be non-blocking)
 */

#ifndef __GOOGLE_STREAMER_SESSION_H__
#define __GOOGLE_STREAMER_SESSION_H__

#include <switch.h>
#include <grpc++/grpc++.h>
#include <memory>
#include <atomic>
#include <string>
#include <functional>
#include <mutex>

#include "speech.grpc.pb.h"
#include "speech.pb.h"
#include "audio_queue.h"
#include "async_pusher_client.h"
#include "error_types.h"

namespace google_transcribe {

// Forward declarations
class GrpcManager;
class ConnectTag;
class WriteTag;
class ReadTag;
class WritesDoneTag;
class FinishTag;

/**
 * Response handler callback type
 *
 * @param session FreeSWITCH session
 * @param event_name Event type (e.g., "google_transcribev2::transcription")
 * @param json JSON payload
 * @param bugname Media bug name
 * @param is_final Whether this is a final result
 */
using ResponseHandler = std::function<void(
    switch_core_session_t* session,
    const char* event_name,
    const char* json,
    const char* bugname,
    int is_final
)>;

/**
 * Session configuration
 */
struct SessionConfig {
    std::string project_id;  // REQUIRED - no default
    std::string location_id; // REQUIRED - no default  
    std::string language_code = "en-US";
    std::string model = "long";
    uint32_t sample_rate = 16000;
    uint32_t channels = 1;
    bool interim_results = true;
    bool enable_word_confidence = true;
    bool enable_word_time_offsets = true;
    bool enable_automatic_punctuation = false;
    bool enable_profanity_filter = false;
    bool enable_speaker_diarization = false;
    int min_speaker_count = 1;
    int max_speaker_count = 6;

    // Pusher configuration (optional)
    std::string pusher_app_id;
    std::string pusher_app_key;
    std::string pusher_app_secret;
    std::string pusher_cluster = "us2";
    
    // Call metadata (for Pusher events)
    std::string sip_call_id;       // SIP Call-ID header
    std::string caller_number;     // Caller ID number
    std::string caller_name;       // Caller ID name  
    std::string callee_number;     // Callee ID number
    std::string callee_name;       // Callee ID name
    
    // Optional error handler callback
    ErrorHandler error_handler;
    
    // Validate configuration
    ErrorInfo validate() const {
        if (project_id.empty()) {
            return ErrorInfo(ErrorCode::CONFIG_MISSING_PROJECT_ID, 
                "Missing GOOGLE_PROJECT_ID",
                "Set GOOGLE_PROJECT_ID channel variable or environment variable");
        }
        if (location_id.empty()) {
            return ErrorInfo(ErrorCode::CONFIG_MISSING_LOCATION,
                "Missing GOOGLE_LOCATION_ID",
                "Set GOOGLE_LOCATION_ID channel variable or environment variable (e.g., us-central1, eu-west1)");
        }
        // Check for obviously invalid project IDs (placeholders)
        if (project_id == "your-project-id" || project_id == "my-project" || 
            project_id == "freeswitch-project" || project_id == "PROJECT_ID") {
            return ErrorInfo(ErrorCode::CONFIG_MISSING_PROJECT_ID,
                "Invalid GOOGLE_PROJECT_ID: '" + project_id + "'",
                "Replace placeholder with your actual Google Cloud project ID");
        }
        if (sample_rate != 8000 && sample_rate != 16000 && 
            sample_rate != 44100 && sample_rate != 48000) {
            return ErrorInfo(ErrorCode::CONFIG_INVALID_SAMPLE_RATE,
                "Invalid sample rate: " + std::to_string(sample_rate),
                "Supported rates: 8000, 16000, 44100, 48000");
        }
        if (channels < 1 || channels > 8) {
            return ErrorInfo(ErrorCode::CONFIG_INVALID_CHANNEL_COUNT,
                "Invalid channel count: " + std::to_string(channels),
                "Must be 1-8 channels");
        }
        return ErrorInfo();  // OK
    }
};

/**
 * GoogleStreamerSession - Async streaming transcription session
 */
class GoogleStreamerSession : public std::enable_shared_from_this<GoogleStreamerSession> {
public:
    /**
     * Session state
     */
    enum class State {
        CREATED,        // Initial state
        CONNECTING,     // gRPC connection in progress
        STREAMING,      // Actively streaming audio
        WRITES_DONE,    // WritesDone sent, waiting for final results
        FINISHING,      // Stream finishing
        FINISHED,       // Session complete
        ERROR           // Error state
    };

    /**
     * Factory method to create a session
     * Use this instead of constructor to ensure shared_ptr is created properly
     */
    static std::shared_ptr<GoogleStreamerSession> create(
        switch_core_session_t* fs_session,
        const SessionConfig& config,
        ResponseHandler response_handler,
        const std::string& bugname
    );

    ~GoogleStreamerSession();

    /**
     * Start the streaming session
     * Initiates async gRPC connection
     */
    void start();

    /**
     * Write audio data (non-blocking)
     * Safe to call from any thread.
     *
     * @param data Audio data (LINEAR16)
     * @param len Data length in bytes
     * @return true if enqueued
     */
    bool writeAudio(const void* data, size_t len);

    /**
     * Signal that no more audio will be written
     * Triggers final results from Google
     */
    void writesDone();

    /**
     * Stop the session immediately
     * Cancels any pending operations
     */
    void stop();

    /**
     * Get current state
     */
    State getState() const { return m_state.load(); }

    /**
     * Check if session is active
     */
    bool isActive() const {
        State s = m_state.load();
        return s == State::STREAMING || s == State::CONNECTING;
    }

    /**
     * Get session ID (for logging)
     */
    uint64_t getSessionId() const { return m_session_id; }

    /**
     * Get FreeSWITCH session UUID
     */
    const std::string& getUuid() const { return m_fs_uuid; }
    
    /**
     * Get last error info
     */
    const ErrorInfo& getLastError() const { return m_last_error; }
    
    /**
     * Get session error metrics
     */
    const ErrorMetrics& getMetrics() const { return m_metrics; }

    // State machine callbacks (called by GrpcManager worker threads)
    void onConnectComplete(bool ok);
    void onWriteComplete(bool ok);
    void onReadComplete(bool ok);
    void onWritesDoneComplete(bool ok);
    void onFinishComplete(bool ok);

    // Delete copy/move
    GoogleStreamerSession(const GoogleStreamerSession&) = delete;
    GoogleStreamerSession& operator=(const GoogleStreamerSession&) = delete;

private:
    // Private constructor - use create() factory method
    GoogleStreamerSession(
        switch_core_session_t* fs_session,
        const SessionConfig& config,
        ResponseHandler response_handler,
        const std::string& bugname
    );

    /**
     * Initialize gRPC request with config
     */
    void initializeRequest();

    /**
     * Process a transcription response
     */
    void processResponse(const google::cloud::speech::v2::StreamingRecognizeResponse& response);

    /**
     * Convert protobuf response to JSON and deliver
     */
    void deliverResult(
        const google::cloud::speech::v2::StreamingRecognizeResponse::StreamingRecognitionResult& result,
        const google::cloud::speech::v2::SpeechRecognitionAlternative& alternative
    );

    /**
     * Try to send next audio chunk from queue
     */
    void trySendAudio();

    /**
     * Fire a FreeSWITCH event
     */
    void fireEvent(const char* event_name, const char* json, bool is_final);

    /**
     * Send to Pusher (async)
     */
    void sendToPusher(const std::string& json, bool is_final);

    /**
     * Transition to new state
     */
    void setState(State new_state);
    
    /**
     * Set error and optionally notify handler
     */
    void setError(const ErrorInfo& error);
    
    /**
     * Set error from gRPC status
     */
    void setGrpcError(const grpc::Status& status, const std::string& context);

    // Session identity
    static std::atomic<uint64_t> s_session_counter;
    uint64_t m_session_id;
    std::string m_fs_uuid;

    // FreeSWITCH session (must check validity before use)
    switch_core_session_t* m_fs_session;

    // Configuration
    SessionConfig m_config;
    std::string m_bugname;
    ResponseHandler m_response_handler;

    // State machine
    std::atomic<State> m_state{State::CREATED};

    // gRPC components
    grpc::ClientContext m_context;
    std::unique_ptr<google::cloud::speech::v2::Speech::Stub> m_stub;
    std::unique_ptr<grpc::ClientAsyncReaderWriter<
        google::cloud::speech::v2::StreamingRecognizeRequest,
        google::cloud::speech::v2::StreamingRecognizeResponse>> m_stream;
    google::cloud::speech::v2::StreamingRecognizeRequest m_request;
    google::cloud::speech::v2::StreamingRecognizeResponse m_response;
    grpc::Status m_finish_status;

    // Async operation tags (owned by session)
    std::unique_ptr<ConnectTag> m_connect_tag;
    std::unique_ptr<WriteTag> m_write_tag;
    std::unique_ptr<ReadTag> m_read_tag;
    std::unique_ptr<WritesDoneTag> m_writes_done_tag;
    std::unique_ptr<FinishTag> m_finish_tag;

    // Audio buffering
    AudioQueue m_audio_queue;
    PreConnectBuffer m_preconnect_buffer;
    std::atomic<bool> m_write_in_progress{false};
    std::atomic<bool> m_writes_done_sent{false};

    // Synchronization
    std::mutex m_mutex;

    // Stats
    std::atomic<uint64_t> m_audio_bytes_sent{0};
    std::atomic<uint64_t> m_results_received{0};
    
    // Error tracking
    ErrorInfo m_last_error;
    ErrorMetrics m_metrics;
};

} // namespace google_transcribe

#endif // __GOOGLE_STREAMER_SESSION_H__
