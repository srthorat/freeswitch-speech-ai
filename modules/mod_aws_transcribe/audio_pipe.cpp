#include "audio_pipe.h"

#include <cstdlib>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/transcribestreaming/TranscribeStreamingServiceClient.h>
#include <aws/transcribestreaming/model/StartStreamTranscriptionHandler.h>
#include <aws/transcribestreaming/model/StartStreamTranscriptionRequest.h>
#include <aws/transcribestreaming/model/AudioEvent.h>
#include <aws/transcribestreaming/model/TranscriptEvent.h>

#include "mod_aws_transcribe.h"
#include "lockfree_ring_buffer.hpp"
#include "worker_thread.h"
#include "aws_client_manager.h"
#include "memory_pool.hpp"

using namespace Aws;
using namespace Aws::Auth;
using namespace Aws::TranscribeStreamingService;
using namespace Aws::TranscribeStreamingService::Model;

// Memory pools for high-scale performance
// deepgram::ObjectPool<AwsPipe> g_pipe_pool; // TODO: Fix namespace
// deepgram::ObjectPool<AwsInternalPipe> g_internal_pipe_pool; // TODO: Fix namespace

class AwsInternalPipe {
public:
    AwsInternalPipe(AwsPipe* p, switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const AwsTranscribeOptions& options, const ResponseHandler_t& callback) :
        m_p(p),
        m_session(session),
        m_sampleRate(sampleRate),
        m_channels(channels),
        m_options(options),
        m_callback(callback),
        m_isOpen(false),
        m_connected(false),
        m_finishing(false),
        m_connecting(false)
    {
        if (m_session) switch_core_session_read_lock(m_session);
    }

    ~AwsInternalPipe() {
        if (m_session) switch_core_session_rwunlock(m_session);
    }

    // Memory pool support: reinitialize for object reuse
    void reinitialize(AwsPipe* p, switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const AwsTranscribeOptions& options, const ResponseHandler_t& callback) {
        m_p = p;
        m_session = session;
        m_sampleRate = sampleRate;
        m_channels = channels;
        m_options = options;
        m_callback = callback;
        m_isOpen = false;
        m_connected = false;
        m_finishing = false;
        m_connecting = false;
        // Ring buffer will be automatically reset on first use
    }

    void connect();
    void process_audio();
    void close();
    bool reserveAudioSpace(size_t size, void** ptr1, size_t* len1, void** ptr2, size_t* len2);
    void commitAudioData(size_t size);
    uint32_t getId() const;
    uint32_t getSampleRate() const;
    uint32_t getChannels() const;
    bool isConnecting();

private:
    AwsPipe* m_p;
    switch_core_session_t* m_session;
    uint32_t m_sampleRate;
    uint32_t m_channels;
    AwsTranscribeOptions m_options;
    ResponseHandler_t m_callback;

    bool m_isOpen;
    bool m_connected;
    bool m_finishing;
    bool m_connecting;
    
    AudioStream* m_pStream;
    // CRITICAL: Heap-allocate AWS SDK objects with shared_ptr to prevent use-after-free
    // AWS SDK async operations run in background threads that may outlive this object
    std::shared_ptr<StartStreamTranscriptionHandler> m_handler;
    std::shared_ptr<TranscribeStreamingServiceClient> m_client;
    // CRITICAL: Keep request alive for the duration of the stream
    std::shared_ptr<StartStreamTranscriptionRequest> m_request;

    aws::LockFreeRingBuffer<16384> m_buffer;
    std::mutex m_mutex;
};



AwsPipe::AwsPipe() : m_pimpl(nullptr), m_node(nullptr) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "AwsPipe::AwsPipe() - Object created at %p\n", this);
}

AwsPipe::~AwsPipe() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "AwsPipe::~AwsPipe() - Object destroyed at %p\n", this);
}

void AwsPipe::init(switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const AwsTranscribeOptions& options, const ResponseHandler_t& callback) {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
        "AwsPipe::init() - Initializing pipe at %p: rate=%u, channels=%u, lang=%s, interim=%d\n",
        this, sampleRate, channels, options.lang.c_str(), options.interim);
    // For now, use direct allocation (memory pool integration can be added later)
    // TODO: Implement proper memory pool integration with unique_ptr compatibility
    m_pimpl.reset(new AwsInternalPipe(this, session, sampleRate, channels, options, callback));
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
        "AwsPipe::init() - Internal pipe created at %p\n", m_pimpl.get());
}

void AwsPipe::connect() {
    if (m_pimpl) {
        m_pimpl->connect();
    }
}

void AwsPipe::process_audio() {
    if (m_pimpl) {
        m_pimpl->process_audio();
    }
}

