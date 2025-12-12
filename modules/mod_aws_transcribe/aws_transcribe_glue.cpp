#include "aws_transcribe_glue.h"
#include "audio_pipe.h"
#include "mod_aws_transcribe.h" // For cap_cb
#include <speex/speex_resampler.h>
#include <thread>
#include "worker_thread.h" // For job queue
#include <string>

extern "C" {
#include "async_pusher.h"
}

// Forward declaration of global pusher
extern async_pusher_t* g_pusher;

// Forward declaration of response handler
static void responseHandler(switch_core_session_t* session, const char * json, const char* bugname, bool final);

// Implementation of the response handler
static void responseHandler(switch_core_session_t* session, const char * json, const char* bugname, bool final) {
    // Fire FreeSWITCH event
    switch_event_t *event;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, final ? TRANSCRIBE_EVENT_RESULTS_FINAL : TRANSCRIBE_EVENT_RESULTS);
    switch_channel_event_set_data(channel, event);
    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
    switch_event_add_body(event, "%s", json);
    if (bugname) switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
    switch_event_fire(&event);

    // Send via async pusher if configured
    if (g_pusher) {
        std::string channel_name = "private-call-" + std::string(switch_core_session_get_uuid(session));
        std::string event_name = final ? TRANSCRIBE_EVENT_RESULTS_FINAL : TRANSCRIBE_EVENT_RESULTS;
        async_pusher_send(g_pusher, channel_name.c_str(), event_name.c_str(), json);
    }
}

// The user_data for the media bug will be a shared_ptr to the AwsPipe,
// but since the bug's user_data is a void*, we must new/delete it.
struct BugData {
    std::shared_ptr<AwsPipe> pPipe;
    SpeexResamplerState *resampler;
    uint32_t source_rate;
    // Resampler stats
    uint64_t resampler_frames_processed = 0;
    uint64_t resampler_samples_in = 0;
    uint64_t resampler_samples_out = 0;
    uint64_t resampler_bytes_written = 0;
    switch_time_t resampler_start_time = 0;
};

switch_status_t aws_transcribe_session_init(switch_core_session_t *session, ResponseHandler_t responseHandler, uint32_t samples_per_second, uint32_t channels, const char* lang, int interim, const char* bugname, const char* metadata, void **ppUserData, switch_media_bug_flag_t flags) {
    
    int err;
    switch_channel_t *channel = switch_core_session_get_channel(session);
    auto read_codec = switch_core_session_get_read_codec(session);
    uint32_t source_rate = read_codec->implementation->actual_samples_per_second;

    auto pPipe = g_pipe_pool.acquire();
    if (!pPipe) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error acquiring aws pipe from pool (pool exhausted).\n");
		return SWITCH_STATUS_FALSE;
    }
    
    // Populate options from channel variables
    AwsTranscribeOptions options;
    options.lang = lang;
    options.interim = interim;
    options.bugname = bugname;
    
    const char* var;
    if ((var = switch_channel_get_variable(channel, "AWS_VOCABULARY_NAME"))) options.vocabularyName = var;
    if ((var = switch_channel_get_variable(channel, "AWS_VOCABULARY_FILTER_NAME"))) options.vocabularyFilterName = var;
    if ((var = switch_channel_get_variable(channel, "AWS_VOCABULARY_FILTER_METHOD"))) options.vocabularyFilterMethod = var;
    if ((var = switch_channel_get_variable(channel, "AWS_SESSION_ID"))) options.sessionId = var;
    
    if ((var = switch_channel_get_variable(channel, "AWS_SHOW_SPEAKER_LABEL"))) {
        options.showSpeakerLabel = switch_true(var);
    } else if ((var = switch_channel_get_variable(channel, "AWS_SPEAKER_LABEL"))) { // Deprecated
        options.showSpeakerLabel = switch_true(var);
    } else {
        options.showSpeakerLabel = false;
    }

    if ((var = switch_channel_get_variable(channel, "AWS_NUMBER_OF_CHANNELS"))) {
        options.numberOfChannels = atoi(var);
    } else {
        options.numberOfChannels = channels;
    }

    if ((var = switch_channel_get_variable(channel, "AWS_ENABLE_CHANNEL_IDENTIFICATION"))) {
        options.enableChannelIdentification = switch_true(var);
    } else {
        // Default to true if stereo
        options.enableChannelIdentification = (options.numberOfChannels > 1);
    }

    if ((var = switch_channel_get_variable(channel, "START_RECOGNIZING_ON_VAD"))) {
        options.startOnVad = switch_true(var);
    } else {
        options.startOnVad = false;
    }

    if (metadata) {
        options.metadata = metadata;
        switch_channel_set_variable(channel, "AWS_METADATA", metadata);
    } else if ((var = switch_channel_get_variable(channel, "AWS_METADATA"))) {
        options.metadata = var;
    }

    // Initialize the recycled AwsPipe object
    pPipe->init(session, samples_per_second, channels, options, responseHandler);


    // The BugData struct will be our user_data for the media bug.
    // We dynamically allocate it and give it ownership of one shared_ptr to the AwsPipe.
    BugData* pBugData = new BugData();
    pBugData->pPipe = pPipe;
    pBugData->resampler = nullptr;
    pBugData->source_rate = source_rate;
    pBugData->resampler_start_time = switch_time_now();

    if (source_rate != samples_per_second) {
        pBugData->resampler = speex_resampler_init(channels, source_rate, samples_per_second, 2, &err);
        if (!pBugData->resampler) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to create resampler: %s\n", speex_resampler_strerror(err));
            delete pBugData;
            return SWITCH_STATUS_FALSE;
        }
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Created resampler: %u -> %u\n", source_rate, samples_per_second);
    }

    // Pass the raw pointer to the media bug.
    // We are responsible for deleting this in the CLOSE callback.
    *ppUserData = pBugData;
    
    switch_media_bug_t *bug = NULL;
    if (switch_core_media_bug_add(session, bugname, NULL, aws_transcribe_frame, pBugData, 0, flags, &bug) != SWITCH_STATUS_SUCCESS) {
        if (pBugData->resampler) speex_resampler_destroy(pBugData->resampler);
        delete pBugData;
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error adding media bug.\n");
		return SWITCH_STATUS_FALSE;
	}

    // This is the key for crash safety:
    // We will launch the processing thread and give it its OWN shared_ptr to the AwsPipe.
    // Even if the call hangs up and the media bug is destroyed, this thread
    // will keep the AwsPipe object alive until it is finished.
    WorkerJob *job = new WorkerJob{JobType::Connect, pPipe};
    push_job(job);


    return SWITCH_STATUS_SUCCESS;
}

