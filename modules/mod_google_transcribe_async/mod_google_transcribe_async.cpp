/*
 * mod_google_transcribe_async.cpp
 * Full Async gRPC Implementation for Google Speech-to-Text V2
 * Uses enable_shared_from_this for crash-safe lifecycle management
 */

#include <switch.h>
#include <grpcpp/grpcpp.h>
#include <google/cloud/speech/v2/cloud_speech.grpc.pb.h>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <atomic>
#include <vector>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientAsyncReaderWriter;
using grpc::CompletionQueue;
using grpc::Status;
using google::cloud::speech::v2::Speech;
using google::cloud::speech::v2::StreamingRecognizeRequest;
using google::cloud::speech::v2::StreamingRecognizeResponse;

SWITCH_MODULE_LOAD_FUNCTION(mod_async_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_async_shutdown);
SWITCH_MODULE_DEFINITION(mod_google_transcribe_async, mod_async_load, mod_async_shutdown, NULL);

// --------------------------------------------------------------------------
// Global Configuration - Pointers allocated at module load
// --------------------------------------------------------------------------
struct Globals {
    char project_id[256];
    char location[64];
    char recognizer_id[256];
    
    std::shared_ptr<Channel> channel;
    std::unique_ptr<CompletionQueue> cq;
    std::vector<std::thread> workers;
    std::atomic<bool> shutdown{false};
    bool initialized{false};
};

static Globals* G = nullptr;

// --------------------------------------------------------------------------
// Tag interface for CompletionQueue
// --------------------------------------------------------------------------
struct Tag {
    virtual ~Tag() = default;
    virtual void Complete(bool ok) = 0;
};

// --------------------------------------------------------------------------
// Session - Self-anchoring async gRPC session
// --------------------------------------------------------------------------
class Session : public std::enable_shared_from_this<Session> {
public:
    static std::shared_ptr<Session> Create(switch_core_session_t* fs, uint32_t rate) {
        return std::make_shared<Session>(fs, rate);
    }
    
    Session(switch_core_session_t* fs, uint32_t r)
        : fs_session_(fs), rate_(r), uuid_(nullptr),
          connected_(false), writing_(false), stopping_(false), writes_done_sent_(false) {
        
        switch_core_session_read_lock(fs_session_);
        const char* u = switch_core_session_get_uuid(fs_session_);
        uuid_ = u ? strdup(u) : strdup("unknown");
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "ASYNC [%s]: Session created, rate=%d\n", uuid_, rate_);
    }
    
    ~Session() {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "ASYNC [%s]: Session destroyed\n", uuid_);
        if (uuid_) free(uuid_);
        if (fs_session_) switch_core_session_rwunlock(fs_session_);
    }
    
    bool Start() {
        if (!G || !G->channel || !G->cq) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "ASYNC [%s]: gRPC not initialized\n", uuid_);
            return false;
        }
        
        stub_ = Speech::NewStub(G->channel);
        if (!stub_) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "ASYNC [%s]: Failed to create stub\n", uuid_);
            return false;
        }
        
        // Create operation tags
        connect_tag_ = std::make_unique<ConnectTag>(this);
        read_tag_ = std::make_unique<ReadTag>(this);
        write_tag_ = std::make_unique<WriteTag>(this);
        finish_tag_ = std::make_unique<FinishTag>(this);
        
        // Self-anchor for connect operation
        connect_anchor_ = shared_from_this();
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "ASYNC [%s]: Starting stream...\n", uuid_);
        
        stream_ = stub_->AsyncStreamingRecognize(&ctx_, G->cq.get(), connect_tag_.get());
        return true;
    }
    
    void SendAudio(const void* data, size_t len) {
        if (!data || len == 0 || stopping_) return;
        
        std::lock_guard<std::mutex> lock(mu_);
        
        // Backpressure - drop if queue too large
        if (audio_queue_.size() > 1000) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "ASYNC [%s]: Queue full, dropping audio\n", uuid_);
            return;
        }
        
        audio_queue_.emplace(static_cast<const char*>(data),
                            static_cast<const char*>(data) + len);
        
        if (connected_ && !writing_) {
            TryWrite();
        }
    }
    
    void Stop() {
        bool expected = false;
        if (!stopping_.compare_exchange_strong(expected, true)) return;
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "ASYNC [%s]: Stopping...\n", uuid_);
        
        std::lock_guard<std::mutex> lock(mu_);
        if (connected_ && stream_ && !writing_ && !writes_done_sent_) {
            writes_done_sent_ = true;
            write_anchor_ = shared_from_this();
            stream_->WritesDone(write_tag_.get());
            writing_ = true;
        }
    }
    
