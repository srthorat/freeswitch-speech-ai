#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>
#include <cstdlib>
#include <cstring>
#include <cmath>

#include "mod_google_transcribe.h"
#include "gstreamer.h"
#include "generic_google_glue.h"

#include "google/cloud/speech/v2/cloud_speech.grpc.pb.h"
#include <google/protobuf/field_mask.pb.h>

using google::cloud::speech::v2::RecognitionConfig;
using google::cloud::speech::v2::Speech;
using google::cloud::speech::v2::StreamingRecognizeRequest;
using google::cloud::speech::v2::StreamingRecognizeResponse;
using google::cloud::speech::v2::SpeakerDiarizationConfig;
using google::cloud::speech::v2::SpeechAdaptation;
using google::cloud::speech::v2::SpeechRecognitionAlternative;
using google::cloud::speech::v2::PhraseSet;
using google::cloud::speech::v2::PhraseSet_Phrase;
using google::cloud::speech::v2::StreamingRecognizeResponse_SpeechEventType_END_OF_SINGLE_UTTERANCE;
using google::cloud::speech::v2::StreamingRecognizeResponse_SpeechEventType_SPEECH_ACTIVITY_BEGIN;
using google::cloud::speech::v2::StreamingRecognizeResponse_SpeechEventType_SPEECH_ACTIVITY_END;
using google::cloud::speech::v2::ExplicitDecodingConfig_AudioEncoding_LINEAR16;
using google::cloud::speech::v2::RecognitionFeatures_MultiChannelMode_SEPARATE_RECOGNITION_PER_CHANNEL;
using google::cloud::speech::v2::SpeechAdaptation_AdaptationPhraseSet;
using google::rpc::Status;

typedef GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub> GStreamer_V2;

