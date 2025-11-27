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
#include <iostream>
#include <memory>
#include <chrono>
#include <queue>
#include <condition_variable>

#include <google/cloud/speech/v2/speech_client.h>
#include <grpcpp/grpcpp.h>

#include "mod_google_transcribev2.h"

namespace gc = ::google::cloud;
namespace speech = ::google::cloud::speech_v2;

#define RTP_PACKETIZATION_PERIOD 20
#define FRAME_SIZE_8000  320 /* 20ms frame at 8 kHz (1 channel) */

namespace {
  static const char *requestedBufferSecs = std::getenv("MOD_AUDIO_FORK_BUFFER_SECS");
  static int nAudioBufferSecs = std::max(1, std::min(requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
  static unsigned int idxCallCount = 0;

  /* ============================================================================
   * Audio Buffer Class - Handles chunked audio buffering
   * ============================================================================ */
  class AudioBuffer {
  public:
    AudioBuffer(size_t max_size = 4096 * 100) : max_size_(max_size) {}

    bool write(const uint8_t* data, size_t len) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (buffer_.size() + len > max_size_) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
          "AudioBuffer overflow: size=%zu, incoming=%zu, max=%zu\n",
          buffer_.size(), len, max_size_);
        return false;
      }
      buffer_.insert(buffer_.end(), data, data + len);
      return true;
    }

    std::vector<uint8_t> read(size_t len) {
      std::lock_guard<std::mutex> lock(mutex_);
      size_t to_read = std::min(len, buffer_.size());
      std::vector<uint8_t> result(buffer_.begin(), buffer_.begin() + to_read);
      buffer_.erase(buffer_.begin(), buffer_.begin() + to_read);
      return result;
    }

    std::vector<uint8_t> readAll() {
      std::lock_guard<std::mutex> lock(mutex_);
      std::vector<uint8_t> result = buffer_;
      buffer_.clear();
      return result;
    }

    size_t size() const {
      std::lock_guard<std::mutex> lock(mutex_);
      return buffer_.size();
    }

    void clear() {
      std::lock_guard<std::mutex> lock(mutex_);
      buffer_.clear();
    }