private:
    // --- Tag implementations ---
    struct ConnectTag : Tag {
        Session* s;
        ConnectTag(Session* session) : s(session) {}
        void Complete(bool ok) override { s->OnConnect(ok); }
    };
    
    struct ReadTag : Tag {
        Session* s;
        ReadTag(Session* session) : s(session) {}
        void Complete(bool ok) override { s->OnRead(ok); }
    };
    
    struct WriteTag : Tag {
        Session* s;
        WriteTag(Session* session) : s(session) {}
        void Complete(bool ok) override { s->OnWrite(ok); }
    };
    
    struct FinishTag : Tag {
        Session* s;
        FinishTag(Session* session) : s(session) {}
        void Complete(bool ok) override { s->OnFinish(ok); }
    };
    
    // --- Callbacks ---
    void OnConnect(bool ok) {
        auto anchor = std::move(connect_anchor_);
        
        if (!ok) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "ASYNC [%s]: Connect FAILED\n", uuid_);
            return;
        }
        
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "ASYNC [%s]: Connected! Sending config...\n", uuid_);
        
        connected_ = true;
        
        // Build streaming config request
        StreamingRecognizeRequest config;
        std::string recognizer = std::string("projects/") + G->project_id +
                                "/locations/" + G->location +
                                "/recognizers/" + G->recognizer_id;
        config.set_recognizer(recognizer);
        
        auto* streaming_config = config.mutable_streaming_config();
        auto* recognition_config = streaming_config->mutable_config();
        
        // Explicit decoding for telephony audio
        auto* decoding = recognition_config->mutable_explicit_decoding_config();
        decoding->set_encoding(google::cloud::speech::v2::ExplicitDecodingConfig::LINEAR16);
        decoding->set_sample_rate_hertz(rate_);
        decoding->set_audio_channel_count(2);
        
        // Enable stereo separation
        recognition_config->mutable_features()->set_multi_channel_mode(
            google::cloud::speech::v2::RecognitionFeatures::SEPARATE_RECOGNITION_PER_CHANNEL);
        
        // Enable interim results
        streaming_config->mutable_streaming_features()->set_interim_results(true);
        
        // Send config and start read loop
        {
            std::lock_guard<std::mutex> lock(mu_);
            write_anchor_ = shared_from_this();
            writing_ = true;
            pending_config_ = config;
            stream_->Write(pending_config_, write_tag_.get());
        }
        
        // Start reading
        read_anchor_ = shared_from_this();
        stream_->Read(&response_, read_tag_.get());
    }
    
    void OnRead(bool ok) {
        if (!ok) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "ASYNC [%s]: Read stream ended - calling Finish to get status\n", uuid_);
            // Call Finish to get the gRPC status (error details)
            finish_anchor_ = shared_from_this();
            stream_->Finish(&finish_status_, finish_tag_.get());
            read_anchor_.reset();
            return;
        }
        
        // Only print if we have actual transcription results
        bool has_transcript = false;
        for (const auto& result : response_.results()) {
            if (result.alternatives_size() > 0 && !result.alternatives(0).transcript().empty()) {
                has_transcript = true;
                break;
            }
        }
        
        if (has_transcript) {
            // Print all results with transcripts
            for (int i = 0; i < response_.results_size(); i++) {
                const auto& result = response_.results(i);
                if (result.alternatives_size() == 0) continue;
                
                const auto& alt = result.alternatives(0);
                if (alt.transcript().empty()) continue;
                
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
                    "TRANSCRIPT [%s] Ch%d %s: \"%s\" (confidence=%.2f)\n",
                    uuid_, 
                    result.channel_tag(),
                    result.is_final() ? "[FINAL]" : "[INTERIM]",
                    alt.transcript().c_str(),
                    alt.confidence());
            }
        }
        
        // Fire FreeSWITCH events for transcripts
        for (const auto& result : response_.results()) {
            if (result.alternatives_size() == 0) continue;
            
            const auto& alt = result.alternatives(0);
            int channel = result.channel_tag();
            bool is_final = result.is_final();
            const std::string& text = alt.transcript();
            
            if (!text.empty()) {
                // Fire FreeSWITCH event
                switch_event_t* event = nullptr;
                if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, "google::transcribe") == SWITCH_STATUS_SUCCESS) {
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Transcription-UUID", uuid_);
                    switch_event_add_header(event, SWITCH_STACK_BOTTOM, "Channel-Index", "%d", channel);
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Transcription-Text", text.c_str());
                    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "Is-Final", is_final ? "true" : "false");
                    switch_event_add_header(event, SWITCH_STACK_BOTTOM, "Confidence", "%.4f", 
                        alt.confidence());
                    switch_event_fire(&event);
                }
            }
        }
        
        // Continue reading (anchor already held)
        stream_->Read(&response_, read_tag_.get());
    }
    
    void OnWrite(bool ok) {
        std::lock_guard<std::mutex> lock(mu_);
        writing_ = false;
        auto anchor = std::move(write_anchor_);
        
        if (!ok) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "ASYNC [%s]: Write failed (stream closed)\n", uuid_);
            return;
        }
        
        // Only log write stats periodically (every ~100 writes = 2 seconds)
        write_count_++;
        if (write_count_ % 100 == 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                "ASYNC [%s]: Streaming... (%d writes, queue=%zu)\n", uuid_, write_count_, audio_queue_.size());
        }
        
        // If stopping and queue empty, send WritesDone
        if (stopping_ && audio_queue_.empty() && !writes_done_sent_) {
            writes_done_sent_ = true;
            write_anchor_ = shared_from_this();
            stream_->WritesDone(write_tag_.get());
            writing_ = true;
            return;
        }
        
        TryWrite();
    }
    
    void OnFinish(bool ok) {
        auto anchor = std::move(finish_anchor_);
        
        if (finish_status_.ok()) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
                "ASYNC [%s]: Stream finished OK\n", uuid_);
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "ASYNC [%s]: gRPC ERROR: code=%d message=%s details=%s\n", 
                uuid_,
                static_cast<int>(finish_status_.error_code()),
                finish_status_.error_message().c_str(),
                finish_status_.error_details().c_str());
        }
    }
    
    void TryWrite() {
        // Must hold mu_
        if (audio_queue_.empty() || writing_ || !connected_ || writes_done_sent_) return;
        
        pending_audio_.Clear();
        pending_audio_.set_audio(audio_queue_.front().data(), audio_queue_.front().size());
        audio_queue_.pop();
        
        write_anchor_ = shared_from_this();
        writing_ = true;
        stream_->Write(pending_audio_, write_tag_.get());
    }
    
    // --- Data ---
    switch_core_session_t* fs_session_;
    char* uuid_;
    uint32_t rate_;
    
    std::unique_ptr<Speech::Stub> stub_;
    ClientContext ctx_;
    std::unique_ptr<ClientAsyncReaderWriter<StreamingRecognizeRequest, StreamingRecognizeResponse>> stream_;
    StreamingRecognizeResponse response_;
    
    // Pending requests (must stay alive during async ops)
    StreamingRecognizeRequest pending_config_;
    StreamingRecognizeRequest pending_audio_;
    
    // Tags
    std::unique_ptr<Tag> connect_tag_;
    std::unique_ptr<Tag> read_tag_;
    std::unique_ptr<Tag> write_tag_;
    std::unique_ptr<Tag> finish_tag_;
    
    // Self-anchors
    std::shared_ptr<Session> connect_anchor_;
    std::shared_ptr<Session> read_anchor_;
    std::shared_ptr<Session> write_anchor_;
    std::shared_ptr<Session> finish_anchor_;
    
    // gRPC status for Finish()
    grpc::Status finish_status_;
    
    // State
    std::mutex mu_;
    std::queue<std::vector<char>> audio_queue_;
    std::atomic<bool> connected_;
    std::atomic<bool> writing_;
    std::atomic<bool> stopping_;
    bool writes_done_sent_;
    int write_count_ = 0;
};