template<>
GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub>::GStreamer(
    switch_core_session_t *session, 
    uint32_t channels, 
    char* lang, 
    int interim, 
    uint32_t config_sample_rate,
	uint32_t samples_per_second,
    int single_utterance, 
    int separate_recognition,
	int max_alternatives, 
    int profanity_filter, 
    int word_time_offset, 
    int punctuation, 
    const char* model, 
    int enhanced, 
	const char* hints) : m_session(session), m_writesDone(false), m_connected(false),
    m_audioBuffer(CHUNKSIZE, 100) {  // 100 chunks = ~1 second of stereo 8kHz audio
  
    switch_channel_t *channel = switch_core_session_get_channel(session);
    const char* var;

    // For V2 API, we need to determine the location FIRST to construct the regional endpoint
    // V2 API requires regional endpoints like: us-central1-speech.googleapis.com
    const char* project_id = switch_channel_get_variable(channel, "GCP_PROJECT_ID");
    const char* location = switch_channel_get_variable(channel, "GCP_LOCATION");
    
    // Fall back to environment variables if channel variables not set
    if (!project_id) project_id = std::getenv("GCP_PROJECT_ID");
    if (!location) location = std::getenv("GCP_LOCATION");

    // Auto-set the V2 regional endpoint if location is available and endpoint not already set
    if (location && !switch_channel_get_variable(channel, "GOOGLE_SPEECH_TO_TEXT_URI")) {
        std::string regional_endpoint = std::string(location) + "-speech.googleapis.com";
        switch_channel_set_variable(channel, "GOOGLE_SPEECH_TO_TEXT_URI", regional_endpoint.c_str());
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO,
            "V2 API: Auto-set regional endpoint: %s\n", regional_endpoint.c_str());
    }

    // Now create the gRPC channel with the correct endpoint
    m_channel = create_grpc_channel(channel);
  	m_stub = Speech::NewStub(m_channel);

	auto streaming_config = m_request.mutable_streaming_config();

    // The parent of the recognizer must still be provided even if the wildcard
    // recognizer is used rather than a pre-prepared recognizer.
    std::string recognizer;
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_RECOGNIZER_PARENT")) {
        recognizer = var;
        recognizer += "/recognizers/";
    } else {
        // project_id and location already fetched above

        if (!project_id || !location) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_ERROR,
                "V2 API requires: GOOGLE_SPEECH_RECOGNIZER_PARENT channel variable, OR both GCP_PROJECT_ID and GCP_LOCATION (as channel variables or environment variables)\n");
            throw std::runtime_error("V2 API: Missing GCP_PROJECT_ID or GCP_LOCATION. Set GOOGLE_SPEECH_RECOGNIZER_PARENT=projects/{project}/locations/{location} or set GCP_PROJECT_ID and GCP_LOCATION");
        }

        recognizer = "projects/";
        recognizer += project_id;
        recognizer += "/locations/";
        recognizer += location;
        recognizer += "/recognizers/";

        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO,
            "V2 API: Using recognizer path: %s_\n", recognizer.c_str());
    }

    // Use the recognizer specified in the variable or just use the wildcard if this is not set.
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_RECOGNIZER_ID")) {
        recognizer += var;
    } else {
        recognizer += "_";

        // When using the wildcard recognizer "_", we must set config_mask to "*"
        // to tell the API to use our provided config completely (not merge with defaults)
        streaming_config->mutable_config_mask()->add_paths("*");
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2 API: set config_mask=* for wildcard recognizer\n");

        RecognitionConfig* config = streaming_config->mutable_config();
        config->add_language_codes(lang);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "transcribe language %s\n", lang);
        
        // alternative language
        if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES")) {
            char *alt_langs[3] = { 0 };
            int argc = switch_separate_string((char *) var, ',', alt_langs, 3);
            for (int i = 0; i < argc; i++) {
                config->add_language_codes(alt_langs[i]);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "added alternative lang %s\n", alt_langs[i]);
            }
        }

        config->mutable_explicit_decoding_config()->set_sample_rate_hertz(config_sample_rate);
        config->mutable_explicit_decoding_config()->set_encoding(ExplicitDecodingConfig_AudioEncoding_LINEAR16);

        // number of channels in the audio stream (default: 1)
        // N.B. It is essential to set this configuration value in v2 even if it doesn't deviate from the default.
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: channels=%d, separate_recognition=%d\n", channels, separate_recognition);

        config->mutable_explicit_decoding_config()->set_audio_channel_count(channels);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: set audio_channel_count=%d\n", channels);

        if (channels > 1) {
            // Auto-enable separate recognition per channel when stereo mode is used
            // This ensures proper per-channel transcription with channel_tag in results
            // (matching AWS Transcribe's enable_channel_identification behavior)
            config->mutable_features()->set_multi_channel_mode(RecognitionFeatures_MultiChannelMode_SEPARATE_RECOGNITION_PER_CHANNEL);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, 
                "V2 API: multi_channel_mode=SEPARATE_RECOGNITION_PER_CHANNEL (auto-enabled for stereo, channel_tag will map: 1=caller/ch_0, 2=agent/ch_1)\n");
            
            // Auto-enable voice activity events for stereo mode to help diagnose channel timing
            // This sends SPEECH_ACTIVITY_BEGIN and SPEECH_ACTIVITY_END events
            streaming_config->mutable_streaming_features()->set_enable_voice_activity_events(true);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, 
                "V2 API: enable_voice_activity_events=true (auto-enabled for stereo for timing diagnostics)\n");
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2 API: mono mode, channels=1\n");
        }

        // max alternatives
        if (max_alternatives > 1) {
            config->mutable_features()->set_max_alternatives(max_alternatives);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "max_alternatives %d\n", max_alternatives);
        }

        // profanity filter
        if (profanity_filter == 1) {
            config->mutable_features()->set_profanity_filter(true);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "profanity_filter\n");
        }

        // enable word offsets
        if (word_time_offset == 1) {
            config->mutable_features()->set_enable_word_time_offsets(true);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_word_time_offsets\n");
        }

        // enable automatic punctuation
        if (punctuation == 1) {
            config->mutable_features()->set_enable_automatic_punctuation(true);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enable_automatic_punctuation\n");
        }
        else {
            config->mutable_features()->set_enable_automatic_punctuation(false);
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "disable_automatic_punctuation\n");
        }

        // speech model
        // V2 API models: chirp_3, chirp_2, chirp_1, long, telephony, medical_conversation, medical_dictation
        // Use 'telephony' model by default (optimized for phone calls)
        const char* selected_model = model;
        if (model == NULL) {
            selected_model = "telephony";  // Default to 'telephony' model for phone calls
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Auto-selected model 'telephony' for phone calls\n");
        } else {
            // Map v1 model names to v2 equivalents for convenience
            if (strcmp(model, "phone_call") == 0) {
                selected_model = "telephony";
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Mapped 'phone_call' → 'telephony'\n");
            } else if (strcmp(model, "command_and_search") == 0) {
                selected_model = "chirp_3";
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Mapped 'command_and_search' → 'chirp_3'\n");
            } else if (strcmp(model, "video") == 0) {
                selected_model = "chirp_3";
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Mapped 'video' → 'chirp_3'\n");
            } else if (strcmp(model, "default") == 0) {
                selected_model = "telephony";
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Mapped 'default' → 'telephony'\n");
            } else if (strcmp(model, "latest_long") == 0) {
                selected_model = "chirp_3";
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Mapped 'latest_long' → 'chirp_3'\n");
            } else if (strcmp(model, "latest_short") == 0) {
                selected_model = "chirp_3";
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Mapped 'latest_short' → 'chirp_3'\n");
            }
        }

        config->set_model(selected_model);
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "V2 API: Using model '%s'\n", selected_model);

        // hints  
        if (hints != NULL) {
            auto* adaptation = config->mutable_adaptation();
            auto* phrase_set = adaptation->add_phrase_sets()->mutable_inline_phrase_set();
            google_speech_configure_grammar_hints(m_session, channel, hints, phrase_set);
        }

        // the rest of config comes from channel vars

        // speaker diarization
        // N.B. At the moment there does not seem to be any combination of model, language and location which supports diarization for STT v2.
        // See https://stackoverflow.com/questions/76779418/speaker-diarization-is-disabled-even-for-supported-languages-in-google-speech-to
        if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION")) {
            auto* diarization_config = config->mutable_features()->mutable_diarization_config();
            // There is no enable function in v2
            // diarization_config->set_enable_speaker_diarization(true);
            // switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "enabling speaker diarization\n", var);
            if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT")) {
                int count = std::max(atoi(var), 1);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting min speaker count to %d\n", count);
                diarization_config->set_min_speaker_count(count);
            }
            if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT")) {
                int count = std::max(atoi(var), 2);
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting max speaker count to %d\n", count);
                diarization_config->set_max_speaker_count(count);
            }
        }
        if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_TRANSCRIPTION_NORMALIZATION")) {
          // parse JSON string
        cJSON *json_array = cJSON_Parse(var);

        int array_size = cJSON_GetArraySize(json_array);

        for(int i=0; i<array_size; i++) {
            cJSON* json_item = cJSON_GetArrayItem(json_array, i);

            auto entry = config->mutable_transcript_normalization()->add_entries();

            std::string search_string = cJSON_GetObjectItem(json_item, "search")->valuestring;
            std::string replacement_string = cJSON_GetObjectItem(json_item, "replace")->valuestring;
            bool case_sensitive = cJSON_GetObjectItem(json_item, "case_sensitive")->valueint != 0;

            entry->set_search(search_string);
            entry->set_replace(replacement_string);
            entry->set_case_sensitive(case_sensitive);

            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG,
              "TRANSCRIPTION_NORMALIZATION search %s, replace %s, set_case_sensitive %d\n", search_string.c_str(), replacement_string.c_str(), case_sensitive);
        }
        // clean json
        cJSON_Delete(json_array);
      }
    }
    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_START_TIMEOUT_MS")) {
      auto ms = atoi(var);
      streaming_config->mutable_streaming_features()->mutable_voice_activity_timeout()->mutable_speech_start_timeout()->set_nanos(ms * 1000000);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting speech_start_timeout to %d milliseconds\n", ms);
    }

    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_END_TIMEOUT_MS")) {
      auto ms = atoi(var);
      streaming_config->mutable_streaming_features()->mutable_voice_activity_timeout()->mutable_speech_end_timeout()->set_nanos(ms * 1000000);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting speech_end_timeout to %d milliseconds\n", ms);
    }

    if (var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_VOICE_ACTIVITY_EVENTS")) {
      bool enabled = !strcmp(var, "true") ? 1 : 0;
      streaming_config->mutable_streaming_features()->set_enable_voice_activity_events(enabled);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "setting enable_voice_activity_events to %d \n", enabled);
    }

    m_request.set_recognizer(recognizer);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "using recognizer: %s\n", recognizer.c_str());

    // This must be set whether a recognizer id is provided or not, because it cannot be configured as part of a recognizer.
    if (interim > 0) {
        streaming_config->mutable_streaming_features()->set_interim_results(interim > 0);
    }
}