void AwsPipe::close() {
    if (m_pimpl) {
        switch_time_t now = switch_time_now();
        m_close_timestamp.store(now, std::memory_order_relaxed);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "AwsPipe::close() - Pipe %p marked for closure at timestamp %lld\n", this, (long long)now);
        m_pimpl->close();
    }
}

bool AwsPipe::should_destroy() const {
    int64_t closed_at = m_close_timestamp.load(std::memory_order_relaxed);
    if (closed_at == 0) return false; // Not closed yet
    int64_t elapsed = switch_time_now() - closed_at;
    bool should_destroy = elapsed > 5000000; // 5 seconds in microseconds
    if (should_destroy) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "AwsPipe::should_destroy() - Pipe %p ready for destruction (closed %lld microseconds ago)\n",
            this, (long long)elapsed);
    }
    return should_destroy;
}

bool AwsPipe::reserveAudioSpace(size_t size, void** ptr1, size_t* len1, void** ptr2, size_t* len2) {
    if (m_pimpl) {
        return m_pimpl->reserveAudioSpace(size, ptr1, len1, ptr2, len2);
    }
    return false;
}

void AwsPipe::commitAudioData(size_t size) {
    if (m_pimpl) {
        m_pimpl->commitAudioData(size);
    }
}

uint32_t AwsPipe::getId() const {
    if (m_pimpl) {
        return m_pimpl->getId();
    }
    return 0;
}

uint32_t AwsPipe::getSampleRate() const {
    if (m_pimpl) {
        return m_pimpl->getSampleRate();
    }
    return 0;
}

uint32_t AwsPipe::getChannels() const {
    if (m_pimpl) {
        return m_pimpl->getChannels();
    }
    return 0;
}