// --------------------------------------------------------------------------
// Media Bug
// --------------------------------------------------------------------------
struct BugData {
    std::shared_ptr<Session> session;
};

static switch_bool_t bug_callback(switch_media_bug_t* bug, void* user_data, switch_abc_type_t type) {
    auto* data = static_cast<BugData*>(user_data);
    if (!data) return SWITCH_FALSE;
    
    switch (type) {
    case SWITCH_ABC_TYPE_INIT:
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "ASYNC: Bug INIT\n");
        break;
        
    case SWITCH_ABC_TYPE_CLOSE:
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "ASYNC: Bug CLOSE\n");
        if (data->session) data->session->Stop();
        delete data;
        break;
        
    case SWITCH_ABC_TYPE_READ_PING: {
        uint8_t buf[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = {0};
        frame.data = buf;
        frame.buflen = sizeof(buf);
        
        if (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
            if (frame.datalen > 0 && data->session) {
                data->session->SendAudio(frame.data, frame.datalen);
            }
        }
        break;
    }
    default:
        break;
    }
    
    return SWITCH_TRUE;
}

// --------------------------------------------------------------------------
// gRPC Initialization
// --------------------------------------------------------------------------
static bool init_grpc() {
    if (G->initialized) return true;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "ASYNC: Initializing gRPC - Project=%s Location=%s Recognizer=%s\n",
        G->project_id, G->location, G->recognizer_id);
    
    try {
        auto creds = grpc::GoogleDefaultCredentials();
        if (!creds) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "ASYNC: Failed to get credentials\n");
            return false;
        }
        
        // For "global" location, use speech.googleapis.com directly
        // For regional locations (e.g., us-central1), use {location}-speech.googleapis.com
        std::string endpoint;
        if (strcmp(G->location, "global") == 0) {
            endpoint = "speech.googleapis.com";
        } else {
            endpoint = std::string(G->location) + "-speech.googleapis.com";
        }
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Endpoint: %s\n", endpoint.c_str());
        
        G->channel = grpc::CreateChannel(endpoint, creds);
        G->cq = std::make_unique<CompletionQueue>();
        
        // Start worker threads
        // Scaling guide:
        //   - 100 calls:  4 workers  (low load)
        //   - 500 calls:  8 workers  (medium load)
        //   - 1000 calls: 16 workers (high load)
        //   - 2000+ calls: 32 workers (very high load)
        // Each call generates ~55 events/sec (50 writes + 5 reads)
        // Each worker can handle ~3000-5000 events/sec
        // Formula: workers = (expected_calls * 55) / 3000
        //
        // Can be overridden via environment variable ASYNC_GRPC_WORKERS
        int num_workers = 16;  // Default: supports ~1000 concurrent calls
        const char* env_workers = getenv("ASYNC_GRPC_WORKERS");
        if (env_workers && strlen(env_workers) > 0) {
            int env_val = atoi(env_workers);
            if (env_val >= 1 && env_val <= 64) {
                num_workers = env_val;
            }
        }
        
        for (int i = 0; i < num_workers; i++) {
            G->workers.emplace_back([i]() {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Worker %d started\n", i);
                void* tag;
                bool ok;
                while (G->cq->Next(&tag, &ok)) {
                    if (G->shutdown) break;
                    if (tag) {
                        try {
                            static_cast<Tag*>(tag)->Complete(ok);
                        } catch (const std::exception& e) {
                            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                                "ASYNC: Worker %d exception: %s\n", i, e.what());
                        }
                    }
                }
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Worker %d exiting\n", i);
            });
        }
        
        G->initialized = true;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: gRPC initialized with %d workers\n", num_workers);
        return true;
        
    } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "ASYNC: Init failed: %s\n", e.what());
        return false;
    }
}

