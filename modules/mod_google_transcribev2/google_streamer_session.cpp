/*
 * google_streamer_session.cpp - Implementation of GoogleStreamerSession
 *
 * Async gRPC streaming for Google Speech-to-Text v2 API.
 */

#include "google_streamer_session.h"
#include "grpc_manager.h"
#include <switch_json.h>
#include <google/protobuf/util/json_util.h>
#include <sstream>

using google::cloud::speech::v2::RecognitionConfig;
using google::cloud::speech::v2::Speech;
using google::cloud::speech::v2::StreamingRecognizeRequest;
using google::cloud::speech::v2::StreamingRecognizeResponse;
using google::cloud::speech::v2::StreamingRecognitionConfig;
using google::cloud::speech::v2::RecognitionFeatures;
using google::cloud::speech::v2::SpeakerDiarizationConfig;
using google::cloud::speech::v2::ExplicitDecodingConfig;

namespace google_transcribe {

// Static session counter for unique IDs
std::atomic<uint64_t> GoogleStreamerSession::s_session_counter{0};

std::shared_ptr<GoogleStreamerSession> GoogleStreamerSession::create(
    switch_core_session_t* fs_session,
    const SessionConfig& config,
    ResponseHandler response_handler,
    const std::string& bugname)
{
    // Can't use make_shared with private constructor
    auto session = std::shared_ptr<GoogleStreamerSession>(
        new GoogleStreamerSession(fs_session, config, response_handler, bugname)
    );

    // Register with GrpcManager
    GrpcManager::getInstance().registerSession(session->m_session_id, session);

    return session;
}

GoogleStreamerSession::GoogleStreamerSession(
    switch_core_session_t* fs_session,
    const SessionConfig& config,
    ResponseHandler response_handler,
    const std::string& bugname)
    : m_session_id(++s_session_counter)
    , m_fs_session(fs_session)
    , m_config(config)
    , m_bugname(bugname)
    , m_response_handler(response_handler)
    , m_audio_queue(256, AudioQueue::OverflowPolicy::DROP_OLDEST)
    , m_preconnect_buffer(32000)  // ~1 second at 16kHz
{
    if (fs_session) {
        m_fs_uuid = switch_core_session_get_uuid(fs_session);
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "GoogleStreamerSession %lu created for %s\n",
        m_session_id, m_fs_uuid.c_str());
}

GoogleStreamerSession::~GoogleStreamerSession() {
    // Ensure we're stopped
    if (m_state.load() != State::FINISHED && m_state.load() != State::ERROR) {
        stop();
    }

    // Unregister from GrpcManager
    GrpcManager::getInstance().unregisterSession(m_session_id);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "GoogleStreamerSession %lu destroyed, sent %lu bytes, received %lu results\n",
        m_session_id, m_audio_bytes_sent.load(), m_results_received.load());
}

void GoogleStreamerSession::initializeRequest() {
    // Build recognizer path
    std::string recognizer = "projects/" + m_config.project_id +
                             "/locations/" + m_config.location_id +
                             "/recognizers/_";
    m_request.set_recognizer(recognizer);

    // Configure streaming
    auto* streaming_config = m_request.mutable_streaming_config();
    auto* config = streaming_config->mutable_config();

    // Explicit decoding config
    auto* explicit_config = config->mutable_explicit_decoding_config();
    explicit_config->set_encoding(ExplicitDecodingConfig::LINEAR16);
    explicit_config->set_sample_rate_hertz(m_config.sample_rate);
    explicit_config->set_audio_channel_count(m_config.channels);

    // Language
    config->add_language_codes(m_config.language_code);

    // Model
    config->set_model(m_config.model);

    // Features
    auto* features = config->mutable_features();
    features->set_enable_word_confidence(m_config.enable_word_confidence);
    features->set_enable_word_time_offsets(m_config.enable_word_time_offsets);

    if (m_config.enable_automatic_punctuation) {
        features->set_enable_automatic_punctuation(true);
    }

    if (m_config.enable_profanity_filter) {
        features->set_profanity_filter(true);
    }

    // Multi-channel mode for stereo
    if (m_config.channels > 1) {
        features->set_multi_channel_mode(
            RecognitionFeatures::SEPARATE_RECOGNITION_PER_CHANNEL);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "Session %lu: Multi-channel mode enabled (%u channels)\n",
            m_session_id, m_config.channels);
    }

    // Speaker diarization
    if (m_config.enable_speaker_diarization) {
        auto* diarization = features->mutable_diarization_config();
        diarization->set_min_speaker_count(m_config.min_speaker_count);
        diarization->set_max_speaker_count(m_config.max_speaker_count);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "Session %lu: Speaker diarization enabled (min=%d, max=%d)\n",
            m_session_id, m_config.min_speaker_count, m_config.max_speaker_count);
    }

    // Enable voice activity events for interim results
    streaming_config->set_enable_voice_activity_events(true);
}