// Implementation of AwsInternalPipe methods
void AwsInternalPipe::connect() {
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO,
        "AwsInternalPipe::connect() - Starting AWS connection for session\n");
    m_connecting = true;
    
    // Get AWS credentials from environment variables (systemd) or channel variables (dialplan)
    switch_channel_t* channel = switch_core_session_get_channel(m_session);
    
    // Try environment variables first (set via systemd), then fall back to channel variables
    const char* awsAccessKeyId = std::getenv("AWS_ACCESS_KEY_ID");
    if (!awsAccessKeyId) {
        awsAccessKeyId = switch_channel_get_variable(channel, "AWS_ACCESS_KEY_ID");
    }
    
    const char* awsSecretAccessKey = std::getenv("AWS_SECRET_ACCESS_KEY");
    if (!awsSecretAccessKey) {
        awsSecretAccessKey = switch_channel_get_variable(channel, "AWS_SECRET_ACCESS_KEY");
    }
    
    const char* awsSessionToken = std::getenv("AWS_SESSION_TOKEN");
    if (!awsSessionToken) {
        awsSessionToken = switch_channel_get_variable(channel, "AWS_SESSION_TOKEN");
    }
    
    const char* region = std::getenv("AWS_DEFAULT_REGION");
    if (!region) {
        region = std::getenv("AWS_REGION");
    }
    if (!region) {
        region = switch_channel_get_variable(channel, "AWS_REGION");
    }

    // CRITICAL: AWS SDK requires credentials - fail fast if missing
    if (!awsAccessKeyId || !awsSecretAccessKey) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR, 
            "AWS credentials not found - set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY in environment or channel variables\n");
        m_connecting = false;
        return;
    }
    
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
        "AWS credentials loaded: AccessKeyId=%s, Region=%s, HasSessionToken=%s\n",
        awsAccessKeyId ? "[PRESENT]" : "[MISSING]",
        region ? region : "[DEFAULT]",
        awsSessionToken ? "YES" : "NO");

    Aws::Client::ClientConfiguration config;
    if (region != nullptr && strlen(region) > 0) {
        config.region = region;
    }

    // Initialize handler BEFORE client (so we can log both addresses)
    m_handler = std::make_shared<StartStreamTranscriptionHandler>();

    // Initialize client with credentials
    if (awsAccessKeyId && awsSecretAccessKey) {
        if (awsSessionToken) {
            m_client = std::make_shared<TranscribeStreamingServiceClient>(AWSCredentials(awsAccessKeyId, awsSecretAccessKey, awsSessionToken), config);
        } else {
            m_client = std::make_shared<TranscribeStreamingServiceClient>(AWSCredentials(awsAccessKeyId, awsSecretAccessKey), config);
        }
    } else {
        m_client = std::make_shared<TranscribeStreamingServiceClient>(config);
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
        "AWS client created at %p, handler created at %p\n", m_client.get(), m_handler.get());

    // CRITICAL: Capture pointer to parent AwsPipe (not this!) for stream handlers
    // These handlers run in AWS SDK threads and may execute after AwsInternalPipe is destroyed
    auto parentPipe = m_p->shared_from_this();
    std::weak_ptr<AwsPipe> weakParent = parentPipe;

    auto callback = m_callback;
    auto bugname = m_options.bugname;
    bool interim = m_options.interim;

    // Set up event handlers with safe captures (no `this` pointer!)
    m_handler->SetTranscriptEventCallback([weakParent, callback, bugname, interim](const TranscriptEvent& ev) {
        auto parent = weakParent.lock();
        if (!parent || !parent->m_pimpl) return;
        auto session = parent->m_pimpl->m_session;

        // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
        //    "AWS Transcript callback invoked - Processing results\n");
        // Process transcription results
        for (const auto& result : ev.GetTranscript().GetResults()) {
            if (result.GetIsPartial() && !interim) continue;
            
            for (const auto& alt : result.GetAlternatives()) {
                transcript_data_t td = {};
                td.is_final = !result.GetIsPartial();
                td.speech_final = !result.GetIsPartial();
                td.has_transcript = true;
                
                // Map AWS ChannelId to channel_index
                if (result.ChannelIdHasBeenSet()) {
                    const auto& chId = result.GetChannelId();
                    if (chId == "ch_0") td.channel_index = 0;
                    else if (chId == "ch_1") td.channel_index = 1;
                    else td.channel_index = 0;
                } else {
                    td.channel_index = 0;
                }

                td.confidence = alt.GetItems().empty() ? 0.0f : alt.GetItems()[0].GetConfidence();
                td.start_time = result.GetStartTime();
                td.end_time = result.GetEndTime();
                
                // Copy transcript to fixed-size buffer
                const auto& transcript = alt.GetTranscript();
                size_t copy_len = std::min(transcript.length(), sizeof(td.transcript) - 1);
                memcpy(td.transcript, transcript.c_str(), copy_len);
                td.transcript[copy_len] = '\0';
                
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                    "AWS Transcript: is_final=%d, text='%s', channel=%d\n", td.is_final, td.transcript, td.channel_index);
                
                // Call the response handler (session is still valid - managed by FreeSWITCH)
                callback(session, &td, bugname.c_str());
            }
        }
    });

    m_handler->SetOnErrorCallback([weakParent](const Aws::Client::AWSError<TranscribeStreamingServiceErrors>& error) {
        auto parent = weakParent.lock();
        if (!parent || !parent->m_pimpl) return;
        auto session = parent->m_pimpl->m_session;

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
            "AWS Transcribe Error callback invoked: %s (error code: %d)\n",
            error.GetMessage().c_str(), static_cast<int>(error.GetErrorType()));
    });
    
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
        "Event handlers registered for AWS handler %p\n", m_handler.get());

    // Start the transcription stream
    m_request = std::make_shared<StartStreamTranscriptionRequest>();
    m_request->SetLanguageCode(LanguageCodeMapper::GetLanguageCodeForName(m_options.lang.c_str()));
    m_request->SetMediaSampleRateHertz(m_sampleRate);
    m_request->SetMediaEncoding(MediaEncoding::pcm);
    m_request->SetEventStreamHandler(*m_handler);

    if (!m_options.vocabularyName.empty()) {
        m_request->SetVocabularyName(m_options.vocabularyName.c_str());
    }
    if (!m_options.vocabularyFilterName.empty()) {
        m_request->SetVocabularyFilterName(m_options.vocabularyFilterName.c_str());
    }
    if (!m_options.vocabularyFilterMethod.empty()) {
        m_request->SetVocabularyFilterMethod(VocabularyFilterMethodMapper::GetVocabularyFilterMethodForName(m_options.vocabularyFilterMethod.c_str()));
    }
    if (!m_options.sessionId.empty()) {
        m_request->SetSessionId(m_options.sessionId.c_str());
    }
    if (m_options.showSpeakerLabel) {
        m_request->SetShowSpeakerLabel(true);
    }
    
    // Channel Identification Logic
    if (m_options.enableChannelIdentification) {
        m_request->SetEnableChannelIdentification(true);
        m_request->SetNumberOfChannels(m_options.numberOfChannels);
    } else if (m_options.numberOfChannels > 1) {
        // If stereo is requested but identification not explicitly set, enable it by default
        m_request->SetEnableChannelIdentification(true);
        m_request->SetNumberOfChannels(m_options.numberOfChannels);
    }
    
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO,
        "Starting AWS transcription stream: lang=%s, rate=%u, channels=%u\n",
        m_options.lang.c_str(), m_sampleRate, m_channels);

    // Set up stream ready handler
    auto streamReadyHandler = [weakParent, callback, bugname](AudioStream& stream) {
        auto parent = weakParent.lock();
        if (parent && parent->m_pimpl) {
            std::lock_guard<std::mutex> lock(parent->m_pimpl->m_mutex);
            parent->m_pimpl->m_pStream = &stream;
            parent->m_pimpl->m_connected = true;
            parent->m_pimpl->m_connecting = false;
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(parent->m_pimpl->m_session), SWITCH_LOG_INFO, 
                "Successfully started AWS transcription stream (m_pStream=%p, m_connected=true)\n", &stream);
            
            // Fire connection success event (triggers session_start in Pusher)
            transcript_data_t td = {};
            td.is_final = false;
            td.speech_final = false;
            td.has_transcript = false;
            td.is_connection_event = true; // Special flag for connection events
            strncpy(td.transcript, "connection_success", sizeof(td.transcript) - 1);
            callback(parent->m_pimpl->m_session, &td, bugname.c_str());
        }
    };

    // Set up response received handler (for errors and completion)
    auto responseReceivedHandler = [weakParent](
        const TranscribeStreamingServiceClient* client,
        const StartStreamTranscriptionRequest& request,
        const Aws::Utils::Outcome<Aws::NoResult, TranscribeStreamingServiceError>& outcome,
        const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context) {
        
        auto parent = weakParent.lock();
        if (!parent || !parent->m_pimpl) return;
        
        std::lock_guard<std::mutex> lock(parent->m_pimpl->m_mutex);
        
        if (!outcome.IsSuccess()) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(parent->m_pimpl->m_session), SWITCH_LOG_ERROR, 
                "AWS stream failed: %s\n", outcome.GetError().GetMessage().c_str());
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(parent->m_pimpl->m_session), SWITCH_LOG_DEBUG,
                "AWS stream completed successfully\n");
        }
        
        // CRITICAL: Always clear the stream pointer when the request completes
        // The stream object is about to be destroyed by the SDK
        parent->m_pimpl->m_connected = false;
        parent->m_pimpl->m_connecting = false;
        parent->m_pimpl->m_pStream = nullptr;
        // We can also release the request here if we want, but it's safe to keep it until destruction
    };

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
        "Calling m_client->StartStreamTranscriptionAsync() with client=%p, handler=%p\n",
        m_client.get(), m_handler.get());
    
    m_client->StartStreamTranscriptionAsync(*m_request, streamReadyHandler, responseReceivedHandler, nullptr);
    
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
        "StartStreamTranscriptionAsync() call completed (async operation started)\n");
}