switch_status_t aws_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing, char* bugname) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, bugname);

    if (bug) {
        BugData *pBugData = (BugData *) switch_core_media_bug_get_user_data(bug);
        if (pBugData && pBugData->pPipe) {
            // Signal the pipe to close by pushing a disconnect job.
            WorkerJob *job = new WorkerJob{JobType::Disconnect, pBugData->pPipe};
            g_job_queue.push(job);
        }
        // Detach the bug. This will trigger the CLOSE callback.
        if (!channelIsClosing) {
            switch_channel_set_private(channel, bugname, NULL);
            switch_core_media_bug_remove(session, &bug);
        }
    }
    return SWITCH_STATUS_SUCCESS;
}

switch_bool_t aws_transcribe_frame(switch_media_bug_t *bug, void* user_data, switch_abc_type_t type) {
    switch_core_session_t *session = switch_core_media_bug_get_session(bug);
    BugData *pBugData = (BugData *) user_data;

    switch (type) {
        case SWITCH_ABC_TYPE_CLOSE:
        {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_CLOSE.\n");
            if (pBugData) {
                // Log final resampler statistics before cleanup
                if (pBugData->resampler_frames_processed > 0) {
                    switch_time_t now = switch_time_now();
                    double elapsed_secs = (now - pBugData->resampler_start_time) / 1000000.0;
                    double fps = pBugData->resampler_frames_processed / elapsed_secs;
                    double mb_written = pBugData->resampler_bytes_written / (1024.0 * 1024.0);
                    double kbps = (pBugData->resampler_bytes_written * 8.0) / (elapsed_secs * 1000.0);

                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                        "[RESAMPLER-FINAL] Session complete - frames=%lu, samples_in=%lu, samples_out=%lu, "
                        "bytes=%.2fMB, avg_fps=%.1f, duration=%.1fs, bitrate=%.1fkbps\n",
                        (unsigned long)pBugData->resampler_frames_processed,
                        (unsigned long)pBugData->resampler_samples_in,
                        (unsigned long)pBugData->resampler_samples_out,
                        mb_written, fps, elapsed_secs, kbps);
                }

                if (pBugData->resampler) speex_resampler_destroy(pBugData->resampler);
                // Deleting BugData will decrement the ref count of the shared_ptr.
                // The AwsPipe object will be destroyed now IF the processing thread is also done.
                delete pBugData;
            }
        }
        break;

        case SWITCH_ABC_TYPE_READ:
        {
            if (!pBugData || !pBugData->pPipe) {
                return SWITCH_TRUE;
            }
            
            uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
            switch_frame_t frame = {};
            frame.data = data;
            frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

            while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
                if (frame.datalen) {
                    pBugData->resampler_frames_processed++;
                    pBugData->resampler_samples_in += frame.samples;

                    if (pBugData->resampler) {
                        spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
                        spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
                        spx_uint32_t in_len = frame.samples;

                        speex_resampler_process_interleaved_int(pBugData->resampler,
                            (const spx_int16_t *) frame.data,
                            &in_len,
                            &out[0],
                            &out_len);
                        
                        if (out_len > 0) {
                            pBugData->resampler_samples_out += out_len;
                            size_t bytes_to_write = out_len * frame.channels * sizeof(spx_int16_t);
                            pBugData->resampler_bytes_written += bytes_to_write;

                            void* p1;
                            void* p2;
                            size_t len1, len2;
                            if (pBugData->pPipe->reserveAudioSpace(bytes_to_write, &p1, &len1, &p2, &len2)) {
                                memcpy(p1, &out[0], len1);
                                if (len2 > 0) {
                                    memcpy(p2, (char*)&out[0] + len1, len2);
                                }
                                pBugData->pPipe->commitAudioData(bytes_to_write);
                            }
                        }
                    } else {
                        pBugData->resampler_samples_out += frame.samples;
                        pBugData->resampler_bytes_written += frame.datalen;

                        void* p1;
                        void* p2;
                        size_t len1, len2;
                        if (pBugData->pPipe->reserveAudioSpace(frame.datalen, &p1, &len1, &p2, &len2)) {
                            memcpy(p1, frame.data, len1);
                            if (len2 > 0) {
                                memcpy(p2, (char*)frame.data + len1, len2);
                            }
                            pBugData->pPipe->commitAudioData(frame.datalen);
                        }
                    }
                }
            }
        }
        break;

        default:
            break;
    }

    return SWITCH_TRUE;
}
