#include <switch.h>
#include <switch_json.h>
#include <string.h>
#include <string>
#include <mutex>
#include <thread>
#include <list>
#include <algorithm>
#include <functional>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <regex>
#include <iostream>
#include <unordered_map>

#include "mod_deepgram_transcribe.h"
#include "audio_pipe.hpp"
#include "memory_pool.hpp"
#include "dg_session.hpp"
#include "context_manager.hpp"

#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000  320 /*which means each 20ms frame as 320 bytes at 8 khz (1 channel only)*/

/* ============================================================================
 * SPEEX RESAMPLER QUALITY CONFIGURATION
 * 
 * Quality levels (0-10):
 *   0  = Lowest quality, fastest
 *   2  = SWITCH_RESAMPLE_QUALITY (FreeSWITCH default) - LOW
 *   3  = SPEEX_RESAMPLER_QUALITY_VOIP - Good for VoIP
 *   4  = SPEEX_RESAMPLER_QUALITY_DEFAULT - Recommended
 *   5  = SPEEX_RESAMPLER_QUALITY_DESKTOP - High quality
 *   10 = SPEEX_RESAMPLER_QUALITY_MAX - Best quality, slowest
 * 
 * For speech transcription, quality 4-5 is recommended for accuracy.
 * Higher quality = better transcription but more CPU.
 * 
 * Set via environment: MOD_DEEPGRAM_RESAMPLE_QUALITY=5
 * ============================================================================ */
#define DEFAULT_RESAMPLE_QUALITY 2  // FreeSWITCH default - increase to 3-5 if transcription quality issues

namespace {
  static bool hasDefaultCredentials = false;
  static const char* defaultApiKey = nullptr;
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
  
  /* Resampler quality (0-10, default 5 for speech transcription) */
  static const char *requestedResampleQuality = std::getenv("MOD_DEEPGRAM_RESAMPLE_QUALITY");
  static int nResampleQuality = std::max(0, std::min(requestedResampleQuality ? ::atoi(requestedResampleQuality) : DEFAULT_RESAMPLE_QUALITY, 10));
  
  /* ============================================================================
   * LWS SERVICE THREAD CONFIGURATION (HIGH SCALE)
   * 
   * Each LWS service thread handles WebSocket I/O for multiple connections.
   * Scaling guide (from mod_google_transcribe_async):
   *   - 100 calls:   1-2 threads (low load)
   *   - 500 calls:   2-3 threads (medium load)  
   *   - 1000 calls:  3-4 threads (high load)
   *   - 2000+ calls: 4-5 threads (very high load)
   * 
   * Each call generates ~50 WebSocket writes/sec (audio frames).
   * Each thread can handle ~2000-3000 writes/sec efficiently.
   * Formula: threads = ceil(expected_calls * 50 / 2500)
   * 
   * Set via environment: MOD_AUDIO_FORK_SERVICE_THREADS=3
   * ============================================================================ */
  static const char *requestedNumServiceThreads = std::getenv("MOD_AUDIO_FORK_SERVICE_THREADS");
  // Default: 3 threads (supports ~1500 concurrent calls)
  // Max: 5 threads (limited by LWS context array size)
  static unsigned int nServiceThreads = std::max(1, std::min(requestedNumServiceThreads ? ::atoi(requestedNumServiceThreads) : 3, 5));
  static unsigned int idxCallCount = 0;
  static uint32_t playCount = 0;

  /* deepgram model / tier defaults by language */
  struct LanguageInfo {
      std::string tier;
      std::string model;
  };

  static const std::unordered_map<std::string, LanguageInfo> languageLookupTable = {
      {"zh", {"base", "general"}},
      {"zh-CN", {"base", "general"}},
      {"zh-TW", {"base", "general"}},
      {"da", {"enhanced", "general"}},
      {"en", {"nova", "phonecall"}},
      {"en-US", {"nova", "phonecall"}},
      {"en-AU", {"nova", "general"}},
      {"en-GB", {"nova", "general"}},
      {"en-IN", {"nova", "general"}},
      {"en-NZ", {"nova", "general"}},
      {"nl", {"enhanced", "general"}},
      {"fr", {"enhanced", "general"}},
      {"fr-CA", {"base", "general"}},
      {"de", {"enhanced", "general"}},
      {"hi", {"enhanced", "general"}},
      {"hi-Latn", {"base", "general"}},
      {"id", {"base", "general"}},
      {"ja", {"enhanced", "general"}},
      {"ko", {"enhanced", "general"}},
      {"no", {"enhanced", "general"}},
      {"pl", {"enhanced", "general"}},
      {"pt", {"enhanced", "general"}},
      {"pt-BR", {"enhanced", "general"}},
      {"pt-PT", {"enhanced", "general"}},
      {"ru", {"base", "general"}},
      {"es", {"nova", "general"}},
      {"es-419", {"nova", "general"}},
      {"sv", {"enhanced", "general"}},
      {"ta", {"enhanced", "general"}},
      {"tr", {"base", "general"}},
      {"uk", {"base", "general"}}
  };

  static bool getLanguageInfo(const std::string& language, LanguageInfo& info) {
      auto it = languageLookupTable.find(language);
      if (it != languageLookupTable.end()) {
          info = it->second;
          return true;
      }
      return false;
  }

  static const char* emptyTranscript = "{\"alternatives\":[{\"transcript\":\"\",\"confidence\":0.0,\"words\":[]}]}";