void AwsInternalPipe::process_audio() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_connected || m_finishing || !m_pStream) return;

    // Read available audio from the ring buffer and send to AWS
    auto [ptr, len] = m_buffer.peek_contiguous();
    if (len > 0) {
        Aws::Vector<unsigned char> audio_data(static_cast<const unsigned char*>(ptr), 
                                             static_cast<const unsigned char*>(ptr) + len);
        AudioEvent audio_event(std::move(audio_data));
        
        try {
            m_pStream->WriteAudioEvent(audio_event);
            m_buffer.consume(len);
        } catch (const std::exception& e) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR, 
                "Error writing audio to AWS stream: %s\n", e.what());
        }
    }
}

void AwsInternalPipe::close() {
    AudioStream* streamToClose = nullptr;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_finishing) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
                "AwsInternalPipe::close() - Already closing, ignoring duplicate call\n");
            return;
        }
        m_finishing = true;
        
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO,
            "AwsInternalPipe::close() - Starting close sequence (m_pStream=%p, m_connected=%d)\n",
            m_pStream, m_connected);
        
        if (m_pStream) {
            streamToClose = m_pStream;
            m_pStream = nullptr; // Clear it so others don't use it
        }
        
        m_connected = false;
    } // Release lock to avoid deadlock with AWS callbacks

    if (streamToClose) {
        try {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
                "Flushing and closing AWS stream\n");
            streamToClose->flush();
            streamToClose->Close();
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
                "AWS stream flush and close completed\n");
        } catch (const std::exception& e) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR, 
                "Error closing AWS stream: %s\n", e.what());
        }
    }
    
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, 
        "AWS transcription stream closed (m_client refcount=%ld, m_handler refcount=%ld)\n",
        m_client.use_count(), m_handler.use_count());
}

bool AwsInternalPipe::reserveAudioSpace(size_t size, void** ptr1, size_t* len1, void** ptr2, size_t* len2) {
    return m_buffer.reserve_write(size, (uint8_t**)ptr1, len1, (uint8_t**)ptr2, len2);
}

void AwsInternalPipe::commitAudioData(size_t size) {
    m_buffer.commit_write(size);
}

uint32_t AwsInternalPipe::getId() const {
    return 0; // Not used in current implementation
}

uint32_t AwsInternalPipe::getSampleRate() const {
    return m_sampleRate;
}

uint32_t AwsInternalPipe::getChannels() const {
    return m_channels;
}

bool AwsInternalPipe::isConnecting() {
    return m_connecting;
}