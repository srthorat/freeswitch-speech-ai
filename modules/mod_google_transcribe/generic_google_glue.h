#ifndef __GENERIC_GOOGLE_GLUE_H__
#define __GENERIC_GOOGLE_GLUE_H__

#include <switch_json.h>

template<typename Streamer>
switch_bool_t google_speech_frame(switch_media_bug_t *bug, void* user_data) {
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);
	struct cap_cb *cb = (struct cap_cb *) user_data;
    if (cb->streamer && (!cb->wants_single_utterance || !cb->got_end_of_utterance)) {
        Streamer* streamer = (Streamer *) cb->streamer;
        uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = {};
        frame.data = data;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

        if (switch_mutex_trylock(cb->mutex) == SWITCH_STATUS_SUCCESS) {
            while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
                if (frame.datalen) {
                    if (cb->vad && !streamer->isConnected()) {
                        switch_vad_state_t state = switch_vad_process(cb->vad, (int16_t*) frame.data, frame.samples);
                        if (state == SWITCH_VAD_STATE_START_TALKING) {
                            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "detected speech, connect to google speech now\n");
                            streamer->connect();
                            cb->responseHandler(session, "vad_detected", cb->bugname);
                        }
                    }

                    if (cb->resampler) {
                        spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
                        spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
                        // For speex_resampler_process_interleaved_int with channels > 1:
                        // in_len is number of FRAMES, not samples. Each frame has cb->channels samples.
                        spx_uint32_t in_len = frame.samples / cb->channels;
                        spx_uint32_t in_len_before = in_len;

                        speex_resampler_process_interleaved_int(cb->resampler,
                            (const spx_int16_t *) frame.data,
                            (spx_uint32_t *) &in_len,
                            &out[0],
                            &out_len);

                        // Track resampler statistics
                        cb->resampler_frames_processed++;
                        cb->resampler_samples_in += in_len_before * cb->channels;
                        cb->resampler_samples_out += out_len * cb->channels;
                        // For interleaved stereo: bytes = frames * channels * sizeof(int16)
                        // out_len is number of output frames, each frame has cb->channels samples
                        size_t bytes_to_write = sizeof(spx_int16_t) * out_len * cb->channels;
                        cb->resampler_bytes_written += bytes_to_write;

                        streamer->write(&out[0], bytes_to_write);
                        
                        // Log stats every 10 seconds (500 frames at 50fps)
                        switch_time_t now = switch_time_now();
                        if ((now - cb->resampler_last_log_time) >= 10000000) {  // 10 seconds in microseconds
                            double elapsed_secs = (now - cb->resampler_start_time) / 1000000.0;
                            double fps = cb->resampler_frames_processed / elapsed_secs;
                            double mb_written = cb->resampler_bytes_written / (1024.0 * 1024.0);
                            
                            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                                "[RESAMPLER-STATS] %s: frames=%lu, samples_in=%lu, samples_out=%lu, "
                                "bytes=%.2fMB, fps=%.1f, elapsed=%.1fs, ratio=%.3f, channels=%u\n",
                                cb->bugname,
                                (unsigned long)cb->resampler_frames_processed,
                                (unsigned long)cb->resampler_samples_in,
                                (unsigned long)cb->resampler_samples_out,
                                mb_written, fps, elapsed_secs,
                                (double)cb->resampler_samples_out / cb->resampler_samples_in,
                                cb->channels);
                            cb->resampler_last_log_time = now;
                        }
                    }
                    else {
                        // Passthrough mode - still track stats
                        cb->resampler_frames_processed++;
                        cb->resampler_samples_in += frame.samples;
                        cb->resampler_samples_out += frame.samples;
                        // Passthrough: frame.samples already includes all channels in interleaved format
                        // frame.datalen = actual bytes in buffer
                        size_t bytes_to_write = frame.datalen;
                        cb->resampler_bytes_written += bytes_to_write;

                        streamer->write(frame.data, bytes_to_write);
                    }
                }
            }
            switch_mutex_unlock(cb->mutex);
        }
	}
	return SWITCH_TRUE;
}