  static void reaper(private_t *tech_pvt) {
    std::shared_ptr<deepgram::AudioPipe> pAp;
    pAp.reset((deepgram::AudioPipe *)tech_pvt->pAudioPipe);
    tech_pvt->pAudioPipe = nullptr;

    std::thread t([pAp, tech_pvt]{
      pAp->finish();
      pAp->waitForClose();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "%s (%u) got remote close\n", tech_pvt->sessionId, tech_pvt->id);
    });
    t.detach();
  }

  static void destroy_tech_pvt(private_t *tech_pvt) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s (%u) destroy_tech_pvt\n", tech_pvt->sessionId, tech_pvt->id);
    if (tech_pvt) {
      // Log final resampler statistics before cleanup
      if (tech_pvt->resampler_frames_processed > 0) {
        switch_time_t now = switch_time_now();
        double elapsed_secs = (now - tech_pvt->resampler_start_time) / 1000000.0;
        double fps = tech_pvt->resampler_frames_processed / elapsed_secs;
        double mb_written = tech_pvt->resampler_bytes_written / (1024.0 * 1024.0);
        double kbps = (tech_pvt->resampler_bytes_written * 8.0) / (elapsed_secs * 1000.0);

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
          "[RESAMPLER-FINAL] (%u) %s: Session complete - frames=%lu, samples_in=%lu, samples_out=%lu, "
          "bytes=%.2fMB, avg_fps=%.1f, duration=%.1fs, bitrate=%.1fkbps, mode=%s\n",
          tech_pvt->id, tech_pvt->bugname,
          (unsigned long)tech_pvt->resampler_frames_processed,
          (unsigned long)tech_pvt->resampler_samples_in,
          (unsigned long)tech_pvt->resampler_samples_out,
          mb_written, fps, elapsed_secs, kbps,
          tech_pvt->resampler ?
            (tech_pvt->resampler_source_rate < tech_pvt->resampler_target_rate ? "UPSAMPLE" : "DOWNSAMPLE")
            : "PASSTHROUGH");
      }

      if (tech_pvt->pAudioPipe) {
        deepgram::AudioPipe* p = (deepgram::AudioPipe *) tech_pvt->pAudioPipe;
        // Release AudioPipe back to pool (HIGH SCALE OPTIMIZATION)
        deepgram::AudioPipePool::release(p);
        tech_pvt->pAudioPipe = nullptr;
      }
      if (tech_pvt->resampler) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
          "[RESAMPLER-CLEANUP] (%u) %s: Destroying resampler %dHz->%dHz\n",
          tech_pvt->id, tech_pvt->bugname, 
          tech_pvt->resampler_source_rate, tech_pvt->resampler_target_rate);
        speex_resampler_destroy(tech_pvt->resampler);
        tech_pvt->resampler = NULL;
      }

      /*
      if (tech_pvt->vad) {
        switch_vad_destroy(&tech_pvt->vad);
        tech_pvt->vad = nullptr;
      }
      */
      
      // Release private_t back to pool (HIGH SCALE OPTIMIZATION)
      deepgram::PrivateDataPool::Release(tech_pvt);
    }
  }

  std::string encodeURIComponent(std::string decoded)
  {

      std::ostringstream oss;
      std::regex r("[!'\\(\\)*-.0-9A-Za-z_~:]");

      for (char &c : decoded)
      {
          if (std::regex_match((std::string){c}, r))
          {
              oss << c;
          }
          else
          {
              oss << "%" << std::uppercase << std::hex << (0xff & c);
          }
      }
      return oss.str();
  }

  std::string& constructPath(switch_core_session_t* session, std::string& path, 
    int sampleRate, int channels, const char* language, int interim) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    const char *var ;
    const char *model = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_MODEL");
    const char *customModel = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_CUSTOM_MODEL");
    const char *tier = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_TIER") ;
    std::ostringstream oss;
    LanguageInfo info;

    oss << "/v1/listen?";

    if (!tier && !model && !customModel) {
      /* make best choice by language */
      if (getLanguageInfo(language, info)) {
        oss << "tier=" << info.tier << "&model=" << info.model;
      }
      else {
        oss << "tier=base&model=general"; // most widely supported, though not ideal
      }
    }
    else {
      if (tier) oss << "tier=" << tier;
      if (model) oss << "&model=" << model;
      if (customModel) oss << "&model=" << customModel;
    }

    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_MODEL_VERSION")) {
     oss <<  "&version";
     oss <<  var;
    }
    oss <<  "&language=";
    oss <<  language;

    if (channels == 2) {
     oss <<  "&multichannel=true";
     oss <<  "&channels=2";
    }

    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_ENABLE_SMART_FORMAT")) {
     oss <<  "&smart_format=true";
     oss <<  "&no_delay=true";
     /**
      * see: https://github.com/orgs/deepgram/discussions/384
      * 
      */
    }
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION")) {
     oss <<  "&punctuate=true";
    }
    if (switch_true(switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_PROFANITY_FILTER"))) {
     oss <<  "&profanity_filter=true";
    }
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_REDACT")) {
     oss <<  "&redact=";
     oss <<  var;
    }
    if (switch_true(switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_DIARIZE"))) {
     oss <<  "&diarize=true";
      if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_DIARIZE_VERSION")) {
       oss <<  "&diarize_version=";
       oss <<  var;
      }
    }
    if (switch_true(switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_NER"))) {
     oss <<  "&ner=true";
    }
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_ALTERNATIVES")) {
     oss <<  "&alternatives=";
     oss <<  var;
    }
    if (switch_true(switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_NUMERALS"))) {
     oss <<  "&numerals=true";
    }

		const char* hints = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_SEARCH");
		if (hints) {
			char *phrases[500] = { 0 };
      int argc = switch_separate_string((char *)hints, ',', phrases, 500);
      for (int i = 0; i < argc; i++) {
       oss <<  "&search=";
       oss <<  encodeURIComponent(phrases[i]);
      }
		}
		const char* keywords = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_KEYWORDS");
		if (keywords) {
			char *phrases[500] = { 0 };
      int argc = switch_separate_string((char *)keywords, ',', phrases, 500);
      for (int i = 0; i < argc; i++) {
       oss <<  "&keywords=";
       oss <<  encodeURIComponent(phrases[i]);
      }
		}
		const char* replace = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_REPLACE");
		if (replace) {
			char *phrases[500] = { 0 };
      int argc = switch_separate_string((char *)replace, ',', phrases, 500);
      for (int i = 0; i < argc; i++) {
       oss <<  "&replace=";
       oss <<  encodeURIComponent(phrases[i]);
      }
		}
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_TAG")) {
     oss <<  "&tag=";
     oss <<  var;
    }
    if (interim) {
     oss <<  "&interim_results=true";
    }
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_ENDPOINTING")) {
      oss <<  "&endpointing=";
      oss <<  var;
    }
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_UTTERANCE_END_MS")) {
      oss <<  "&utterance_end_ms=";
      oss <<  var;
    }
    if (var = switch_channel_get_variable(channel, "DEEPGRAM_SPEECH_VAD_TURNOFF")) {
      oss <<  "&vad_turnoff=";
      oss <<  var;
    }
   oss <<  "&encoding=linear16";
   oss <<  "&sample_rate=" << sampleRate;
   path = oss.str();
   return path;
  }

  static void eventCallback(const char* sessionId, deepgram::AudioPipe::NotifyEvent_t event, const char* message, bool finished) {
    switch_core_session_t* session = switch_core_session_locate(sessionId);
    if (session) {
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
      if (bug) {
        private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
        if (tech_pvt) {
          switch (event) {
            case deepgram::AudioPipe::CONNECT_SUCCESS:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "connection successful\n");
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_SUCCESS, NULL, tech_pvt->bugname, finished);
            break;
            case deepgram::AudioPipe::CONNECT_FAIL:
            {
              // first thing: we can no longer access the AudioPipe
              std::stringstream json;
              json << "{\"reason\":\"" << message << "\"}";
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_CONNECT_FAIL, (char *) json.str().c_str(), tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "connection failed: %s\n", message);
            }
            break;
            case deepgram::AudioPipe::CONNECTION_DROPPED:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_DISCONNECT, NULL, tech_pvt->bugname, finished);
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection dropped from far end\n");
            break;
            case deepgram::AudioPipe::CONNECTION_CLOSED_GRACEFULLY:
              // first thing: we can no longer access the AudioPipe
              tech_pvt->pAudioPipe = nullptr;
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection closed gracefully\n");
            break;
            case deepgram::AudioPipe::MESSAGE:
              if( strstr(message, emptyTranscript)) {
                // Silently discard empty transcripts - no logging needed
              }
              else {
                // DEBUG: Log raw Deepgram message to trace all incoming data
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                  "RAW DEEPGRAM: %s\n", message);
                
                // OPTIMIZED: Parse JSON ONCE and extract all fields
                cJSON* msgJson = cJSON_Parse(message);
                if (msgJson) {
                  transcript_data_t td;
                  transcript_data_init(&td);
                  td.raw_json = message;
                  
                  // Extract is_final and speech_final flags
                  cJSON* is_final = cJSON_GetObjectItem(msgJson, "is_final");
                  cJSON* speech_final = cJSON_GetObjectItem(msgJson, "speech_final");
                  td.is_final = is_final && cJSON_IsTrue(is_final);
                  td.speech_final = speech_final && cJSON_IsTrue(speech_final);
                  
                  // Get channel index for speaker mapping
                  cJSON* channel_index = cJSON_GetObjectItem(msgJson, "channel_index");
                  if (channel_index && cJSON_IsArray(channel_index) && cJSON_GetArraySize(channel_index) > 0) {
                    cJSON* idx = cJSON_GetArrayItem(channel_index, 0);
                    if (idx && cJSON_IsNumber(idx)) td.channel_index = (int)idx->valuedouble;
                  }
                  
                  // Extract transcript text and confidence
                  cJSON* channel_obj = cJSON_GetObjectItem(msgJson, "channel");
                  if (channel_obj) {
                    cJSON* alts = cJSON_GetObjectItem(channel_obj, "alternatives");
                    if (alts && cJSON_IsArray(alts) && cJSON_GetArraySize(alts) > 0) {
                      cJSON* first = cJSON_GetArrayItem(alts, 0);
                      cJSON* t = cJSON_GetObjectItem(first, "transcript");
                      if (t && cJSON_IsString(t)) {
                        const char* text = cJSON_GetStringValue(t);
                        if (text && strlen(text) > 0) {
                          strncpy(td.transcript, text, sizeof(td.transcript) - 1);
                          td.transcript[sizeof(td.transcript) - 1] = '\0';
                          td.has_transcript = true;
                        }
                      }
                      cJSON* conf = cJSON_GetObjectItem(first, "confidence");
                      if (conf && cJSON_IsNumber(conf)) td.confidence = conf->valuedouble;
                    }
                  }
                  
                  // Extract timing info
                  cJSON* start_obj = cJSON_GetObjectItem(msgJson, "start");
                  cJSON* duration_obj = cJSON_GetObjectItem(msgJson, "duration");
                  if (start_obj && cJSON_IsNumber(start_obj)) td.start = start_obj->valuedouble;
                  if (duration_obj && cJSON_IsNumber(duration_obj)) td.duration = duration_obj->valuedouble;
                  
                  // Call optimized response handler with pre-parsed data
                  responseHandlerParsed_t handler = (responseHandlerParsed_t)tech_pvt->responseHandler;
                  handler(session, TRANSCRIBE_EVENT_RESULTS, message, tech_pvt->bugname, finished, &td);
                  
                  // Log transcripts at INFO level
                  if (td.has_transcript) {
                    if (td.is_final || td.speech_final) {
                      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                        "\033[32m[FINAL%s]\033[0m [CH%d] %s\n", 
                        td.speech_final ? "+SF" : "", td.channel_index, td.transcript);
                    } else {
                      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
                        "\033[33m[INTERIM]\033[0m [CH%d] %s\n", td.channel_index, td.transcript);
                    }
                  }
                  cJSON_Delete(msgJson);
                } else {
                  // JSON parse failed - fall back to legacy handler
                  tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_RESULTS, message, tech_pvt->bugname, finished);
                }
              }
            break;

            default:
              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_NOTICE, "got unexpected msg from deepgram %d:%s\n", event, message);
              break;
          }
        }
      }
      switch_core_session_rwunlock(session);
    }
  }
  static void emit_metadata_event(switch_core_session_t* session, const char* metadata, const char* event_name, const char* bugname) {
    if (metadata && strlen(metadata) > 0) {
      switch_event_t *event;
      switch_channel_t *channel = switch_core_session_get_channel(session);
      switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, event_name);
      switch_channel_event_set_data(channel, event);
      switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "deepgram");
      switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
      switch_event_add_body(event, "%s", metadata);
      switch_event_fire(&event);
    }
  }

  switch_status_t fork_data_init(private_t *tech_pvt, switch_core_session_t *session,
    int sampling, int desiredSampling, int channels, char *lang, int interim,
    char* bugname, char* metadata, responseHandler_t responseHandler) {

    int err;
    switch_codec_implementation_t read_impl;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    switch_core_session_get_read_impl(session, &read_impl);

    memset(tech_pvt, 0, sizeof(private_t));
  
    std::string path;
    constructPath(session, path, desiredSampling, channels, lang, interim);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "path: %s\n", path.c_str());

    strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
    strncpy(tech_pvt->host, "api.deepgram.com", MAX_WS_URL_LEN);
    tech_pvt->port = 443;
    strncpy(tech_pvt->path, path.c_str(), MAX_PATH_LEN);
    strncpy(tech_pvt->bugname, bugname, MAX_BUG_LEN);
    if (metadata && strlen(metadata) > 0) {
      strncpy(tech_pvt->metadata, metadata, MAX_METADATA_LEN - 1);
      tech_pvt->metadata[MAX_METADATA_LEN - 1] = '\0';
    }
    tech_pvt->sampling = desiredSampling;
    tech_pvt->responseHandler = responseHandler;
    tech_pvt->channels = channels;
    tech_pvt->id = ++idxCallCount;
    tech_pvt->buffer_overrun_notified = 0;
    
    size_t buflen = LWS_PRE + (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 / RTP_PACKETIZATION_PERIOD * nAudioBufferSecs);

    const char* apiKey = switch_channel_get_variable(channel, "DEEPGRAM_API_KEY");
    if (!apiKey && defaultApiKey) apiKey = defaultApiKey;
    else if (!apiKey) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "no deepgram api key provided\n");
      return SWITCH_STATUS_FALSE;
    }

    // Use AudioPipe pool for high-scale operation (eliminates malloc in hot path)
    deepgram::AudioPipe* ap = deepgram::AudioPipePool::acquire(
      tech_pvt->sessionId, tech_pvt->host, tech_pvt->port, tech_pvt->path, 
      buflen, read_impl.decoded_bytes_per_packet, apiKey, 
      reinterpret_cast<void*>(eventCallback));
    if (!ap) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error allocating AudioPipe from pool\n");
      return SWITCH_STATUS_FALSE;
    }

    tech_pvt->pAudioPipe = static_cast<void *>(ap);

    switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));

    // Initialize resampler performance tracking stats
    tech_pvt->resampler_frames_processed = 0;
    tech_pvt->resampler_samples_in = 0;
    tech_pvt->resampler_samples_out = 0;
    tech_pvt->resampler_bytes_written = 0;
    tech_pvt->resampler_source_rate = sampling;
    tech_pvt->resampler_target_rate = desiredSampling;
    tech_pvt->resampler_start_time = switch_time_now();
    tech_pvt->resampler_last_log_time = tech_pvt->resampler_start_time;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
      "[RESAMPLER-INIT] (%u) %s: codec=%dHz, target=%dHz, channels=%d, direction=%s\n",
      tech_pvt->id, tech_pvt->bugname, sampling, desiredSampling, channels,
      sampling < desiredSampling ? "UPSAMPLE" :
      (sampling > desiredSampling ? "DOWNSAMPLE" : "PASSTHROUGH"));

    if (desiredSampling != sampling) {
      // Log detailed resampler configuration for scale monitoring
      // Quality setting: nResampleQuality (configurable via MOD_DEEPGRAM_RESAMPLE_QUALITY)
      // Memory per resampler: ~10-50KB depending on quality and channels
      // At 10k calls: ~100-500MB just for resamplers
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
        "[RESAMPLER-INIT] (%u) Initializing Speex resampler: %dHz -> %dHz (%d ch), quality=%d (0=low, 5=desktop, 10=max)\n",
        tech_pvt->id, sampling, desiredSampling, channels, nResampleQuality);
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
        "[RESAMPLER-INIT] (%u) Estimated memory: ~%dKB, ratio=%.3f, frame_in=%d samples, frame_out=%d samples\n",
        tech_pvt->id,
        (channels * 2 * 160 * (nResampleQuality + 1)) / 1024 + 10,  // Higher quality = more memory
        (float)desiredSampling / sampling,
        (sampling / 50),      // 20ms frame at source rate
        (desiredSampling / 50));  // 20ms frame at target rate

      tech_pvt->resampler = speex_resampler_init(channels, sampling, desiredSampling, nResampleQuality, &err);
      if (0 != err) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
          "[RESAMPLER-ERROR] (%u) Failed to initialize resampler: %s (code=%d)\n",
          tech_pvt->id, speex_resampler_strerror(err), err);
        return SWITCH_STATUS_FALSE;
      }
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
        "[RESAMPLER-INIT] (%u) Resampler initialized successfully at %p\n",
        tech_pvt->id, (void*)tech_pvt->resampler);
    }
    else {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
        "[RESAMPLER-INIT] (%u) Passthrough mode - no resampling needed (rate=%dHz)\n",
        tech_pvt->id, desiredSampling);
    }

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) fork_data_init\n", tech_pvt->id);

    return SWITCH_STATUS_SUCCESS;
  }

  void lws_logger(int level, const char *line) {
    switch_log_level_t llevel = SWITCH_LOG_DEBUG;

    switch (level) {
      case LLL_ERR: llevel = SWITCH_LOG_ERROR; break;
      case LLL_WARN: llevel = SWITCH_LOG_WARNING; break;
      case LLL_NOTICE: llevel = SWITCH_LOG_NOTICE; break;
      case LLL_INFO: llevel = SWITCH_LOG_INFO; break;
      break;
    }
	  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "%s\n", line);
  }
}