void GoogleStreamerSession::start() {
    if (m_state.load() != State::CREATED) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "Session %lu: Cannot start, state=%d\n",
            m_session_id, static_cast<int>(m_state.load()));
        return;
    }

    setState(State::CONNECTING);

    // Initialize request config
    initializeRequest();

    // Create gRPC channel and stub
    auto creds = grpc::GoogleDefaultCredentials();
    auto channel = grpc::CreateChannel("speech.googleapis.com", creds);
    m_stub = Speech::NewStub(channel);

    // Set deadline for the stream (5 minutes max)
    m_context.set_deadline(
        std::chrono::system_clock::now() + std::chrono::minutes(5));

    // Create async tags
    m_connect_tag = std::make_unique<ConnectTag>(shared_from_this());
    m_write_tag = std::make_unique<WriteTag>(shared_from_this());
    m_read_tag = std::make_unique<ReadTag>(shared_from_this());
    m_writes_done_tag = std::make_unique<WritesDoneTag>(shared_from_this());
    m_finish_tag = std::make_unique<FinishTag>(shared_from_this());

    // Start async connection
    m_stream = m_stub->AsyncStreamingRecognize(
        &m_context,
        GrpcManager::getInstance().getCompletionQueue(),
        m_connect_tag.get()
    );

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "Session %lu: Connection initiated\n", m_session_id);
}

bool GoogleStreamerSession::writeAudio(const void* data, size_t len) {
    if (len == 0) {
        return true;
    }

    State state = m_state.load();

    if (state == State::CONNECTING) {
        // Buffer audio until connected
        return m_preconnect_buffer.add(data, len);
    }

    if (state != State::STREAMING) {
        return false;
    }

    // Enqueue audio
    bool enqueued = m_audio_queue.enqueue(data, len);

    // Try to send if no write in progress
    if (!m_write_in_progress.load()) {
        trySendAudio();
    }

    return enqueued;
}

void GoogleStreamerSession::trySendAudio() {
    // Only one write at a time
    bool expected = false;
    if (!m_write_in_progress.compare_exchange_strong(expected, true)) {
        return;  // Another write in progress
    }

    AudioChunk chunk;
    if (!m_audio_queue.dequeue(chunk)) {
        // Queue empty
        m_write_in_progress.store(false);

        // Check if we should send WritesDone
        if (m_writes_done_sent.load() && !m_audio_queue.empty()) {
            // More audio arrived, keep going
            return;
        }
        return;
    }

    // Build request with audio
    StreamingRecognizeRequest request;
    request.set_audio(chunk.data, chunk.size);

    // Send async write
    m_stream->Write(request, m_write_tag.get());
    m_audio_bytes_sent += chunk.size;
}

void GoogleStreamerSession::writesDone() {
    if (m_writes_done_sent.exchange(true)) {
        return;  // Already sent
    }

    if (m_state.load() != State::STREAMING) {
        return;
    }

    // Wait for pending write to complete, then send WritesDone
    // This will be handled in onWriteComplete
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "Session %lu: WritesDone requested\n", m_session_id);
}

void GoogleStreamerSession::stop() {
    State current = m_state.load();
    if (current == State::FINISHED || current == State::ERROR) {
        return;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "Session %lu: Stopping (current state=%d)\n",
        m_session_id, static_cast<int>(current));

    // Cancel any pending operations
    m_context.TryCancel();

    setState(State::FINISHED);
}

