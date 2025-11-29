/*
 * google_transcribe_glue_v2.cpp - Updated C/C++ Glue Layer
 *
 * This file provides the C interface for the refactored async Google
 * Speech-to-Text module. It replaces the old blocking implementation
 * with the new async architecture.
 *
 * Key Changes:
 * - Uses GoogleStreamerSession with async state machine
 * - Uses shared_ptr for automatic memory management
 * - Simplified audio callback (just enqueue, no blocking)
 * - No more detached threads for cleanup
 */

#include <switch.h>
#include <switch_json.h>
#include <speex/speex_resampler.h>
#include <memory>
#include <string>

#include "google_streamer_session.h"
#include "grpc_manager.h"
#include "async_pusher_client.h"
#include "mod_google_transcribev2.h"

using namespace google_transcribe;

/**
 * Extended private data structure
 * Holds the async session and resampler
 */
struct private_data_v2 {
    switch_mutex_t* mutex;
    char session_id[MAX_SESSION_ID];
    char bugname[MAX_BUG_LEN + 1];
    char metadata[MAX_METADATA_LEN];

    // Async session (shared_ptr stored as void* for C compatibility)
    void* google_session;  // Actually std::shared_ptr<GoogleStreamerSession>*

    // Resampler for sample rate conversion
    SpeexResamplerState* resampler;

    // Configuration
    uint32_t sample_rate;
    uint32_t channels;
    responseHandler_t response_handler;

    // State flags
    unsigned int id;
    int is_finished : 1;
};

// Global session counter
static std::atomic<unsigned int> g_session_counter{0};

/**
 * Helper to get the GoogleStreamerSession from private data
 */
static std::shared_ptr<GoogleStreamerSession> getSession(private_data_v2* pvt) {
    if (!pvt || !pvt->google_session) {
        return nullptr;
    }
    auto* ptr = static_cast<std::shared_ptr<GoogleStreamerSession>*>(pvt->google_session);
    return *ptr;
}

/**
 * Helper to store the GoogleStreamerSession in private data
 */
static void setSession(private_data_v2* pvt, std::shared_ptr<GoogleStreamerSession> session) {
    if (pvt->google_session) {
        delete static_cast<std::shared_ptr<GoogleStreamerSession>*>(pvt->google_session);
    }
    pvt->google_session = new std::shared_ptr<GoogleStreamerSession>(session);
}

/**
 * Helper to clear the GoogleStreamerSession from private data
 */
static void clearSession(private_data_v2* pvt) {
    if (pvt->google_session) {
        delete static_cast<std::shared_ptr<GoogleStreamerSession>*>(pvt->google_session);
        pvt->google_session = nullptr;
    }
}

// C interface implementation