  private:
    mutable std::mutex mutex_;
    std::vector<uint8_t> buffer_;
    size_t max_size_;
  };

  /* ============================================================================
   * Google Transcribe Session Class
   * ============================================================================ */
  class GoogleTranscribeSession {
  public:
    GoogleTranscribeSession(
      switch_core_session_t* session,
      responseHandler_t responseHandler,
      uint32_t sample_rate,
      uint32_t channels,
      const char* lang,
      bool interim,
      const char* bugname,
      const char* metadata)
      : session_(session)
      , responseHandler_(responseHandler)
      , sample_rate_(sample_rate)
      , channels_(channels)
      , language_(lang ? lang : "en-US")
      , interim_(interim)
      , bugname_(bugname ? bugname : "google_transcribev2")
      , metadata_(metadata ? metadata : "")
      , is_finished_(false)
      , result_id_(1)
    {
      // Get Google Cloud configuration from environment
      project_id_ = std::getenv("GCP_PROJECT_ID");
      if (!project_id_) project_id_ = "your-project-id";

      location_ = std::getenv("GCP_LOCATION");
      if (!location_) location_ = "us-central1";

      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "GoogleTranscribeSession created: project=%s, location=%s, lang=%s, rate=%d, channels=%d\n",
        project_id_, location_, language_.c_str(), sample_rate_, channels_);
    }

    ~GoogleTranscribeSession() {
      stop();
    }

    bool start() {
      try {
        // Create Google Speech client
        client_ = std::make_unique<speech::SpeechClient>(speech::MakeSpeechConnection());

        // Build recognizer name: projects/{project}/locations/{location}/recognizers/_
        std::string recognizer = "projects/" + std::string(project_id_) +
                                "/locations/" + std::string(location_) +
                                "/recognizers/_";

        // Create recognition config
        speech::v2::RecognitionConfig config;

        // Set explicit decoding config
        auto* decoding_config = config.mutable_explicit_decoding_config();
        decoding_config->set_encoding(speech::v2::ExplicitDecodingConfig::LINEAR16);
        decoding_config->set_sample_rate_hertz(sample_rate_);
        decoding_config->set_audio_channel_count(channels_);

        // Set language codes
        config.add_language_codes(language_);

        // Set model
        config.set_model("long");

        // Set features
        auto* features = config.mutable_features();
        features->set_enable_word_time_offsets(true);
        features->set_enable_word_confidence(true);

        // Multi-channel mode (stereo)
        if (channels_ > 1) {
          features->set_multi_channel_mode(
            speech::v2::RecognitionFeatures::SEPARATE_RECOGNITION_PER_CHANNEL);
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
            "Enabled separate recognition per channel (stereo mode)\n");
        }

        // Create streaming config
        speech::v2::StreamingRecognitionConfig streaming_config;
        *streaming_config.mutable_config() = config;
        streaming_config.set_streaming_features(
          speech::v2::StreamingRecognitionFeatures::ENABLE_VOICE_ACTIVITY_EVENTS);

        // Create initial request with recognizer
        speech::v2::StreamingRecognizeRequest initial_request;
        initial_request.set_recognizer(recognizer);
        *initial_request.mutable_streaming_config() = streaming_config;

        // Start streaming
        stream_ = client_->StreamingRecognize();

        // Send initial config
        if (!stream_->Write(initial_request)) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "Failed to write initial config to Google Speech API\n");
          return false;
        }

        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
          "Google Speech streaming started successfully\n");

        // Start response reader thread
        response_thread_ = std::thread(&GoogleTranscribeSession::responseReader, this);

        return true;

      } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
          "Failed to start Google transcription: %s\n", e.what());
        return false;
      }
    }

    void stop() {
      if (is_finished_.exchange(true)) {
        return; // Already stopped
      }

      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "Stopping Google transcription session\n");

      // Close write stream
      if (stream_) {
        stream_->WritesDone();
      }

      // Wait for response thread to finish
      if (response_thread_.joinable()) {
        response_thread_.join();
      }

      // Cleanup
      stream_.reset();
      client_.reset();
    }

    bool processAudioFrame(const uint8_t* data, size_t len) {
      if (is_finished_ || !stream_) {
        return false;
      }

      // Buffer audio
      for (size_t ch = 0; ch < channels_; ++ch) {
        if (ch < 2) { // Max 2 channels
          buffers_[ch].write(data + (ch * len / channels_), len / channels_);
        }
      }

      // Send audio chunks (4KB at a time)
      const size_t chunk_size = 4096;
      for (size_t ch = 0; ch < channels_; ++ch) {
        while (buffers_[ch].size() >= chunk_size) {
          auto chunk = buffers_[ch].read(chunk_size);

          speech::v2::StreamingRecognizeRequest audio_request;
          audio_request.set_audio(chunk.data(), chunk.size());

          if (!stream_->Write(audio_request)) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
              "Failed to write audio chunk to Google Speech API\n");
            return false;
          }
        }
      }

      return true;
    }

  private:
    void responseReader() {
      try {
        speech::v2::StreamingRecognizeResponse response;
        while (stream_->Read(&response)) {
          processResponse(response);
        }

        auto status = stream_->Finish();
        if (!status.ok()) {
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "Google Speech streaming error: %s\n", status.message().c_str());
        }

      } catch (const std::exception& e) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
          "Response reader exception: %s\n", e.what());
      }
    }

    void processResponse(const speech::v2::StreamingRecognizeResponse& response) {
      for (const auto& result : response.results()) {
        if (result.alternatives().empty()) continue;

        const auto& alternative = result.alternatives(0);
        bool is_final = result.is_final();

        // Get channel tag (0 or 1)
        int channel = result.has_channel_tag() ? result.channel_tag() : 0;

        // Map channel to speaker ID
        std::string speaker_id = getSpeak  erIdForChannel(channel);

        // Build JSON response (aligned with AWS/Deepgram format)
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "event", is_final ? "final_transcript" : "partial_transcript");
        cJSON_AddStringToObject(root, "uuid", switch_core_session_get_uuid(session_));
        cJSON_AddNumberToObject(root, "channel", channel);
        cJSON_AddStringToObject(root, "speaker_id", speaker_id.c_str());
        cJSON_AddStringToObject(root, "text", alternative.transcript().c_str());
        cJSON_AddStringToObject(root, "timestamp", getCurrentTimestamp().c_str());
        cJSON_AddBoolToObject(root, "is_final", is_final);

        if (is_final) {
          cJSON_AddNumberToObject(root, "confidence", alternative.confidence());

          // Add word-level details
          cJSON* words = cJSON_CreateArray();
          for (const auto& word : alternative.words()) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "content", word.word().c_str());
            cJSON_AddNumberToObject(item, "start_time",
              word.start_offset().seconds() + word.start_offset().nanos() / 1e9);
            cJSON_AddNumberToObject(item, "end_time",
              word.end_offset().seconds() + word.end_offset().nanos() / 1e9);
            cJSON_AddNumberToObject(item, "confidence", word.confidence());
            cJSON_AddArrayToObject(root, "words", words);
            cJSON_AddItemToArray(words, item);
          }
        } else {
          cJSON_AddNullToObject(root, "confidence");
        }

        char* json_str = cJSON_PrintUnformatted(root);
        if (json_str) {
          // Log the transcript
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
            "[%s %d] Channel %d: %s\n",
            is_final ? "FINAL" : "PARTIAL",
            is_final ? result_id_++ : 0,
            channel,
            alternative.transcript().c_str());

          // Call response handler
          if (responseHandler_) {
            responseHandler_(session_, TRANSCRIBE_EVENT_RESULTS, json_str, bugname_.c_str(), is_final ? 1 : 0);
          }

          free(json_str);
        }
        cJSON_Delete(root);
      }
    }

    std::string getSpeakerIdForChannel(int channel) {
      switch_channel_t *chan = switch_core_session_get_channel(session_);

      if (channel == 0) {
        // Caller (Channel 0)
        const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
        const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
        if (!caller_name) caller_name = "Unknown";
        if (!caller_number) caller_number = "Unknown";

        std::ostringstream oss;
        oss << caller_name << "(" << caller_number << ")";
        return oss.str();

      } else {
        // Callee (Channel 1)
        const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
        const char* callee_number = switch_channel_get_variable(chan, "callee_id_number");
        if (!callee_name) callee_name = "Unknown";
        if (!callee_number) callee_number = "Unknown";

        std::ostringstream oss;
        oss << callee_name << "(" << callee_number << ")";
        return oss.str();
      }
    }

    std::string getCurrentTimestamp() {
      auto now = std::chrono::system_clock::now();
      auto time_t = std::chrono::system_clock::to_time_t(now);
      auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

      std::ostringstream oss;
      char buf[32];
      std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&time_t));
      oss << buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
      return oss.str();
    }

  private:
    switch_core_session_t* session_;
    responseHandler_t responseHandler_;
    uint32_t sample_rate_;
    uint32_t channels_;
    std::string language_;
    bool interim_;
    std::string bugname_;
    std::string metadata_;
    std::atomic<bool> is_finished_;
    int result_id_;

    const char* project_id_;
    const char* location_;

    std::unique_ptr<speech::SpeechClient> client_;
    std::unique_ptr<grpc::ClientReaderWriterInterface<
      speech::v2::StreamingRecognizeRequest,
      speech::v2::StreamingRecognizeResponse>> stream_;

    std::thread response_thread_;
    AudioBuffer buffers_[2];  // Max 2 channels
  };

  /* ============================================================================
   * Reaper function - cleanup session
   * ============================================================================ */
  static void reaper(private_t *tech_pvt) {
    if (tech_pvt && tech_pvt->pGoogleSession) {
      GoogleTranscribeSession* session = (GoogleTranscribeSession*)tech_pvt->pGoogleSession;
      std::thread t([session]{
        session->stop();
        delete session;
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Google session cleaned up\n");
      });
      t.detach();
      tech_pvt->pGoogleSession = nullptr;
    }
  }

  static void destroy_tech_pvt(private_t *tech_pvt) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s destroy_tech_pvt\n", tech_pvt->sessionId);
    if (tech_pvt) {
      if (tech_pvt->pGoogleSession) {
        GoogleTranscribeSession* session = (GoogleTranscribeSession*)tech_pvt->pGoogleSession;
        delete session;
        tech_pvt->pGoogleSession = nullptr;
      }
      if (tech_pvt->resampler) {
        speex_resampler_destroy(tech_pvt->resampler);
        tech_pvt->resampler = NULL;
      }
    }
  }

} // anonymous namespace