static void *SWITCH_THREAD_FUNC grpc_read_thread(switch_thread_t *thread, void *obj) {
  static int count;
	struct cap_cb *cb = (struct cap_cb *) obj;
	GStreamer_V2* streamer = (GStreamer_V2 *) cb->streamer;

  bool connected = streamer->waitForConnect();
  if (!connected) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "google transcribe grpc read thread exiting since we didn't connect\n") ;
    return nullptr;
  }

  // Read responses.
  StreamingRecognizeResponse response;
  while (streamer->read(&response)) {  // Returns false when no more to read.
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (!session) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "grpc_read_thread: session %s is gone!\n", cb->sessionId) ;
      return nullptr;
    }
    count++;
    
    if (cb->play_file == 1){
      cb->responseHandler(session, "play_interrupt", cb->bugname);
    }
    
    for (int r = 0; r < response.results_size(); ++r) {
      auto result = response.results(r);
      int channel_tag = result.channel_tag();
      
      // Track first result timing per channel for latency analysis
      switch_time_t now = switch_time_now();
      if (channel_tag == 1 && !cb->got_first_result_ch1) {
        cb->got_first_result_ch1 = 1;
        cb->first_result_time_ch1 = now;
        double latency_ms = (now - cb->stream_start_time) / 1000.0;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
          "[CHANNEL-LATENCY] First result for channel_tag=1 (caller): latency=%.0fms from stream start\n", latency_ms);
        
        // If ch2 was already received, report the gap
        if (cb->got_first_result_ch2) {
          double diff_ms = (now - cb->first_result_time_ch2) / 1000.0;
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, 
            "[CHANNEL-LATENCY] Channel 1 arrived %.0fms AFTER channel 2 - investigating Google multi-channel processing\n", diff_ms);
        }
      } else if (channel_tag == 2 && !cb->got_first_result_ch2) {
        cb->got_first_result_ch2 = 1;
        cb->first_result_time_ch2 = now;
        double latency_ms = (now - cb->stream_start_time) / 1000.0;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
          "[CHANNEL-LATENCY] First result for channel_tag=2 (agent): latency=%.0fms from stream start\n", latency_ms);
        
        // If ch1 was already received, report the gap
        if (cb->got_first_result_ch1) {
          double diff_ms = (now - cb->first_result_time_ch1) / 1000.0;
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
            "[CHANNEL-LATENCY] Channel 2 arrived %.0fms after channel 1\n", diff_ms);
        }
      }
      
      // Debug logging for channel_tag (like V1)
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "V2 API: result[%d] channel_tag=%d, is_final=%s, stability=%.2f\n",
        r, channel_tag, result.is_final() ? "true" : "false", result.stability());
      
      cJSON * jResult = cJSON_CreateObject();
      cJSON * jAlternatives = cJSON_CreateArray();
      cJSON * jStability = cJSON_CreateNumber(result.stability());
      cJSON * jIsFinal = cJSON_CreateBool(result.is_final());
      cJSON * jLanguageCode = cJSON_CreateString(result.language_code().c_str());
      cJSON * jChannelTag = cJSON_CreateNumber(result.channel_tag());

      auto duration = result.result_end_offset();
      int32_t seconds = duration.seconds();
      int64_t nanos = duration.nanos();
      int span = (int) trunc(seconds * 1000. + ((float) nanos / 1000000.));
      cJSON * jResultEndTime = cJSON_CreateNumber(span);

      cJSON_AddItemToObject(jResult, "stability", jStability);
      cJSON_AddItemToObject(jResult, "is_final", jIsFinal);
      cJSON_AddItemToObject(jResult, "alternatives", jAlternatives);
      cJSON_AddItemToObject(jResult, "language_code", jLanguageCode);
      cJSON_AddItemToObject(jResult, "channel_tag", jChannelTag);
      cJSON_AddItemToObject(jResult, "result_end_time", jResultEndTime);

      if (result.alternatives_size() == 0) {
        SpeechRecognitionAlternative alternative;
        alternative.set_confidence(0.0);
        alternative.set_transcript("");
        *result.add_alternatives() = alternative;
      }
      for (int a = 0; a < result.alternatives_size(); ++a) {
        auto alternative = result.alternatives(a);
        cJSON* jAlt = cJSON_CreateObject();
        cJSON* jConfidence = cJSON_CreateNumber(alternative.confidence());
        cJSON* jTranscript = cJSON_CreateString(alternative.transcript().c_str());
        cJSON_AddItemToObject(jAlt, "confidence", jConfidence);
        cJSON_AddItemToObject(jAlt, "transcript", jTranscript);

        if (alternative.words_size() > 0) {
          cJSON * jWords = cJSON_CreateArray();
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: %d words\n", alternative.words_size()) ;
          for (int b = 0; b < alternative.words_size(); b++) {
            auto words = alternative.words(b);
            cJSON* jWord = cJSON_CreateObject();
            cJSON_AddItemToObject(jWord, "word", cJSON_CreateString(words.word().c_str()));
            if (words.has_start_offset()) {
              cJSON_AddItemToObject(jWord, "start_offset", cJSON_CreateNumber(words.start_offset().seconds()));
            }
            if (words.has_end_offset()) {
              cJSON_AddItemToObject(jWord, "end_offset", cJSON_CreateNumber(words.end_offset().seconds()));
            }
            auto speaker_label = words.speaker_label();
            if (speaker_label.size() > 0) {
              cJSON_AddItemToObject(jWord, "speaker_label", cJSON_CreateString(speaker_label.c_str()));
            }
            float confidence = words.confidence();
            if (confidence > 0.0) {
              cJSON_AddItemToObject(jWord, "confidence", cJSON_CreateNumber(confidence));
            }

            cJSON_AddItemToArray(jWords, jWord);
          }
          cJSON_AddItemToObject(jAlt, "words", jWords);
        }
        cJSON_AddItemToArray(jAlternatives, jAlt);
      }

      char* json = cJSON_PrintUnformatted(jResult);
      cb->responseHandler(session, (const char *) json, cb->bugname);
      free(json);

      cJSON_Delete(jResult);
    }

    auto speech_event_type = response.speech_event_type();
    if (speech_event_type == StreamingRecognizeResponse_SpeechEventType_END_OF_SINGLE_UTTERANCE) {
      // we only get this when we have requested it, and recognition stops after we get this
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: got end_of_utterance\n") ;
      cb->got_end_of_utterance = 1;
      cb->responseHandler(session, "end_of_utterance", cb->bugname);
      if (cb->wants_single_utterance) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: sending writesDone because we want only a single utterance\n") ;
        streamer->writesDone();
      }
    }
    else if (speech_event_type == StreamingRecognizeResponse_SpeechEventType_SPEECH_ACTIVITY_BEGIN) {
      // Log with timing info - speech_event_offset tells us when in the audio stream this occurred
      auto offset = response.speech_event_offset();
      int32_t offset_ms = (int32_t)(offset.seconds() * 1000 + offset.nanos() / 1000000);
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
        "V2 API: SPEECH_ACTIVITY_BEGIN at offset=%dms (voice detected in audio stream)\n", offset_ms);
      cb->responseHandler(session, "speech_activity_begin", cb->bugname);
    }
    else if (speech_event_type == StreamingRecognizeResponse_SpeechEventType_SPEECH_ACTIVITY_END) {
      auto offset = response.speech_event_offset();
      int32_t offset_ms = (int32_t)(offset.seconds() * 1000 + offset.nanos() / 1000000);
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
        "V2 API: SPEECH_ACTIVITY_END at offset=%dms (silence detected in audio stream)\n", offset_ms);
      cb->responseHandler(session, "speech_activity_end", cb->bugname);
    }
    switch_core_session_rwunlock(session);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: got %d responses\n", response.results_size());
  }

  {
    switch_core_session_t* session = switch_core_session_locate(cb->sessionId);
    if (session) {
      grpc::Status status = streamer->finish();
      // TODO: This works on the same principle as that used in the v1 equivalent, in that we search for the textual
      // error message to determine whether the cause of the problem is the expiration of the session.
      // It would be better if we could find a more reliable way of detecting this.
      if (10 == status.error_code()) {
        if (std::string::npos != status.error_message().find("Max duration of 5 minutes reached")) {
          cb->responseHandler(session, "max_duration_exceeded", cb->bugname);
        }
        else {
          cb->responseHandler(session, "no_audio", cb->bugname);
        }
      }
      else if (status.error_code() != 0) {
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "type", "error");
        cJSON_AddStringToObject(json, "error_cause", "stream_close");
        cJSON_AddItemToObject(json, "error_code", cJSON_CreateNumber(status.error_code()));
        cJSON_AddStringToObject(json, "error_message", status.error_message().c_str());
        char* jsonString = cJSON_PrintUnformatted(json);
        cb->responseHandler(session, jsonString, cb->bugname);
        free(jsonString);
        cJSON_Delete(json);
      }
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "grpc_read_thread: finish() status %s (%d)\n", status.error_message().c_str(), status.error_code()) ;
      switch_core_session_rwunlock(session);
    }
  }
  return nullptr;
}