template<typename Streamer>
switch_status_t google_speech_session_init(switch_core_session_t *session, responseHandler_t responseHandler,
		switch_thread_start_t func, uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang,
		int interim, char *bugname, int single_utterance, int separate_recognition, int max_alternatives,
		int profanity_filter, int word_time_offset, int punctuation, const char* model, int enhanced,
		const char* hints, char* play_file, void **ppUserData) {

	switch_channel_t *channel = switch_core_session_get_channel(session);
	auto read_codec = switch_core_session_get_read_codec(session);
	uint32_t sampleRate = read_codec->implementation->actual_samples_per_second;
	struct cap_cb *cb;
	int err;

	cb =(struct cap_cb *) switch_core_session_alloc(session, sizeof(*cb));
	strncpy(cb->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
	strncpy(cb->bugname, bugname, MAX_BUG_LEN);
	cb->got_end_of_utterance = 0;
	cb->wants_single_utterance = single_utterance;
	cb->channels = channels;  // Store channels for frame processing
	if (play_file != NULL){
		cb->play_file = 1;
	}
	
	// Initialize resampler statistics
	cb->resampler_source_rate = sampleRate;
	cb->resampler_target_rate = to_rate;
	cb->resampler_frames_processed = 0;
	cb->resampler_samples_in = 0;
	cb->resampler_samples_out = 0;
	cb->resampler_bytes_written = 0;
	cb->resampler_start_time = switch_time_now();
	cb->resampler_last_log_time = cb->resampler_start_time;
	
	// Initialize per-channel timing for latency analysis
	cb->stream_start_time = cb->resampler_start_time;
	cb->first_result_time_ch1 = 0;
	cb->first_result_time_ch2 = 0;
	cb->got_first_result_ch1 = 0;
	cb->got_first_result_ch2 = 0;
	
	switch_mutex_init(&cb->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
	if (sampleRate != to_rate) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
			"[RESAMPLER-INIT] %s: Creating resampler %uHz->%uHz, channels=%u, mode=%s\n",
			bugname, sampleRate, to_rate, channels,
			sampleRate < to_rate ? "UPSAMPLE" : "DOWNSAMPLE");
		// Initialize resampler with actual channel count for proper stereo interleaved processing
		cb->resampler = speex_resampler_init(channels, sampleRate, to_rate, SWITCH_RESAMPLE_QUALITY, &err);
		if (0 != err) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing resampler: %s.\n",
								switch_channel_get_name(channel), speex_resampler_strerror(err));
			return SWITCH_STATUS_FALSE;
		}
	} else {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "%s: no resampling needed for this call, channels=%u\n", switch_channel_get_name(channel), channels);
	}
	cb->responseHandler = responseHandler;

	// allocate vad if we are delaying connecting to the recognizer until we detect speech
	if (switch_channel_var_true(channel, "START_RECOGNIZING_ON_VAD")) {
		cb->vad = switch_vad_init(sampleRate, 1);
		if (cb->vad) {
			const char* var;
			int mode = 2;
			int silence_ms = 150;
			int voice_ms = 250;
			int debug = 0;

			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_MODE")) {
				mode = atoi(var);
			}
			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_SILENCE_MS")) {
				silence_ms = atoi(var);
			}
			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_VOICE_MS")) {
				voice_ms = atoi(var);
			}
			if (var = switch_channel_get_variable(channel, "RECOGNIZER_VAD_VOICE_MS")) {
				voice_ms = atoi(var);
			}
			switch_vad_set_mode(cb->vad, mode);
			switch_vad_set_param(cb->vad, "silence_ms", silence_ms);
			switch_vad_set_param(cb->vad, "voice_ms", voice_ms);
			switch_vad_set_param(cb->vad, "debug", debug);
		}
	}

	Streamer *streamer = NULL;
	try {
	    streamer = new Streamer(session, channels, lang, interim, to_rate, sampleRate, single_utterance, separate_recognition, max_alternatives,
		    profanity_filter, word_time_offset, punctuation, model, enhanced, hints);
	    cb->streamer = streamer;
	} catch (std::exception& e) {
	    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "%s: Error initializing gstreamer: %s.\n", 
		switch_channel_get_name(channel), e.what());
	    return SWITCH_STATUS_FALSE;
	}

	if (!cb->vad) streamer->connect();

	// create the read thread
	switch_threadattr_t *thd_attr = NULL;
	switch_memory_pool_t *pool = switch_core_session_get_pool(session);

	switch_threadattr_create(&thd_attr, pool);
	switch_threadattr_stacksize_set(thd_attr, SWITCH_THREAD_STACKSIZE);
	switch_thread_create(&cb->thread, thd_attr, func, cb, pool);

	*ppUserData = cb;
	return SWITCH_STATUS_SUCCESS;
}