// State machine callbacks

void GoogleStreamerSession::onConnectComplete(bool ok) {
    if (!ok) {
        setError(ErrorInfo(ErrorCode::CONNECT_FAILED,
            "Failed to connect to Google Speech API",
            "AsyncStreamingRecognize connection failed"));
        setState(State::ERROR);
        return;
    }
    
    m_metrics.recordSuccess();

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "Session %lu: Connected to Google Speech API\n", m_session_id);

    // Send initial config
    m_stream->Write(m_request, m_write_tag.get());
    m_write_in_progress.store(true);

    // Start reading responses
    m_stream->Read(&m_response, m_read_tag.get());

    setState(State::STREAMING);

    // Flush pre-connection buffer
    size_t flushed = m_preconnect_buffer.flushTo(m_audio_queue);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "Session %lu: Flushed %zu pre-connect chunks\n", m_session_id, flushed);

    // Fire connect event
    fireEvent("google_transcribev2::connect", "{}", false);

    // Send session start to Pusher
    if (!m_config.pusher_app_id.empty()) {
        CallMetadata metadata;
        metadata.sip_call_id = m_config.sip_call_id;
        metadata.caller_number = m_config.caller_number;
        metadata.caller_name = m_config.caller_name;
        metadata.callee_number = m_config.callee_number;
        metadata.callee_name = m_config.callee_name;
        
        auto event = createSessionStartEvent(
            m_fs_uuid,
            metadata,
            m_config.pusher_app_id,
            m_config.pusher_app_key,
            m_config.pusher_app_secret,
            m_config.pusher_cluster
        );
        AsyncPusherClient::getInstance().enqueue(std::move(event));
    }
}

void GoogleStreamerSession::onWriteComplete(bool ok) {
    m_write_in_progress.store(false);

    if (!ok) {
        setError(ErrorInfo(ErrorCode::GRPC_INTERNAL,
            "gRPC write failed",
            "Stream write operation returned not-ok"));
        setState(State::ERROR);
        return;
    }

    // Check if we should send WritesDone
    if (m_writes_done_sent.load() && m_audio_queue.empty()) {
        setState(State::WRITES_DONE);
        m_stream->WritesDone(m_writes_done_tag.get());
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "Session %lu: Sent WritesDone\n", m_session_id);
        return;
    }

    // Try to send more audio
    trySendAudio();
}

void GoogleStreamerSession::onReadComplete(bool ok) {
    if (!ok) {
        // Stream ended or error
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "Session %lu: Read stream ended\n", m_session_id);

        // Start finishing
        setState(State::FINISHING);
        m_stream->Finish(&m_finish_status, m_finish_tag.get());
        return;
    }

    // Process the response
    processResponse(m_response);
    m_results_received++;

    // Continue reading
    m_stream->Read(&m_response, m_read_tag.get());
}

void GoogleStreamerSession::onWritesDoneComplete(bool ok) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "Session %lu: WritesDone complete (ok=%d)\n", m_session_id, ok);

    // Continue processing reads until stream ends
}

void GoogleStreamerSession::onFinishComplete(bool ok) {
    setState(State::FINISHED);

    if (m_finish_status.ok()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "Session %lu: Finished successfully, errors=%lu, success=%lu\n",
            m_session_id, m_metrics.totalErrors(), m_metrics.totalSuccess());
        m_metrics.recordSuccess();
    } else {
        setGrpcError(m_finish_status, "Stream finished with error");
    }

    // Fire disconnect event
    fireEvent("google_transcribev2::disconnect", "{}", true);

    // Send session stop to Pusher
    if (!m_config.pusher_app_id.empty()) {
        CallMetadata metadata;
        metadata.sip_call_id = m_config.sip_call_id;
        
        auto event = createSessionStopEvent(
            m_fs_uuid,
            metadata,
            m_config.pusher_app_id,
            m_config.pusher_app_key,
            m_config.pusher_app_secret,
            m_config.pusher_cluster
        );
        AsyncPusherClient::getInstance().enqueue(std::move(event));
    }
}

