#include <cstdlib>
#include <algorithm>
#include <future>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>

#include "speech.grpc.pb.h"
#include "speech.pb.h"

#include <switch_json.h>

#include "mod_google_transcribev2.h"

using google::cloud::speech::v2::RecognitionConfig;
using google::cloud::speech::v2::Speech;
using google::cloud::speech::v2::StreamingRecognizeRequest;
using google::cloud::speech::v2::StreamingRecognizeResponse;
using google::cloud::speech::v2::StreamingRecognitionConfig;
using google::cloud::speech::v2::RecognitionFeatures;
using google::cloud::speech::v2::SpeakerDiarizationConfig;
using google::cloud::speech::v2::SpeechAdaptation;
using google::cloud::speech::v2::PhraseSet;
using google::cloud::speech::v2::AutoDetectDecodingConfig;
using google::cloud::speech::v2::ExplicitDecodingConfig;
// using google::cloud::speech::v2::Phrase; // Not available in v2

#define CHUNKSIZE (320)

namespace {
  // Utility functions
}
class GStreamer;

class GStreamer {
public:
	GStreamer(
    switch_core_session_t *session, 
    uint32_t channels, 
    char* lang, 
    int interim, 
    uint32_t config_sample_rate,
    char* project_id, 
    char* location_id) : 
		m_session(session), m_writesDone(false), m_connected(false), m_interim(interim), m_finishing(false), m_packets(0) {

    const char* var;
    switch_channel_t *channel = switch_core_session_get_channel(session);

		m_request.set_recognizer(std::string("projects/") + project_id + "/locations/" + location_id + "/recognizers/_");

		StreamingRecognitionConfig* streaming_config = m_request.mutable_streaming_config();
		RecognitionConfig* config = streaming_config->mutable_config();

		// Set explicit decoding config
		ExplicitDecodingConfig* explicit_config = config->mutable_explicit_decoding_config();
		explicit_config->set_encoding(ExplicitDecodingConfig::LINEAR16);
		explicit_config->set_sample_rate_hertz(config_sample_rate);
		explicit_config->set_audio_channel_count(channels);

		config->add_language_codes(lang);

		// Configure features
		RecognitionFeatures* features = config->mutable_features();

		// V2 API: Enable word confidence and timing
		features->set_enable_word_confidence(true);
		features->set_enable_word_time_offsets(true);

		// V2 API: Multi-channel mode (stereo)
		if (channels > 1) {
			features->set_multi_channel_mode(RecognitionFeatures::SEPARATE_RECOGNITION_PER_CHANNEL);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: multi-channel mode enabled\n");
		}

		// Set model if specified
		if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_MODEL"))) {
			config->set_model(var);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: model: %s\n", var);
		} else {
			config->set_model("long");
		}