extern "C" {
  switch_status_t dg_transcribe_init() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_deepgram_transcribe: audio buffer (in secs):    %d secs\n", nAudioBufferSecs);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_deepgram_transcribe: lws service threads:       %d\n", nServiceThreads);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "mod_deepgram_transcribe: resample quality:          %d (0=low, 5=desktop, 10=max)\n", nResampleQuality);
 
    int logs = LLL_ERR | LLL_WARN | LLL_NOTICE | LLL_INFO | LLL_PARSER | LLL_HEADER | LLL_EXT | LLL_CLIENT  | LLL_LATENCY | LLL_DEBUG ;
    
    deepgram::AudioPipe::initialize(nServiceThreads, logs, lws_logger);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AudioPipe::initialize completed\n");

    // Initialize AudioPipe memory pool for high-scale operation
    const char* poolSizeEnv = std::getenv("MOD_DEEPGRAM_POOL_SIZE");
    size_t poolSize = poolSizeEnv ? std::atoi(poolSizeEnv) : 1000;  // Default 1K, can scale to 5K+
    if (deepgram::AudioPipePool::initialize(poolSize)) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
        "mod_deepgram_transcribe: AudioPipe pool initialized (capacity=%zu)\n", poolSize);
    } else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
        "mod_deepgram_transcribe: AudioPipe pool init failed - using direct allocation\n");
    }
    
    // Initialize PrivateData memory pool for high-scale operation
    const char* pvtPoolSizeEnv = std::getenv("MOD_DEEPGRAM_PVT_POOL_SIZE");
    size_t pvtPoolSize = pvtPoolSizeEnv ? std::atoi(pvtPoolSizeEnv) : 2000;  // Default 2K sessions
    if (deepgram::PrivateDataPool::Initialize(pvtPoolSize)) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
        "mod_deepgram_transcribe: PrivateData pool initialized (capacity=%zu)\n", pvtPoolSize);
    } else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
        "mod_deepgram_transcribe: PrivateData pool init failed - using malloc\n");
    }

		const char* apiKey = std::getenv("DEEPGRAM_API_KEY");
		if (NULL == apiKey) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
				"\"DEEPGRAM_API_KEY\" env var not set; authentication will expect channel variables of same names to be set\n");
		}
		else {
			hasDefaultCredentials = true;
      defaultApiKey = apiKey;
		}
		return SWITCH_STATUS_SUCCESS;
  }

  switch_status_t dg_transcribe_cleanup() {
    // Shutdown PrivateData pool first
    deepgram::PrivateDataPool::Shutdown();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "PrivateData pool shutdown complete\n");
    
    // Shutdown AudioPipe pool (log stats before cleanup)
    deepgram::AudioPipePool::shutdown();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AudioPipe pool shutdown complete\n");
    
    bool cleanup = false;
    cleanup = deepgram::AudioPipe::deinitialize();
    if (cleanup == true) {
        return SWITCH_STATUS_SUCCESS;
    }
    return SWITCH_STATUS_FALSE;
  }
	
  switch_status_t dg_transcribe_session_init(switch_core_session_t *session,
    responseHandler_t responseHandler, uint32_t samples_per_second, uint32_t channels,
    char* lang, int interim, char* bugname, char* metadata, void **ppUserData)
  {
    int err;

    // allocate per-session data structure from memory pool (HIGH SCALE OPTIMIZATION)
    // Uses pre-allocated pool to eliminate malloc overhead in hot path
    // Falls back to malloc if pool exhausted
    private_t* tech_pvt = deepgram::PrivateDataPool::Acquire();
    if (!tech_pvt) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "error allocating memory from pool!\n");
      return SWITCH_STATUS_FALSE;
    }

    // Get actual codec sample rate for resampling
    switch_codec_implementation_t read_impl = {};
    switch_core_session_get_read_impl(session, &read_impl);
    uint32_t codec_sample_rate = !strcasecmp(read_impl.iananame, "g722") ?
      read_impl.actual_samples_per_second : read_impl.samples_per_second;

    // User-configurable target sampling rate (samples_per_second parameter)
    // Note: Deepgram supports 8kHz, 16kHz, and other rates
    if (SWITCH_STATUS_SUCCESS != fork_data_init(tech_pvt, session, codec_sample_rate, samples_per_second, channels, lang, interim, bugname, metadata, responseHandler)) {
      destroy_tech_pvt(tech_pvt);
      return SWITCH_STATUS_FALSE;
    }

    // Emit session start event with metadata
    emit_metadata_event(session, metadata, TRANSCRIBE_EVENT_SESSION_START, bugname);

    *ppUserData = tech_pvt;

    deepgram::AudioPipe *pAudioPipe = static_cast<deepgram::AudioPipe *>(tech_pvt->pAudioPipe);
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connecting now\n");
    pAudioPipe->connect();
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "connection in progress\n");
    return SWITCH_STATUS_SUCCESS;
  }

	switch_status_t dg_transcribe_session_stop(switch_core_session_t *session,int channelIsClosing, char* bugname) {
    switch_channel_t *channel = switch_core_session_get_channel(session);
    switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, MY_BUG_NAME);
    if (!bug) {
      switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "dg_transcribe_session_stop: no bug - websocket conection already closed\n");
      return SWITCH_STATUS_FALSE;
    }
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    uint32_t id = tech_pvt->id;

    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) dg_transcribe_session_stop\n", id);

    if (!tech_pvt) return SWITCH_STATUS_FALSE;

    // Emit session stop event with metadata
    emit_metadata_event(session, tech_pvt->metadata, TRANSCRIBE_EVENT_SESSION_STOP, bugname);

    // close connection and get final responses
    switch_mutex_lock(tech_pvt->mutex);
    switch_channel_set_private(channel, bugname, NULL);
    if (!channelIsClosing) switch_core_media_bug_remove(session, &bug);

    deepgram::AudioPipe *pAudioPipe = static_cast<deepgram::AudioPipe *>(tech_pvt->pAudioPipe);
    if (pAudioPipe) reaper(tech_pvt);
    destroy_tech_pvt(tech_pvt);
    switch_mutex_unlock(tech_pvt->mutex);
    switch_mutex_destroy(tech_pvt->mutex);
    tech_pvt->mutex = nullptr;
    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "(%u) dg_transcribe_session_stop\n", id);
    return SWITCH_STATUS_SUCCESS;
  }
	
	switch_bool_t dg_transcribe_frame(switch_core_session_t *session, switch_media_bug_t *bug) {
    private_t* tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
    bool dirty = false;

    if (!tech_pvt) return SWITCH_TRUE;
    
    if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
      if (!tech_pvt->pAudioPipe) {
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }
      deepgram::AudioPipe *pAudioPipe = static_cast<deepgram::AudioPipe *>(tech_pvt->pAudioPipe);
      
      // CRITICAL FIX: Don't buffer audio until WebSocket connection is fully established
      // This prevents buffer overflow and data loss during the initial connection phase
      if (pAudioPipe->getLwsState() != deepgram::AudioPipe::LWS_CLIENT_CONNECTED) {
        // Connection not ready yet - discard audio frames instead of buffering
        // This is intentional: first ~3s of audio won't be transcribed, but no data corruption
        switch_mutex_unlock(tech_pvt->mutex);
        return SWITCH_TRUE;
      }

      /* ============================================================================
       * LOCK-FREE AUDIO PATH (HIGH SCALE OPTIMIZATION)
       * 
       * The lockAudioBuffer()/unlockAudioBuffer() calls are now NO-OPS.
       * Audio is pushed directly to the lock-free ring buffer using pushAudio().
       * 
       * ZERO-COPY OPTIMIZATION:
       * For resampling mode, we use reserve/commit pattern to have speex write
       * directly into the ring buffer, eliminating one memcpy per frame.
       * ============================================================================ */
      pAudioPipe->lockAudioBuffer();  // No-op in lock-free design
      
      if (NULL == tech_pvt->resampler) {
        // Passthrough mode - no resampling, direct push to ring buffer
        // Note: Can't fully eliminate copy here since switch_core_media_bug_read
        // requires its own buffer. We still use pushAudio() which does one memcpy.
        uint8_t frame_buffer[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = { 0 };
        frame.data = frame_buffer;
        frame.buflen = sizeof(frame_buffer);
        
        while (true) {
          // Check available space in lock-free ring buffer
          size_t available = pAudioPipe->audioSpaceAvailable();
          size_t buffer_used = pAudioPipe->audioSize();
          size_t buffer_capacity = pAudioPipe->audioCapacity();
          if (available < pAudioPipe->binaryMinSpace()) {
            if (!tech_pvt->buffer_overrun_notified) {
              tech_pvt->buffer_overrun_notified = 1;
              tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
            }
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
              "(%u) dropping packets - ring buffer full! used=%zu/%zu bytes (%.1f%%), available=%zu, min_space=%zu\n", 
              tech_pvt->id, buffer_used, buffer_capacity, 
              (buffer_used * 100.0) / buffer_capacity, available, pAudioPipe->binaryMinSpace());
            break;  // Exit loop - buffer is full
          }

          switch_status_t rv = switch_core_media_bug_read(bug, &frame, SWITCH_TRUE);
          if (rv != SWITCH_STATUS_SUCCESS) break;
          
          if (frame.datalen) {
            // Push audio to lock-free ring buffer (non-blocking)
            size_t pushed = pAudioPipe->pushAudio(frame.data, frame.datalen);
            
            if (pushed > 0) {
              // Track passthrough stats
              tech_pvt->resampler_frames_processed++;
              tech_pvt->resampler_samples_in += frame.datalen / (2 * tech_pvt->channels);
              tech_pvt->resampler_samples_out += frame.datalen / (2 * tech_pvt->channels);
              tech_pvt->resampler_bytes_written += pushed;
              dirty = true;
            } else {
              // Ring buffer full - will be processed next frame
              break;
            }
          }
        }
      }
      else {
        // ============================================================================
        // ZERO-COPY RESAMPLING MODE
        // 
        // Reserve space in ring buffer BEFORE resampling, then have speex write
        // directly into the reserved region. This eliminates one memcpy per frame:
        //   Old: frame -> speex -> resample_out -> memcpy -> ring buffer
        //   New: frame -> speex -> ring buffer (direct)
        // 
        // At 50fps * 5K calls = 250,000 memcpy operations eliminated per second!
        // ============================================================================
        uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
        switch_frame_t frame = { 0 };
        frame.data = data;
        frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;
        
        // Fallback buffer for wrap-around cases (rare)
        uint8_t fallback_resample_out[SWITCH_RECOMMENDED_BUFFER_SIZE];
        
        while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS) {
          if (frame.datalen) {
            // Estimate output size: assume worst case 8kHz->16kHz = 2x expansion
            // Actual: out_samples <= in_samples * (target_rate / source_rate) + some
            // For 16kHz output with typical 320 byte frames, max output ~640 bytes
            size_t max_output_bytes = (frame.datalen * 2) + 64;  // Conservative estimate
            
            // Try zero-copy: reserve space directly in ring buffer
            auto reserve = pAudioPipe->reserveAudio(max_output_bytes);
            
            if (reserve.success && reserve.contiguous >= max_output_bytes) {
              // ZERO-COPY PATH: Have speex write directly to ring buffer
              spx_uint32_t out_len = reserve.contiguous >> 1;  // space for samples
              spx_uint32_t in_len = frame.samples;
              spx_uint32_t in_len_before = in_len;

              speex_resampler_process_interleaved_int(tech_pvt->resampler, 
                (const spx_int16_t *) frame.data, 
                (spx_uint32_t *) &in_len, 
                (spx_int16_t *) reserve.ptr,  // Write directly to ring buffer!
                &out_len);

              if (out_len > 0) {
                // Commit the actual bytes written
                size_t bytes_written = out_len << tech_pvt->channels;
                pAudioPipe->commitAudio(bytes_written);
                
                // Track statistics
                tech_pvt->resampler_frames_processed++;
                tech_pvt->resampler_samples_in += in_len_before;
                tech_pvt->resampler_samples_out += out_len;
                tech_pvt->resampler_bytes_written += bytes_written;
                dirty = true;
              }
              // Note: If out_len == 0, we simply don't commit anything
            }
            else if (reserve.success) {
              // FALLBACK PATH: Not enough contiguous space (wrap-around case)
              // Use intermediate buffer + pushAudio (one extra memcpy)
              // This is rare - only happens near buffer wrap boundary
              spx_uint32_t out_len = sizeof(fallback_resample_out) >> 1;
              spx_uint32_t in_len = frame.samples;
              spx_uint32_t in_len_before = in_len;

              speex_resampler_process_interleaved_int(tech_pvt->resampler, 
                (const spx_int16_t *) frame.data, 
                (spx_uint32_t *) &in_len, 
                (spx_int16_t *) fallback_resample_out,
                &out_len);

              if (out_len > 0) {
                size_t bytes_to_push = out_len << tech_pvt->channels;
                size_t pushed = pAudioPipe->pushAudio(fallback_resample_out, bytes_to_push);
                
                if (pushed > 0) {
                  tech_pvt->resampler_frames_processed++;
                  tech_pvt->resampler_samples_in += in_len_before;
                  tech_pvt->resampler_samples_out += out_len;
                  tech_pvt->resampler_bytes_written += pushed;
                  dirty = true;
                }
              }
            }
            else {
              // Buffer full - trigger overrun notification
              if (!tech_pvt->buffer_overrun_notified) {
                tech_pvt->buffer_overrun_notified = 1;
                size_t buffer_used = pAudioPipe->audioSize();
                size_t buffer_capacity = pAudioPipe->audioCapacity();
                size_t space_avail = pAudioPipe->audioSpaceAvailable();
                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, 
                  "(%u) dropping packets - reserve failed! reserve={success=%d, contiguous=%zu, total=%zu}, "
                  "requested=%zu, buf_used=%zu/%zu (%.1f%%), space_avail=%zu, ws_state=%d\n", 
                  tech_pvt->id, reserve.success, reserve.contiguous, reserve.total,
                  max_output_bytes, buffer_used, buffer_capacity, 
                  (buffer_used * 100.0) / buffer_capacity, space_avail, (int)pAudioPipe->getLwsState());
                tech_pvt->responseHandler(session, TRANSCRIBE_EVENT_BUFFER_OVERRUN, NULL, tech_pvt->bugname, 0);
              }
              break;
            }
            
            // Log stats every 10 seconds (500 frames at 50fps)
            switch_time_t now = switch_time_now();
            if ((now - tech_pvt->resampler_last_log_time) >= 10000000) {  // 10 seconds in microseconds
              double elapsed_secs = (now - tech_pvt->resampler_start_time) / 1000000.0;
              double fps = tech_pvt->resampler_frames_processed / elapsed_secs;
              double mb_written = tech_pvt->resampler_bytes_written / (1024.0 * 1024.0);
              size_t buf_used = pAudioPipe->audioSize();
              size_t buf_cap = pAudioPipe->audioCapacity();

              switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                "[RESAMPLER-STATS] (%u) %s: frames=%lu, samples_in=%lu, samples_out=%lu, "
                "bytes=%.2fMB, fps=%.1f, elapsed=%.1fs, ratio=%.3f, buf=%zu/%zu (%.1f%%), ws=%d\n",
                tech_pvt->id, tech_pvt->bugname,
                (unsigned long)tech_pvt->resampler_frames_processed,
                (unsigned long)tech_pvt->resampler_samples_in,
                (unsigned long)tech_pvt->resampler_samples_out,
                mb_written, fps, elapsed_secs,
                (double)tech_pvt->resampler_samples_out / tech_pvt->resampler_samples_in,
                buf_used, buf_cap, (buf_used * 100.0) / buf_cap,
                (int)pAudioPipe->getLwsState());
              tech_pvt->resampler_last_log_time = now;
            }
          }
        }
      }

      pAudioPipe->unlockAudioBuffer();  // Triggers pending write if data available
      switch_mutex_unlock(tech_pvt->mutex);
    }
    return SWITCH_TRUE;
  }
}