void GoogleStreamerSession::processResponse(const StreamingRecognizeResponse& response) {
    for (int r = 0; r < response.results_size(); r++) {
        const auto& result = response.results(r);

        for (int a = 0; a < result.alternatives_size(); a++) {
            const auto& alternative = result.alternatives(a);
            deliverResult(result, alternative);
        }
    }
}

void GoogleStreamerSession::deliverResult(
    const google::cloud::speech::v2::StreamingRecognizeResponse::StreamingRecognitionResult& result,
    const google::cloud::speech::v2::SpeechRecognitionAlternative& alternative)
{
    // Build JSON using cJSON for compatibility with existing code
    cJSON* json = cJSON_CreateObject();
    cJSON* alternatives = cJSON_CreateArray();
    cJSON* alt_obj = cJSON_CreateObject();

    cJSON_AddStringToObject(alt_obj, "transcript", alternative.transcript().c_str());

    if (alternative.confidence() > 0.0) {
        cJSON_AddNumberToObject(alt_obj, "confidence", alternative.confidence());
    }

    // Add word-level details if available
    if (alternative.words_size() > 0) {
        cJSON* words = cJSON_CreateArray();
        for (int w = 0; w < alternative.words_size(); w++) {
            const auto& word = alternative.words(w);
            cJSON* word_obj = cJSON_CreateObject();

            cJSON_AddStringToObject(word_obj, "word", word.word().c_str());

            if (word.has_start_offset()) {
                double start_sec = word.start_offset().seconds() +
                                   word.start_offset().nanos() / 1e9;
                cJSON_AddNumberToObject(word_obj, "start_time", start_sec);
            }

            if (word.has_end_offset()) {
                double end_sec = word.end_offset().seconds() +
                                 word.end_offset().nanos() / 1e9;
                cJSON_AddNumberToObject(word_obj, "end_time", end_sec);
            }

            if (word.confidence() > 0.0) {
                cJSON_AddNumberToObject(word_obj, "confidence", word.confidence());
            }

            cJSON_AddItemToArray(words, word_obj);
        }
        cJSON_AddItemToObject(alt_obj, "words", words);
    }

    cJSON_AddItemToArray(alternatives, alt_obj);
    cJSON_AddItemToObject(json, "alternatives", alternatives);

    cJSON_AddBoolToObject(json, "is_final", result.is_final());

    if (result.stability() > 0.0) {
        cJSON_AddNumberToObject(json, "stability", result.stability());
    }

    if (result.channel_tag() > 0) {
        cJSON_AddNumberToObject(json, "channel", result.channel_tag());
    }

    if (!result.language_code().empty()) {
        cJSON_AddStringToObject(json, "language_code", result.language_code().c_str());
    }

    // Convert to string
    char* json_str = cJSON_PrintUnformatted(json);
    if (json_str) {
        // Deliver via response handler
        fireEvent("google_transcribev2::transcription", json_str, result.is_final());

        // Send to Pusher
        sendToPusher(json_str, result.is_final());

        free(json_str);
    }

    cJSON_Delete(json);
}

void GoogleStreamerSession::fireEvent(const char* event_name, const char* json, bool is_final) {
    // Find FreeSWITCH session
    switch_core_session_t* session = switch_core_session_locate(m_fs_uuid.c_str());
    if (!session) {
        return;
    }

    // Call response handler
    if (m_response_handler) {
        m_response_handler(session, event_name, json, m_bugname.c_str(), is_final ? 1 : 0);
    }

    // Fire FreeSWITCH event
    switch_channel_t* channel = switch_core_session_get_channel(session);
    switch_event_t* event;

    if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, event_name) == SWITCH_STATUS_SUCCESS) {
        switch_channel_event_set_data(channel, event);
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
        switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-session-finished",
                                       is_final ? "true" : "false");
        switch_event_add_body(event, "%s", json);
        switch_event_fire(&event);
    }

    switch_core_session_rwunlock(session);
}