		// V2 API: Enable automatic punctuation
		if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION"))) {
			features->set_enable_automatic_punctuation(true);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: automatic punctuation enabled\n");
		}

		// V2 API: Profanity filter
		if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_PROFANITY_FILTER"))) {
			features->set_profanity_filter(true);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: profanity filter enabled\n");
		}

		// V2 API: Speaker diarization
		if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_SPEAKER_DIARIZATION"))) {
			SpeakerDiarizationConfig* diarization_config = features->mutable_diarization_config();
			diarization_config->set_min_speaker_count(1);
			diarization_config->set_max_speaker_count(6);
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: speaker diarization enabled\n");

			if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT"))) {
				int count = std::max(atoi(var), 1);
				diarization_config->set_min_speaker_count(count);
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: min speaker count: %d\n", count);
			}
			if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT"))) {
				int count = std::max(atoi(var), 2);
				diarization_config->set_max_speaker_count(count);
				switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "V2: max speaker count: %d\n", count);
			}
		}

		// V2 API: Voice activity events 
		streaming_config->set_enable_voice_activity_events(true);

		// create the channel and stub
		auto creds = grpc::GoogleDefaultCredentials();
		auto channel_grpc = grpc::CreateChannel("speech.googleapis.com", creds);
		m_stub = Speech::NewStub(channel_grpc);

		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_DEBUG, "GStreamer %p created\n", this);	
	}

	~GStreamer() {
		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStreamer::~GStreamer - deleting channel and stub: %p\n", (void*)this);
	}

  void connect() {
    assert(!m_connected);
    // Begin a stream.
  	m_streamer = m_stub->StreamingRecognize(&m_context);
    m_connected = true;

    // read thread is waiting on this
    m_promise.set_value();

  	// Write the first request, containing the config only.
  	m_streamer->Write(m_request);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p got stream ready\n", this);	
  }

  bool write(void* data, uint32_t datalen) {
    if (m_writesDone) {
      return false;
    }

    // Send audio content.
    StreamingRecognizeRequest request;
    request.set_audio(data, datalen);
    bool ok = m_streamer->Write(request);
    if (!ok) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p stream Write failed\n", this);
    }
    m_packets++;
    return ok;
  }

	void writesDone() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p writesDone\n", this);
    m_writesDone = true;
		m_streamer->WritesDone();
	}

  bool read(StreamingRecognizeResponse* response) {
    return m_streamer->Read(response);
  }

  grpc::Status finish() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p finish\n", this);
    return m_streamer->Finish();
  }

  void startRead() {
    m_future = std::async(std::launch::async, [this]{
      return this->read_loop();
    });
  }

  void finishRead() {
    m_finishing = true;
    if (m_future.valid()) {
      m_future.get();
    }
  }

private:

  void read_loop() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p read_loop started\n", this);
    
    // wait for the read loop to be started
    std::shared_future<void> sf(m_promise.get_future());
    sf.wait();

    StreamingRecognizeResponse response;
    while (!m_finishing && read(&response)) {  // Returns false when no more to read.
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
        "GStreamer %p read_loop got a response with %d results\n", this, response.results_size());

      for (int r = 0; r < response.results_size(); ++r) {
        auto result = response.results(r);
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
          "GStreamer %p read_loop processing result %d, is_final: %s, alternatives: %d\n", 
          this, r, result.is_final() ? "true" : "false", result.alternatives_size());

        for (int a = 0; a < result.alternatives_size(); ++a) {
          auto alternative = result.alternatives(a);
          
          switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
            "GStreamer %p read_loop alternative %d transcript: %s\n", 
            this, a, alternative.transcript().c_str());

          // Create JSON response
          cJSON *jResult = cJSON_CreateObject();
          cJSON *jAlternatives = cJSON_CreateArray();
          cJSON *jAlternative = cJSON_CreateObject();

          cJSON_AddStringToObject(jAlternative, "transcript", alternative.transcript().c_str());
          if (alternative.confidence() > 0.0) {
            cJSON_AddNumberToObject(jAlternative, "confidence", alternative.confidence());
          }

          cJSON_AddItemToArray(jAlternatives, jAlternative);
          cJSON_AddItemToObject(jResult, "alternatives", jAlternatives);

          cJSON_AddBoolToObject(jResult, "is_final", result.is_final());
          if (result.stability() > 0.0) {
            cJSON_AddNumberToObject(jResult, "stability", result.stability());
          }

          // Add channel info if available
          if (result.channel_tag() > 0) {
            cJSON_AddNumberToObject(jResult, "channel", result.channel_tag());
          }

          if (!result.language_code().empty()) {
            cJSON_AddStringToObject(jResult, "language_code", result.language_code().c_str());
          }

          char *json_string = cJSON_Print(jResult);
          if (json_string) {
            m_responseHandler(m_session, TRANSCRIBE_EVENT_RESULTS, (const char*)json_string, m_bugname, result.is_final() ? 1 : 0);
            free(json_string);
          }
          cJSON_Delete(jResult);
        }
      }
    }

    grpc::Status status = finish();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p read_loop finished\n", this);
    
    if (status.ok()) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "GStreamer %p read_loop finished successfully\n", this);
    } else {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "GStreamer %p read_loop finished with error: %s\n", 
        this, status.error_message().c_str());
    }
  }