template <>
bool GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub>::write(void* data, uint32_t datalen) {
	// Wait for connection before sending
	if (!m_connected) {
		return true;
	}
	
	// Add current audio data to accumulation buffer
	m_accumBuffer.insert(m_accumBuffer.end(), (uint8_t*)data, (uint8_t*)data + datalen);
	
	// Only send when we have accumulated enough data (100ms target = 3200 bytes for 8kHz stereo)
	// This is critical for Google V2 multichannel: smaller chunks cause channel 1 to be delayed
	// Python reference implementation uses 100ms chunks and works correctly
	if (m_accumBuffer.size() >= ACCUMULATE_TARGET) {
		m_request.clear_streaming_config();
		m_request.set_audio(m_accumBuffer.data(), m_accumBuffer.size());
		bool ok = m_streamer->Write(m_request);
		if (!ok) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
				"V2 API: Failed to write accumulated audio chunk (%zu bytes)\n", m_accumBuffer.size());
			return false;
		}
		m_accumBuffer.clear();
	}
	
	return true;
}

template <>
void GStreamer<StreamingRecognizeRequest, StreamingRecognizeResponse, Speech::Stub>::flushAccumulatedAudio() {
	// Flush any remaining accumulated audio before ending the stream
	// This ensures we don't lose the final portion of speech
	if (m_connected && !m_writesDone && m_accumBuffer.size() > 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
			"V2 API: Flushing final accumulated audio (%zu bytes) before writesDone\n", m_accumBuffer.size());
		m_request.clear_streaming_config();
		m_request.set_audio(m_accumBuffer.data(), m_accumBuffer.size());
		m_streamer->Write(m_request);
		m_accumBuffer.clear();
	}
}

extern "C" {

    switch_status_t google_speech_session_cleanup_v2(switch_core_session_t *session, int channelIsClosing, switch_media_bug_t *bug) {
      return google_speech_session_cleanup<GStreamer_V2>(session, channelIsClosing, bug);
    }

    switch_bool_t google_speech_frame_v2(switch_media_bug_t *bug, void* user_data) {
      return google_speech_frame<GStreamer_V2>(bug, user_data);
    }

    switch_status_t google_speech_session_init_v2(switch_core_session_t *session, responseHandler_t responseHandler,
		  uint32_t to_rate, uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char *bugname, int single_utterance,
		  int separate_recognition, int max_alternatives, int profanity_filter, int word_time_offset, int punctuation, const char* model, int enhanced, 
		  const char* hints, char* play_file, void **ppUserData) {
      return google_speech_session_init<GStreamer_V2>(session, responseHandler, grpc_read_thread, to_rate, samples_per_second, channels,
        lang, interim, bugname, single_utterance, separate_recognition, max_alternatives, profanity_filter,
        word_time_offset, punctuation, model, enhanced, hints, play_file, ppUserData);
    }

}
