#include "audio_pipe.h"

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
    AwsInternalPipe(AwsPipe* p, switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const char* lang, bool interim, const char* bugname, const ResponseHandler_t& callback) :
        m_p(p),
        m_session(session),
        m_sampleRate(sampleRate),
        m_channels(channels),
        m_lang(lang),
        m_interim(interim),
        m_bugname(bugname),
        m_callback(callback),
        m_isOpen(false),
        m_connected(false),
        m_finishing(false),
        m_connecting(false)
    {}

    ~AwsInternalPipe() {}

    // Memory pool support: reinitialize for object reuse
    void reinitialize(AwsPipe* p, switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const char* lang, bool interim, const char* bugname, const ResponseHandler_t& callback) {
        m_p = p;
        m_session = session;
        m_sampleRate = sampleRate;
        m_channels = channels;
        m_lang = lang;
        m_interim = interim;
        m_bugname = bugname;
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
    std::string m_lang;
    bool m_interim;
    std::string m_bugname;
    ResponseHandler_t m_callback;

    bool m_isOpen;
    bool m_connected;
    bool m_finishing;
    bool m_connecting;
    
    AudioStream* m_pStream;
    StartStreamTranscriptionHandler m_handler;
    TranscribeStreamingServiceClient m_client;

    deepgram::LockFreeRingBuffer<16384> m_buffer;
};



AwsPipe::AwsPipe() : m_pimpl(nullptr), m_node(nullptr) {}

AwsPipe::~AwsPipe() {}

void AwsPipe::init(switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const char* lang, bool interim, const char* bugname, const ResponseHandler_t& callback) {
    // For now, use direct allocation (memory pool integration can be added later)
    // TODO: Implement proper memory pool integration with unique_ptr compatibility
    m_pimpl.reset(new AwsInternalPipe(this, session, sampleRate, channels, lang, interim, bugname, callback));
}

void AwsPipe::connect() {
    m_pimpl->connect();
}

void AwsPipe::process_audio() {
    m_pimpl->process_audio();
}

void AwsPipe::close() {
    m_pimpl->close();
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
    m_connecting = true;
    
    // Get AWS credentials from channel variables
    switch_channel_t* channel = switch_core_session_get_channel(m_session);
    const char* awsAccessKeyId = switch_channel_get_variable(channel, "AWS_ACCESS_KEY_ID");
    const char* awsSecretAccessKey = switch_channel_get_variable(channel, "AWS_SECRET_ACCESS_KEY");
    const char* awsSessionToken = switch_channel_get_variable(channel, "AWS_SESSION_TOKEN");
    const char* region = switch_channel_get_variable(channel, "AWS_REGION");

    Aws::Client::ClientConfiguration config;
    if (region != nullptr && strlen(region) > 0) {
        config.region = region;
    }

    // Initialize client with credentials
    if (awsAccessKeyId && awsSecretAccessKey) {
        if (awsSessionToken) {
            m_client = TranscribeStreamingServiceClient(AWSCredentials(awsAccessKeyId, awsSecretAccessKey, awsSessionToken), config);
        } else {
            m_client = TranscribeStreamingServiceClient(AWSCredentials(awsAccessKeyId, awsSecretAccessKey), config);
        }
    } else {
        m_client = TranscribeStreamingServiceClient(config);
    }

    // Set up event handlers
    m_handler.SetTranscriptEventCallback([this](const TranscriptEvent& ev) {
        // Process transcription results
        for (const auto& result : ev.GetTranscript().GetResults()) {
            if (result.GetIsPartial() && !m_interim) continue;
            
            for (const auto& alt : result.GetAlternatives()) {
                transcript_data_t td = {};
                td.is_final = !result.GetIsPartial();
                td.speech_final = !result.GetIsPartial();
                td.has_transcript = true;
                td.channel_index = 0;
                td.confidence = alt.GetItems().empty() ? 0.0f : alt.GetItems()[0].GetConfidence();
                td.start_time = 0.0;
                td.end_time = 0.0;
                
                // Copy transcript to fixed-size buffer
                const auto& transcript = alt.GetTranscript();
                size_t copy_len = std::min(transcript.length(), sizeof(td.transcript) - 1);
                memcpy(td.transcript, transcript.c_str(), copy_len);
                td.transcript[copy_len] = '\0';
                
                // Call the response handler
                m_callback(m_session, &td, m_bugname.c_str());
            }
        }
    });

    m_handler.SetOnErrorCallback([this](const Aws::Client::AWSError<TranscribeStreamingServiceErrors>& error) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR, 
            "AWS Transcribe Error: %s\n", error.GetMessage().c_str());
        m_connected = false;
        m_connecting = false;
    });

    // Start the transcription stream
    StartStreamTranscriptionRequest request;
    request.SetLanguageCode(LanguageCodeMapper::GetLanguageCodeForName(m_lang.c_str()));
    request.SetMediaSampleRateHertz(m_sampleRate);
    request.SetMediaEncoding(MediaEncoding::pcm);
    request.SetEventStreamHandler(m_handler);
    if (m_channels > 1) {
        request.SetNumberOfChannels(m_channels);
    }

    // Set up stream ready handler
    auto streamReadyHandler = [this](AudioStream& stream) {
        m_pStream = &stream;
        m_connected = true;
        m_connecting = false;
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, 
            "Successfully started AWS transcription stream\n");
    };

    // Set up response received handler (for errors)
    auto responseReceivedHandler = [this](const TranscribeStreamingServiceClient* client,
                                          const StartStreamTranscriptionRequest& request,
                                          const Aws::Utils::Outcome<Aws::NoResult, TranscribeStreamingServiceError>& outcome,
                                          const std::shared_ptr<const Aws::Client::AsyncCallerContext>& context) {
        if (!outcome.IsSuccess()) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR, 
                "Failed to start AWS stream: %s\n", outcome.GetError().GetMessage().c_str());
            m_connected = false;
            m_connecting = false;
        }
    };

    m_client.StartStreamTranscriptionAsync(request, streamReadyHandler, responseReceivedHandler, nullptr);
}

void AwsInternalPipe::process_audio() {
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
    if (m_finishing) return;
    m_finishing = true;
    
    if (m_pStream) {
        try {
            m_pStream->flush();
            m_pStream->Close();
        } catch (const std::exception& e) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR, 
                "Error closing AWS stream: %s\n", e.what());
        }
        m_pStream = nullptr;
    }
    
    m_connected = false;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, 
        "AWS transcription stream closed\n");
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