/* ============================================================================
 * C API Implementation
 * ============================================================================ */

extern "C" {

switch_status_t google_transcribe_init() {
  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Google transcribe init\n");
  return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_cleanup() {
  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Google transcribe cleanup\n");
  return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_session_init(
  switch_core_session_t *session,
  responseHandler_t responseHandler,
  uint32_t samples_per_second,
  uint32_t channels,
  char* lang,
  int interim,
  char* bugname,
  char* metadata,
  void **ppUserData)
{
  switch_channel_t *channel = switch_core_session_get_channel(session);
  private_t *tech_pvt = NULL;
  switch_codec_implementation_t read_impl = { 0 };

  switch_core_session_get_read_impl(session, &read_impl);

  // Allocate private data
  tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));
  memset(tech_pvt, 0, sizeof(private_t));

  strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
  strncpy(tech_pvt->bugname, bugname, MAX_BUG_LEN);
  if (metadata) {
    strncpy(tech_pvt->metadata, metadata, MAX_METADATA_LEN);
  }

  tech_pvt->sampling = samples_per_second;
  tech_pvt->channels = channels;
  tech_pvt->id = idxCallCount++;
  tech_pvt->responseHandler = responseHandler;

  // Setup resampler if needed
  if (read_impl.actual_samples_per_second != samples_per_second) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
      "(%u) resampling from %u to %u\n", tech_pvt->id,
      read_impl.actual_samples_per_second, samples_per_second);

    tech_pvt->resampler = speex_resampler_init(channels,
      read_impl.actual_samples_per_second,
      samples_per_second, SWITCH_RESAMPLE_QUALITY,
      NULL);

    if (!tech_pvt->resampler) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
        "(%u) error initializing resampler\n", tech_pvt->id);
      return SWITCH_STATUS_FALSE;
    }
  }

  // Create Google transcribe session
  GoogleTranscribeSession* google_session = new GoogleTranscribeSession(
    session, responseHandler, samples_per_second, channels,
    lang, interim, bugname, metadata);

  if (!google_session->start()) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
      "Failed to start Google transcribe session\n");
    delete google_session;
    destroy_tech_pvt(tech_pvt);
    return SWITCH_STATUS_FALSE;
  }

  tech_pvt->pGoogleSession = google_session;
  *ppUserData = tech_pvt;

  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
    "(%u) Google transcribe session initialized\n", tech_pvt->id);

  return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_session_stop(
  switch_core_session_t *session,
  int channelIsClosing,
  char* bugname)
{
  switch_channel_t *channel = switch_core_session_get_channel(session);
  switch_media_bug_t *bug = switch_channel_get_private(channel, bugname);

  if (!bug) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
      "google_transcribe_session_stop: no bug found for %s\n", bugname);
    return SWITCH_STATUS_FALSE;
  }

  private_t *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
  if (!tech_pvt) {
    return SWITCH_STATUS_FALSE;
  }

  switch_mutex_lock(tech_pvt->mutex);
  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
    "(%u) google_transcribe_session_stop\n", tech_pvt->id);

  // Stop Google session
  if (tech_pvt->pGoogleSession) {
    reaper(tech_pvt);
  }

  // Cleanup
  destroy_tech_pvt(tech_pvt);

  switch_mutex_unlock(tech_pvt->mutex);
  return SWITCH_STATUS_SUCCESS;
}