void GoogleStreamerSession::sendToPusher(const std::string& json, bool is_final) {
    if (m_config.pusher_app_id.empty()) {
        return;
    }

    CallMetadata metadata;
    metadata.sip_call_id = m_config.sip_call_id;
    metadata.caller_number = m_config.caller_number;
    metadata.caller_name = m_config.caller_name;
    metadata.callee_number = m_config.callee_number;
    metadata.callee_name = m_config.callee_name;

    auto event = createTranscriptionEvent(
        json,
        m_fs_uuid,
        metadata,
        is_final,
        m_config.pusher_app_id,
        m_config.pusher_app_key,
        m_config.pusher_app_secret,
        m_config.pusher_cluster
    );

    AsyncPusherClient::getInstance().enqueue(std::move(event));
}

void GoogleStreamerSession::setState(State new_state) {
    State old_state = m_state.exchange(new_state);
    if (old_state != new_state) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "Session %lu: State %d -> %d\n",
            m_session_id, static_cast<int>(old_state), static_cast<int>(new_state));
    }
}

void GoogleStreamerSession::setError(const ErrorInfo& error) {
    // Store the error
    m_last_error = error;
    m_last_error.session_id = m_session_id;
    m_last_error.fs_uuid = m_fs_uuid;
    
    // Update metrics
    m_metrics.recordError(error.category);
    
    // Log based on category
    switch_log_level_t log_level = SWITCH_LOG_ERROR;
    if (error.category == ErrorCategory::TIMEOUT || 
        error.category == ErrorCategory::RESOURCE) {
        log_level = SWITCH_LOG_WARNING;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, log_level,
        "Session %lu: Error [%d/%s] %s%s%s\n",
        m_session_id,
        error.codeAsInt(),
        error.categoryName(),
        error.message.c_str(),
        error.details.empty() ? "" : " - ",
        error.details.c_str());
    
    // Invoke error handler callback if configured
    if (m_config.error_handler) {
        try {
            m_config.error_handler(m_last_error);
        } catch (const std::exception& e) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "Session %lu: Error handler threw exception: %s\n",
                m_session_id, e.what());
        }
    }
    
    // Fire FreeSWITCH error event
    fireEvent("google_transcribev2::error", m_last_error.toJson().c_str(), true);
}

void GoogleStreamerSession::setGrpcError(const grpc::Status& status, const std::string& context) {
    ErrorInfo error;
    error.code = grpcStatusToErrorCode(static_cast<int>(status.error_code()));
    error.category = ErrorCategory::GRPC;
    error.message = context;
    error.details = status.error_message();
    error.grpc_status_code = static_cast<int>(status.error_code());
    error.grpc_error_message = status.error_message();
    error.grpc_error_details = status.error_details();
    
    // Detect specific Google API errors from message content
    const std::string& msg = status.error_message();
    
    // "Invalid resource field value in the request" - wrong project/location
    if (msg.find("Invalid resource field") != std::string::npos ||
        msg.find("invalid resource") != std::string::npos) {
        error.code = ErrorCode::API_INVALID_RESOURCE;
        error.category = ErrorCategory::CONFIGURATION;
        error.message = "Invalid Google Cloud resource path";
        error.details = "Check GOOGLE_PROJECT_ID and GOOGLE_LOCATION_ID. " + msg;
    }
    // "Recognizer not found" or similar
    else if (msg.find("Recognizer") != std::string::npos && 
             msg.find("not found") != std::string::npos) {
        error.code = ErrorCode::API_RECOGNIZER_NOT_FOUND;
        error.category = ErrorCategory::CONFIGURATION;
        error.message = "Google Speech recognizer not found";
        error.details = "Verify project has Speech-to-Text API enabled. " + msg;
    }
    // Check if it's an auth error
    else if (status.error_code() == grpc::StatusCode::UNAUTHENTICATED ||
             status.error_code() == grpc::StatusCode::PERMISSION_DENIED) {
        error.category = ErrorCategory::AUTHENTICATION;
        if (msg.find("credential") != std::string::npos || 
            msg.find("token") != std::string::npos) {
            error.details = "Check GOOGLE_APPLICATION_CREDENTIALS. " + msg;
        }
    }
    // Check for connection errors
    else if (status.error_code() == grpc::StatusCode::UNAVAILABLE) {
        error.category = ErrorCategory::CONNECTION;
    }
    
    setError(error);
}

} // namespace google_transcribe