private:
	switch_core_session_t* m_session;
	grpc::ClientContext m_context;
	std::shared_ptr<grpc::ClientReaderWriterInterface<StreamingRecognizeRequest, StreamingRecognizeResponse>> m_streamer;
	std::unique_ptr<Speech::Stub> m_stub;
	StreamingRecognizeRequest m_request;
  bool m_writesDone;
  bool m_connected;
  bool m_interim;
  bool m_finishing;
  int m_packets;
  std::promise<void> m_promise;
  std::future<void> m_future;

public:
  responseHandler_t m_responseHandler;
  char* m_bugname;
};

/*
static void reaper(private_t *tech_pvt) {
  if (tech_pvt->pGoogleSession) {
    GStreamer* pGStreamer = (GStreamer*) tech_pvt->pGoogleSession;
    std::thread t([pGStreamer, tech_pvt]{
      pGStreamer->finishRead();
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "google session finished\n");
      delete pGStreamer;
      tech_pvt->pGoogleSession = nullptr;
    });
    t.detach();
  }
}
*/

static void destroy_tech_pvt(private_t *tech_pvt) {
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s destroy_tech_pvt\n", tech_pvt->sessionId);
	if (tech_pvt) {
		if (tech_pvt->pGoogleSession) {
			GStreamer* pGStreamer = (GStreamer*) tech_pvt->pGoogleSession;
			delete pGStreamer;
			tech_pvt->pGoogleSession = nullptr;
		}
    if (tech_pvt->resampler) {
  		speex_resampler_destroy(tech_pvt->resampler);
  		tech_pvt->resampler = nullptr;
  	}
	}
}

extern "C" switch_status_t google_transcribe_session_init(switch_core_session_t *session, responseHandler_t responseHandler, 
		uint32_t samples_per_second, uint32_t channels, char* lang, int interim, char* bugname, char* metadata, void **ppUserData) {

	private_t *tech_pvt = NULL;
	switch_codec_implementation_t read_impl;
	memset(&read_impl, 0, sizeof(read_impl));

	switch_core_session_get_read_impl(session, &read_impl);

	//allocate the private data
	tech_pvt = (private_t *) switch_core_session_alloc(session, sizeof(private_t));
	memset(tech_pvt, 0, sizeof(private_t));

	strncpy(tech_pvt->sessionId, switch_core_session_get_uuid(session), MAX_SESSION_ID);
	strncpy(tech_pvt->bugname, bugname, MAX_BUG_LEN);
  if (metadata) {
    strncpy(tech_pvt->metadata, metadata, MAX_METADATA_LEN);
  }
  
	tech_pvt->sampling = samples_per_second;
	tech_pvt->channels = channels;
	tech_pvt->id = tech_pvt->id + 1;
	tech_pvt->responseHandler = responseHandler;

  // setup resampler
  if (read_impl.actual_samples_per_second != samples_per_second) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
      "(%u) resampling from %u to %u\n", tech_pvt->id, 
      read_impl.actual_samples_per_second, samples_per_second);
    
    int err;
    tech_pvt->resampler = speex_resampler_init(channels, 
      read_impl.actual_samples_per_second, samples_per_second, 
      SWITCH_RESAMPLE_QUALITY, &err);
      
    if (0 != err) {
      switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, 
        "Error initializing resampler: %s.\n", speex_resampler_strerror(err));
      return SWITCH_STATUS_FALSE;
    }
  } else {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, 
      "(%u) no resampling needed for this call\n", tech_pvt->id);
  }

  // Get Google Cloud credentials from environment or channel variables
  char *project_id = (char*) "freeswitch-project";  // default
  char *location_id = (char*) "us-central1";  // default

  switch_channel_t *channel = switch_core_session_get_channel(session);
  const char* var;
  if ((var = switch_channel_get_variable(channel, "GOOGLE_PROJECT_ID"))) {
    project_id = (char*) var;
  }
  if ((var = switch_channel_get_variable(channel, "GOOGLE_LOCATION_ID"))) {
    location_id = (char*) var;
  }

  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) google_transcribe_session_init\n", tech_pvt->id);

	GStreamer* pGStreamer = new GStreamer(session, channels, lang, interim, samples_per_second, project_id, location_id);
	pGStreamer->m_responseHandler = responseHandler;
  pGStreamer->m_bugname = tech_pvt->bugname;

	tech_pvt->pGoogleSession = pGStreamer;

	switch_mutex_init(&tech_pvt->mutex, SWITCH_MUTEX_NESTED, switch_core_session_get_pool(session));
	if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
		switch_mutex_unlock(tech_pvt->mutex);
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing mutex\n");
	}

	*ppUserData = tech_pvt;

	pGStreamer->connect();
  pGStreamer->startRead();

	return SWITCH_STATUS_SUCCESS;
}