extern "C" {

switch_status_t google_transcribe_init() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "google_transcribe_init: Starting async Google Speech module\n");

    // Start GrpcManager with auto-detected worker count
    GrpcManager::getInstance().start(0);

    // Start AsyncPusherClient
    AsyncPusherClient::getInstance().start(2, 10000);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "google_transcribe_init: Module initialized successfully\n");

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_cleanup() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "google_transcribe_cleanup: Shutting down\n");

    // Shutdown in reverse order
    AsyncPusherClient::getInstance().shutdown(5000);
    GrpcManager::getInstance().shutdown();

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "google_transcribe_cleanup: Shutdown complete\n");

    return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_session_init(
    switch_core_session_t* session,
    responseHandler_t response_handler,
    uint32_t samples_per_second,
    uint32_t channels,
    char* lang,
    int interim,
    char* bugname,
    char* metadata,
    void** ppUserData)
{
    switch_codec_implementation_t read_impl;
    memset(&read_impl, 0, sizeof(read_impl));
    switch_core_session_get_read_impl(session, &read_impl);

    switch_channel_t* channel = switch_core_session_get_channel(session);

    // Allocate private data (from session pool for automatic cleanup)
    private_data_v2* pvt = (private_data_v2*)switch_core_session_alloc(
        session, sizeof(private_data_v2));
    memset(pvt, 0, sizeof(private_data_v2));

    // Initialize basic fields
    strncpy(pvt->session_id, switch_core_session_get_uuid(session), MAX_SESSION_ID - 1);
    strncpy(pvt->bugname, bugname, MAX_BUG_LEN);
    if (metadata) {
        strncpy(pvt->metadata, metadata, MAX_METADATA_LEN - 1);
    }

    pvt->sample_rate = samples_per_second;
    pvt->channels = channels;
    pvt->response_handler = response_handler;
    pvt->id = ++g_session_counter;

    // Initialize mutex
    if (switch_mutex_init(&pvt->mutex, SWITCH_MUTEX_NESTED,
                          switch_core_session_get_pool(session)) != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "(%u) Failed to initialize mutex\n", pvt->id);
        return SWITCH_STATUS_FALSE;
    }

    // Setup resampler if needed
    if (read_impl.actual_samples_per_second != samples_per_second) {
        int err;
        pvt->resampler = speex_resampler_init(
            channels,
            read_impl.actual_samples_per_second,
            samples_per_second,
            SWITCH_RESAMPLE_QUALITY,
            &err
        );
        if (err != 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "(%u) Failed to initialize resampler: %s\n",
                pvt->id, speex_resampler_strerror(err));
            return SWITCH_STATUS_FALSE;
        }
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "(%u) Resampling %u -> %u Hz\n",
            pvt->id, read_impl.actual_samples_per_second, samples_per_second);
    }

    // Build session config
    SessionConfig config;

    // Get project/location from channel vars or environment
    // Support multiple naming conventions: GOOGLE_PROJECT_ID, GCP_PROJECT_ID
    const char* var;
    if ((var = switch_channel_get_variable(channel, "GOOGLE_PROJECT_ID"))) {
        config.project_id = var;
    } else if ((var = switch_channel_get_variable(channel, "GCP_PROJECT_ID"))) {
        config.project_id = var;
    } else if ((var = getenv("GOOGLE_PROJECT_ID"))) {
        config.project_id = var;
    } else if ((var = getenv("GCP_PROJECT_ID"))) {
        config.project_id = var;
    }

    // Support: GOOGLE_LOCATION_ID, GCP_LOCATION, GCP_LOCATION_ID
    if ((var = switch_channel_get_variable(channel, "GOOGLE_LOCATION_ID"))) {
        config.location_id = var;
    } else if ((var = switch_channel_get_variable(channel, "GCP_LOCATION"))) {
        config.location_id = var;
    } else if ((var = switch_channel_get_variable(channel, "GCP_LOCATION_ID"))) {
        config.location_id = var;
    } else if ((var = getenv("GOOGLE_LOCATION_ID"))) {
        config.location_id = var;
    } else if ((var = getenv("GCP_LOCATION"))) {
        config.location_id = var;
    } else if ((var = getenv("GCP_LOCATION_ID"))) {
        config.location_id = var;
    }

    config.language_code = lang ? lang : "en-US";
    config.sample_rate = samples_per_second;
    config.channels = channels;
    config.interim_results = (interim != 0);

    // Extract call metadata for Pusher events
    if ((var = switch_channel_get_variable(channel, "sip_call_id"))) {
        config.sip_call_id = var;
    } else {
        config.sip_call_id = switch_core_session_get_uuid(session);
    }
    
    if ((var = switch_channel_get_variable(channel, "caller_id_number"))) {
        config.caller_number = var;
    }
    if ((var = switch_channel_get_variable(channel, "caller_id_name"))) {
        config.caller_name = var;
    }
    if ((var = switch_channel_get_variable(channel, "callee_id_number"))) {
        config.callee_number = var;
    }
    if ((var = switch_channel_get_variable(channel, "callee_id_name"))) {
        config.callee_name = var;
    }

    // Model configuration
    if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_MODEL"))) {
        config.model = var;
    }

    // Feature flags
    config.enable_automatic_punctuation =
        switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION"));
    config.enable_profanity_filter =
        switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_PROFANITY_FILTER"));
    config.enable_speaker_diarization =
        switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_SPEAKER_DIARIZATION"));

    if (config.enable_speaker_diarization) {
        if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT"))) {
            config.min_speaker_count = std::max(1, atoi(var));
        }
        if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT"))) {
            config.max_speaker_count = std::max(2, atoi(var));
        }
    }

    // Pusher configuration
    if ((var = switch_channel_get_variable(channel, "PUSHER_APP_ID"))) {
        config.pusher_app_id = var;
    } else if ((var = getenv("PUSHER_APP_ID"))) {
        config.pusher_app_id = var;
    }

    if ((var = switch_channel_get_variable(channel, "PUSHER_KEY"))) {
        config.pusher_app_key = var;
    } else if ((var = getenv("PUSHER_KEY"))) {
        config.pusher_app_key = var;
    }

    if ((var = switch_channel_get_variable(channel, "PUSHER_SECRET"))) {
        config.pusher_app_secret = var;
    } else if ((var = getenv("PUSHER_SECRET"))) {
        config.pusher_app_secret = var;
    }

    if ((var = switch_channel_get_variable(channel, "PUSHER_CLUSTER"))) {
        config.pusher_cluster = var;
    } else if ((var = getenv("PUSHER_CLUSTER"))) {
        config.pusher_cluster = var;
    }

    // Validate configuration before creating session
    auto validation = config.validate();
    if (validation.isError()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "(%u) Configuration validation failed: [%d] %s\n",
            pvt->id, validation.codeAsInt(), validation.message.c_str());
        return SWITCH_STATUS_FALSE;
    }

    // Create and start the async session
    try {
        auto google_session = GoogleStreamerSession::create(
            session,
            config,
            response_handler,
            bugname
        );

        setSession(pvt, google_session);
        google_session->start();

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "(%u) Started async Google transcription session\n", pvt->id);
    }
    catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "(%u) Failed to create Google session: %s\n", pvt->id, e.what());
        return SWITCH_STATUS_FALSE;
    }

    *ppUserData = pvt;
    return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_session_stop(
    switch_core_session_t* session,
    int channel_is_closing,
    char* bugname)
{
    switch_channel_t* channel = switch_core_session_get_channel(session);
    switch_media_bug_t* bug = (switch_media_bug_t*)switch_channel_get_private(channel, bugname);

    if (!bug) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "google_transcribe_session_stop: No bug found for %s\n", bugname);
        return SWITCH_STATUS_FALSE;
    }

    private_data_v2* pvt = (private_data_v2*)switch_core_media_bug_get_user_data(bug);
    if (!pvt) {
        return SWITCH_STATUS_FALSE;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "(%u) Stopping Google transcription session\n", pvt->id);

    switch_mutex_lock(pvt->mutex);

    // Stop the async session
    auto google_session = getSession(pvt);
    if (google_session) {
        // Signal writesDone first for graceful shutdown
        google_session->writesDone();

        // Give a brief moment for final results
        switch_yield(100000);  // 100ms

        // Then stop
        google_session->stop();
        clearSession(pvt);
    }

    // Cleanup resampler
    if (pvt->resampler) {
        speex_resampler_destroy(pvt->resampler);
        pvt->resampler = nullptr;
    }

    pvt->is_finished = 1;

    switch_mutex_unlock(pvt->mutex);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "(%u) Google transcription session stopped\n", pvt->id);

    return SWITCH_STATUS_SUCCESS;
}