/**
 * PHASE 3: Performance Monitoring API Implementation
 * 
 * Provides comprehensive performance statistics for high-scale deployments.
 * Called from CLI: fs_cli -x "uuid_deepgram_transcribe <uuid> stats [pool|context|audio|all]"
 */
extern "C" switch_status_t dg_get_stats(switch_core_session_t *session, const char* stats_type, switch_stream_handle_t *stream)
{
    if (!session || !stream || !stats_type) {
        return SWITCH_STATUS_FALSE;
    }
    
    // Handle different stats types
    if (!strcmp(stats_type, "pool") || !strcmp(stats_type, "all")) {
        // Memory pool statistics
        if (deepgram::AudioPipePool::is_initialized()) {
            const auto& pool_stats = deepgram::AudioPipePool::stats();
            stream->write_function(stream,
                "+OK AudioPipe Pool Stats: "
                "Capacity: %zu, In Use: %lu, High Water: %lu, "
                "Acquires: %lu, Releases: %lu, Hits: %lu, Misses: %lu\n",
                pool_stats.pool_capacity,
                (unsigned long)pool_stats.current_in_use.load(),
                (unsigned long)pool_stats.high_water_mark.load(),
                (unsigned long)pool_stats.acquires.load(),
                (unsigned long)pool_stats.releases.load(),
                (unsigned long)pool_stats.pool_hits.load(),
                (unsigned long)pool_stats.pool_misses.load());
        } else {
            stream->write_function(stream, "+OK AudioPipe Pool: Not initialized\n");
        }
        
        if (deepgram::PrivateDataPool::IsInitialized()) {
            const auto& pvt_stats = deepgram::PrivateDataPool::GetStats();
            stream->write_function(stream,
                "+OK PrivateData Pool Stats: "
                "Capacity: %zu, In Use: %lu, High Water: %lu, "
                "Acquires: %lu, Releases: %lu, Hits: %lu, Misses: %lu\n",
                pvt_stats.capacity,
                (unsigned long)pvt_stats.current_in_use.load(),
                (unsigned long)pvt_stats.high_water_mark.load(),
                (unsigned long)pvt_stats.acquires.load(),
                (unsigned long)pvt_stats.releases.load(),
                (unsigned long)pvt_stats.pool_hits.load(),
                (unsigned long)pvt_stats.pool_misses.load());
        } else {
            stream->write_function(stream, "+OK PrivateData Pool: Not initialized\n");
        }
    }
    
    if (!strcmp(stats_type, "context") || !strcmp(stats_type, "all")) {
        // Thread-local context statistics
        uint32_t total_contexts = deepgram::ContextManager::getTotalContexts();
        stream->write_function(stream,
            "+OK Context Manager Stats: "
            "Total Contexts: %u (Thread-Local: Zero Contention)\n",
            total_contexts);
    }
    
    if (!strcmp(stats_type, "audio") || !strcmp(stats_type, "all")) {
        // Session-specific audio statistics
        switch_channel_t *channel = switch_core_session_get_channel(session);
        switch_media_bug_t *bug = (switch_media_bug_t *) switch_channel_get_private(channel, "deepgram_transcribe");
        
        if (bug) {
            private_t *tech_pvt = (private_t *) switch_core_media_bug_get_user_data(bug);
            if (tech_pvt && tech_pvt->pAudioPipe) {
                deepgram::AudioPipe* pAudioPipe = static_cast<deepgram::AudioPipe*>(tech_pvt->pAudioPipe);
                
                stream->write_function(stream,
                    "+OK Audio Pipeline Stats: "
                    "Session: %s, Buffer Capacity: %zu, Pending Bytes: %lu, "
                    "Resampler Frames: %lu, Samples In: %lu, Samples Out: %lu\n",
                    tech_pvt->sessionId,
                    pAudioPipe->audioCapacity(),
                    (unsigned long)pAudioPipe->audioDataAvailable(),
                    (unsigned long)tech_pvt->resampler_frames_processed,
                    (unsigned long)tech_pvt->resampler_samples_in,
                    (unsigned long)tech_pvt->resampler_samples_out);
            } else {
                stream->write_function(stream, "+OK Audio Pipeline: Session has no active AudioPipe\n");
            }
        } else {
            stream->write_function(stream, "+OK Audio Pipeline: Session has no active transcription\n");
        }
    }
    
    if (strcmp(stats_type, "pool") && strcmp(stats_type, "context") && 
        strcmp(stats_type, "audio") && strcmp(stats_type, "all")) {
        stream->write_function(stream, "-ERR Invalid stats type '%s'. Supported: pool, context, audio, all\n", stats_type);
        return SWITCH_STATUS_FALSE;
    }
    
    return SWITCH_STATUS_SUCCESS;
}