switch_bool_t google_transcribe_frame(
  switch_core_session_t *session,
  switch_media_bug_t *bug)
{
  private_t *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
  if (!tech_pvt || tech_pvt->is_finished) {
    return SWITCH_TRUE;
  }

  GoogleTranscribeSession* google_session = (GoogleTranscribeSession*)tech_pvt->pGoogleSession;
  if (!google_session) {
    return SWITCH_TRUE;
  }

  // Get audio frame
  switch_frame_t frame = { 0 };
  frame.data = NULL;
  frame.buflen = 0;

  while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
    if (frame.datalen > 0) {
      // Resample if needed
      if (tech_pvt->resampler) {
        int16_t resampled[SWITCH_RECOMMENDED_BUFFER_SIZE];
        uint32_t in_len = frame.samples;
        uint32_t out_len = sizeof(resampled) / sizeof(int16_t);

        speex_resampler_process_interleaved_int(tech_pvt->resampler,
          (int16_t*)frame.data, &in_len,
          resampled, &out_len);

        google_session->processAudioFrame((uint8_t*)resampled, out_len * sizeof(int16_t));
      } else {
        google_session->processAudioFrame((uint8_t*)frame.data, frame.datalen);
      }
    }
  }

  return SWITCH_TRUE;
}

} // extern "C"