template<typename Streamer>
switch_status_t google_speech_session_cleanup(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
	switch_channel_t *channel = switch_core_session_get_channel(session);

	if (bug) {
		struct cap_cb *cb = (struct cap_cb *) switch_core_media_bug_get_user_data(bug);
		switch_mutex_lock(cb->mutex);

		if (!switch_channel_get_private(channel, cb->bugname)) {
			// race condition
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached (race).\n", switch_channel_get_name(channel));
			switch_mutex_unlock(cb->mutex);
			return SWITCH_STATUS_FALSE;
		}
		switch_channel_set_private(channel, cb->bugname, NULL);

		// stop playback if available
		if (cb->play_file == 1){ 
			if (switch_channel_test_flag(channel, CF_BROADCAST)) {
				switch_channel_stop_broadcast(channel);
			} else {
				switch_channel_set_flag_value(channel, CF_BREAK, 1);
			}
		}

		// close connection and get final responses
		Streamer* streamer = (Streamer *) cb->streamer;

		if (streamer) {
			// Flush any accumulated audio before ending the stream
			streamer->flushAccumulatedAudio();
			streamer->writesDone();

			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_speech_session_cleanup: GStreamer (%p) waiting for read thread to complete\n", (void*)streamer);
			switch_status_t st;
			switch_thread_join(&st, cb->thread);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_speech_session_cleanup:  GStreamer (%p) read thread completed\n", (void*)streamer);

			delete streamer;
			cb->streamer = NULL;
		}

		// Log final resampler statistics before cleanup (for both resampling and passthrough modes)
		if (cb->resampler_frames_processed > 0) {
			switch_time_t now = switch_time_now();
			double elapsed_secs = (now - cb->resampler_start_time) / 1000000.0;
			double fps = cb->resampler_frames_processed / elapsed_secs;
			double mb_written = cb->resampler_bytes_written / (1024.0 * 1024.0);
			double kbps = (cb->resampler_bytes_written * 8.0) / (elapsed_secs * 1000.0);
			
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
				"[RESAMPLER-FINAL] %s: Session complete - frames=%lu, samples_in=%lu, samples_out=%lu, "
				"bytes=%.2fMB, avg_fps=%.1f, duration=%.1fs, bitrate=%.1fkbps, channels=%u, mode=%s\n",
				cb->bugname,
				(unsigned long)cb->resampler_frames_processed,
				(unsigned long)cb->resampler_samples_in,
				(unsigned long)cb->resampler_samples_out,
				mb_written, fps, elapsed_secs, kbps, cb->channels,
				cb->resampler ? 
					(cb->resampler_source_rate < cb->resampler_target_rate ? "UPSAMPLE" : "DOWNSAMPLE") 
					: "PASSTHROUGH");
		}

		if (cb->resampler) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
				"[RESAMPLER-CLEANUP] %s: Destroying resampler %uHz->%uHz\n",
				cb->bugname, cb->resampler_source_rate, cb->resampler_target_rate);
			speex_resampler_destroy(cb->resampler);
		}
		if (cb->vad) {
			switch_vad_destroy(&cb->vad);
			cb->vad = nullptr;
		}
		if (!channelIsClosing) {
			switch_core_media_bug_remove(session, &bug);
		}

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "google_speech_session_cleanup: Closed stream\n");

		switch_mutex_unlock(cb->mutex);

		return SWITCH_STATUS_SUCCESS;
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s Bug is not attached.\n", switch_channel_get_name(channel));
	return SWITCH_STATUS_FALSE;
}

template<typename PhraseSet>
void google_speech_configure_grammar_hints(switch_core_session_t *session, switch_channel_t *channel, const char* hints, PhraseSet* phrase_set) {
    float boost = -1;

    // get boost setting for the phrase set in its entirety
    if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_HINTS_BOOST"))) {
        boost = (float) atof(switch_channel_get_variable(channel, "GOOGLE_SPEECH_HINTS_BOOST"));
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "boost value: %f\n", boost);
        phrase_set->set_boost(boost);
    }

    // hints are either a simple comma-separated list of phrases, or a json array of objects
    // containing a phrase and a boost value
    auto *jHint = cJSON_Parse((char *) hints);
    if (jHint) {
        int i = 0;
        cJSON *jPhrase = NULL;
        cJSON_ArrayForEach(jPhrase, jHint) {
            cJSON *jItem = cJSON_GetObjectItem(jPhrase, "phrase");
            if (jItem) {
                auto* phrase = phrase_set->add_phrases();
                phrase->set_value(cJSON_GetStringValue(jItem));
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "phrase: %s\n", phrase->value().c_str());
                if (cJSON_GetObjectItem(jPhrase, "boost")) {
                    phrase->set_boost((float) cJSON_GetObjectItem(jPhrase, "boost")->valuedouble);
                    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "boost value: %f\n", phrase->boost());
                }
                i++;
            }
        }
        cJSON_Delete(jHint);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added %d hints\n", i);
    }
    else {
        char *phrases[500] = { 0 };
        int argc = switch_separate_string((char *) hints, ',', phrases, 500);
        for (int i = 0; i < argc; i++) {
            auto* phrase = phrase_set->add_phrases();
            phrase->set_value(phrases[i]);
        }
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "added %d hints\n", argc);
    }
}

#endif