extern "C" switch_status_t google_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing, char* bugname) {
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = (switch_media_bug_t*) switch_channel_get_private(channel, bugname);
	
	if (!bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "google_transcribe_session_stop: no bug found\n");
		return SWITCH_STATUS_FALSE;
	}

	private_t *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
	if (!tech_pvt) {
		return SWITCH_STATUS_FALSE;
	}

	switch_mutex_lock(tech_pvt->mutex);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "(%u) google_transcribe_session_stop\n", tech_pvt->id);

	// Close the session
	if (tech_pvt->pGoogleSession) {
		GStreamer* pGStreamer = (GStreamer*) tech_pvt->pGoogleSession;
		pGStreamer->writesDone();
		
    // start the reaper thread to clean up
    std::thread reaper_thread([tech_pvt](){
			if (tech_pvt->pGoogleSession) {
				GStreamer* pGStreamer = (GStreamer*) tech_pvt->pGoogleSession;
				pGStreamer->finishRead();
				delete pGStreamer;
				tech_pvt->pGoogleSession = nullptr;
			}
		});
		reaper_thread.detach();
	}

  destroy_tech_pvt(tech_pvt);
	
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "(%u) google_transcribe_session_stop\n", tech_pvt->id);
	switch_mutex_unlock(tech_pvt->mutex);

	return SWITCH_STATUS_SUCCESS;
}

extern "C" switch_bool_t google_transcribe_frame(switch_media_bug_t *bug, void* user_data) {
	private_t *tech_pvt = (private_t *) user_data;

	if (!tech_pvt || !tech_pvt->pGoogleSession) return SWITCH_TRUE;

	GStreamer* pGStreamer = (GStreamer*) tech_pvt->pGoogleSession;
	uint8_t data[SWITCH_RECOMMENDED_BUFFER_SIZE];
	switch_frame_t frame = {};
	frame.data = data;
	frame.buflen = SWITCH_RECOMMENDED_BUFFER_SIZE;

	if (switch_mutex_trylock(tech_pvt->mutex) == SWITCH_STATUS_SUCCESS) {
		while (switch_core_media_bug_read(bug, &frame, SWITCH_TRUE) == SWITCH_STATUS_SUCCESS && !switch_test_flag((&frame), SFF_CNG)) {
			if (frame.datalen) {
				// resample if necessary
				if (tech_pvt->resampler) {
					spx_int16_t out[SWITCH_RECOMMENDED_BUFFER_SIZE];
					spx_uint32_t out_len = SWITCH_RECOMMENDED_BUFFER_SIZE;
					spx_uint32_t in_len = frame.samples;
					
					speex_resampler_process_interleaved_int(tech_pvt->resampler, 
						(const spx_int16_t *) frame.data, &in_len, out, &out_len);
					
					pGStreamer->write(out, out_len * sizeof(spx_int16_t));
				} else {
					pGStreamer->write(frame.data, frame.datalen);
				}
			}
		}
		switch_mutex_unlock(tech_pvt->mutex);
	}

	return SWITCH_TRUE;
}

// Export functions with C linkage
extern "C" {

switch_status_t google_transcribe_init() {
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "google_transcribe_init\n");
	return SWITCH_STATUS_SUCCESS;
}

switch_status_t google_transcribe_cleanup() {
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "google_transcribe_cleanup\n");
	return SWITCH_STATUS_SUCCESS;
}

}