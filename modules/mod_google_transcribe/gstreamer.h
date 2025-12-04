#include <cstdlib>
#include <algorithm>
#include <future>
#include <chrono>

#include <switch.h>
#include <switch_json.h>
#include <grpc++/grpc++.h>
#include <grpcpp/impl/codegen/sync_stream.h>

#include "mod_google_transcribe.h"
#include "simple_buffer.h"

// Chunk size for sending to Google Speech API
// Python reference uses 100ms chunks which works well for multichannel
// At 8kHz stereo (2 channels, 16-bit): 8000 * 2 * 2 * 0.1 = 3200 bytes per 100ms
// At 16kHz stereo: 16000 * 2 * 2 * 0.1 = 6400 bytes per 100ms
// We use a fixed 100ms worth of 8kHz stereo as our target chunk
#define CHUNKSIZE_100MS_8KHZ_STEREO (3200)  // 100ms @ 8kHz stereo
#define CHUNKSIZE_100MS_16KHZ_STEREO (6400) // 100ms @ 16kHz stereo
#define CHUNKSIZE (320)  // Legacy: 20ms @ 8kHz mono for buffer granularity

// Accumulation buffer size - hold up to 100ms of audio before sending
// This matches Python's approach of sending larger, consistent chunks
#define ACCUMULATION_BUFFER_SIZE (CHUNKSIZE_100MS_16KHZ_STEREO)

namespace {
  int case_insensitive_match(std::string s1, std::string s2) {
   std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
   std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
   if(s1.compare(s2) == 0)
      return 1; //The strings are same
   return 0; //not matched
  }
}

template <typename Request, typename Response, typename Stub>
class GStreamer {
public:
	GStreamer(
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
	const char* hints);

	~GStreamer() {
		//switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(m_session), SWITCH_LOG_INFO, "GStreamer::~GStreamer - deleting channel and stub: %p\n", (void*)this);
	}

	bool write(void* data, uint32_t datalen);
	
	// Flush any accumulated audio - must be called before writesDone for V2 API
	void flushAccumulatedAudio();

	void connect() {
		assert(!m_connected);
		
		// Warmup: Wait for gRPC channel to be ready (connection establishment)
		// Reduced timeout to 800ms for faster connection
		auto deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(800);
		bool channel_ready = m_channel->WaitForConnected(deadline);
		if (channel_ready) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer %p gRPC channel warmed up and ready\n", this);
		} else {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "GStreamer %p gRPC channel warmup timeout (800ms), proceeding anyway\n", this);
		}
		
		// Begin a stream.
		m_streamer = m_stub->StreamingRecognize(&m_context);
		m_connected = true;

		// read thread is waiting on this
		m_promise.set_value();

		// Write the first request, containing the config only.
		m_streamer->Write(m_request);
		
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "GStreamer %p stream ready - zero-latency direct write mode\n", this);
	}

	uint32_t nextMessageSize(void) {
		uint32_t size = 0;
		m_streamer->NextMessageSize(&size);
		return size;
	}

	bool read(Response* response) {
		return m_streamer->Read(response);
	}

	grpc::Status finish() {
		return m_streamer->Finish();
	}

	void writesDone() {
		// grpc crashes if we call this twice on a stream
		if (!m_connected) {
			cancelConnect();
		}
		else if (!m_writesDone) {
			m_streamer->WritesDone();
			m_writesDone = true;
		}
	}

	bool waitForConnect() {
		std::shared_future<void> sf(m_promise.get_future());
		sf.wait();
		return m_connected;
	}

	void cancelConnect() {
		assert(!m_connected);
		m_promise.set_value();
	}

	bool isConnected() {
		return m_connected;
	}

private:
	std::shared_ptr<grpc::Channel> create_grpc_channel(switch_channel_t *channel) {
	    const char* google_uri;
		if (!(google_uri = switch_channel_get_variable(channel, "GOOGLE_SPEECH_TO_TEXT_URI"))) {
			google_uri = "speech.googleapis.com";
		}

	    const char* var;
		if (var = switch_channel_get_variable(channel, "GOOGLE_APPLICATION_CREDENTIALS")) {
			auto channelCreds = grpc::SslCredentials(grpc::SslCredentialsOptions());
			auto callCreds = grpc::ServiceAccountJWTAccessCredentials(var);
			auto creds = grpc::CompositeChannelCredentials(channelCreds, callCreds);
			return grpc::CreateChannel(google_uri, creds);
		}
		else {
			auto creds = grpc::GoogleDefaultCredentials();
			return grpc::CreateChannel(google_uri, creds);
		}
	}

	switch_core_session_t* m_session;
	grpc::ClientContext m_context;
	std::shared_ptr<grpc::Channel> m_channel;
	std::unique_ptr<Stub> 	m_stub;
	std::unique_ptr< grpc::ClientReaderWriterInterface<Request, Response> > m_streamer;
	Request m_request;
	bool m_writesDone;
	bool m_connected;
	std::promise<void> m_promise;
	SimpleBuffer m_audioBuffer;
	
	// Accumulation buffer for batching small frames into larger chunks
	// Google V2 API processes multichannel audio more efficiently with ~100ms chunks
	std::vector<uint8_t> m_accumBuffer;
	static const size_t ACCUMULATE_TARGET = 3200;  // 100ms at 8kHz stereo (8000 * 0.1 * 2 * 2)
};