switch_bool_t google_transcribe_frame(switch_media_bug_t* bug, void* user_data) {
    private_data_v2* pvt = (private_data_v2*)user_data;

    if (!pvt || pvt->is_finished) {
        return SWITCH_TRUE;
    }

    auto google_session = getSession(pvt);
    if (!google_session || !google_session->isActive()) {
        return SWITCH_TRUE;
    }

    uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
    switch_frame_t frame = {};
    frame.data = data;
    frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

    // Read audio frames - NO BLOCKING, just try to get data
    while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
        if (switch_test_flag(&frame, SFF_CNG)) {
            continue;  // Skip comfort noise
        }

        if (frame.datalen == 0) {
            continue;
        }

        // Resample if needed
        if (pvt->resampler) {
            spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
            spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
            spx_uint32_t in_len = frame.samples;

            speex_resampler_process_interleaved_int(
                pvt->resampler,
                (const spx_int16_t*)frame.data,
                &in_len,
                out,
                &out_len
            );

            // Non-blocking enqueue
            google_session->writeAudio(out, out_len * sizeof(spx_int16_t));
        } else {
            // Non-blocking enqueue
            google_session->writeAudio(frame.data, frame.datalen);
        }
    }

    return SWITCH_TRUE;
}

} // extern "C"