// --------------------------------------------------------------------------
// Config Loading
// --------------------------------------------------------------------------
static void load_config() {
    // Defaults
    strcpy(G->project_id, "default");
    strcpy(G->location, "us-central1");
    strcpy(G->recognizer_id, "telephony");
    
    // Environment variables
    const char* env;
    if ((env = getenv("GCP_PROJECT_ID")) && strlen(env) > 0)
        strncpy(G->project_id, env, sizeof(G->project_id) - 1);
    if ((env = getenv("GCP_LOCATION")) && strlen(env) > 0)
        strncpy(G->location, env, sizeof(G->location) - 1);
    if ((env = getenv("GCP_RECOGNIZER_ID")) && strlen(env) > 0)
        strncpy(G->recognizer_id, env, sizeof(G->recognizer_id) - 1);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "ASYNC: Config loaded - Project=%s Location=%s Recognizer=%s\n",
        G->project_id, G->location, G->recognizer_id);
}

// --------------------------------------------------------------------------
// API Function
// --------------------------------------------------------------------------
#define API_SYNTAX "<uuid> <start|stop> [rate]"

SWITCH_STANDARD_API(uuid_google_async_function) {
    char* mycmd = nullptr;
    char* argv[4] = {0};
    int argc = 0;
    
    if (!zstr(cmd)) {
        mycmd = strdup(cmd);
        argc = switch_separate_string(mycmd, ' ', argv, 4);
    }
    
    if (argc < 2) {
        stream->write_function(stream, "-ERR Usage: uuid_google_async %s\n", API_SYNTAX);
        switch_safe_free(mycmd);
        return SWITCH_STATUS_SUCCESS;
    }
    
    const char* uuid = argv[0];
    const char* action = argv[1];
    uint32_t rate = (argc > 2) ? atoi(argv[2]) : 8000;
    if (rate != 8000 && rate != 16000) rate = 8000;
    
    switch_core_session_t* fs_session = switch_core_session_locate(uuid);
    if (!fs_session) {
        stream->write_function(stream, "-ERR Session not found: %s\n", uuid);
        switch_safe_free(mycmd);
        return SWITCH_STATUS_SUCCESS;
    }
    
    if (!strcasecmp(action, "start")) {
        // Initialize gRPC if needed
        if (!init_grpc()) {
            stream->write_function(stream, "-ERR gRPC initialization failed\n");
            switch_core_session_rwunlock(fs_session);
            switch_safe_free(mycmd);
            return SWITCH_STATUS_SUCCESS;
        }
        
        // Remove any existing bug
        switch_core_media_bug_remove_callback(fs_session, bug_callback);
        
        // Create session
        auto sess = Session::Create(fs_session, rate);
        auto* bug_data = new BugData{sess};
        
        switch_media_bug_t* bug = nullptr;
        switch_media_bug_flag_t flags = (switch_media_bug_flag_t)(
            SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_READ_PING | SMBF_STEREO);
        
        if (switch_core_media_bug_add(fs_session, "google_async", nullptr, bug_callback, bug_data, 0, flags, &bug) == SWITCH_STATUS_SUCCESS) {
            switch_mm_t mm = {0};
            mm.samplerate = rate;
            mm.channels = 2;
            switch_core_media_bug_set_media_params(bug, &mm);
            
            if (sess->Start()) {
                stream->write_function(stream, "+OK Started transcription at %dHz stereo\n", rate);
            } else {
                switch_core_media_bug_remove(fs_session, &bug);
                stream->write_function(stream, "-ERR Failed to start gRPC stream\n");
            }
        } else {
            delete bug_data;
            stream->write_function(stream, "-ERR Failed to add media bug\n");
        }
        
    } else if (!strcasecmp(action, "stop")) {
        if (switch_core_media_bug_remove_callback(fs_session, bug_callback)) {
            stream->write_function(stream, "+OK Stopped\n");
        } else {
            stream->write_function(stream, "-ERR Not running\n");
        }
        
    } else {
        stream->write_function(stream, "-ERR Unknown action: %s\n", action);
    }
    
    switch_core_session_rwunlock(fs_session);
    switch_safe_free(mycmd);
    return SWITCH_STATUS_SUCCESS;
}

// --------------------------------------------------------------------------
// Module Load/Shutdown
// --------------------------------------------------------------------------
SWITCH_MODULE_LOAD_FUNCTION(mod_async_load) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Loading module...\n");
    
    G = new Globals();
    load_config();
    
    *module_interface = switch_loadable_module_create_module_interface(pool, modname);
    
    switch_api_interface_t* api;
    SWITCH_ADD_API(api, "uuid_google_async", "Google Speech V2 Async Transcription", 
                   uuid_google_async_function, API_SYNTAX);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Module loaded successfully\n");
    return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_async_shutdown) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Shutting down...\n");
    
    if (G) {
        G->shutdown = true;
        
        if (G->cq) {
            G->cq->Shutdown();
        }
        
        for (auto& t : G->workers) {
            if (t.joinable()) t.join();
        }
        
        G->channel.reset();
        G->cq.reset();
        G->workers.clear();
        
        delete G;
        G = nullptr;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "ASYNC: Shutdown complete\n");
    return SWITCH_STATUS_SUCCESS;
}
