#ifndef __MOD_GOOGLE_TRANSCRIBEV2_H__
#define __MOD_GOOGLE_TRANSCRIBEV2_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#define MY_BUG_NAME "google_transcribev2"
#define TRANSCRIBE_EVENT_RESULTS "google_transcribev2::transcription"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "google_transcribev2::no_audio_detected"
#define TRANSCRIBE_EVENT_VAD_DETECTED "google_transcribev2::vad_detected"
#define TRANSCRIBE_EVENT_CONNECT_SUCCESS "google_transcribev2::connect"
#define TRANSCRIBE_EVENT_CONNECT_FAIL    "google_transcribev2::connect_failed"
#define TRANSCRIBE_EVENT_BUFFER_OVERRUN  "google_transcribev2::buffer_overrun"
#define TRANSCRIBE_EVENT_DISCONNECT      "google_transcribev2::disconnect"
#define TRANSCRIBE_EVENT_SESSION_START   "google_transcribev2::session_start"
#define TRANSCRIBE_EVENT_SESSION_STOP    "google_transcribev2::session_stop"

#define MAX_LANG (12)
#define MAX_SESSION_ID (256)
#define MAX_PROJECT_ID (256)
#define MAX_LOCATION (64)
#define MAX_PATH_LEN (4096)
#define MAX_BUG_LEN (64)
#define MAX_METADATA_LEN (8192)

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished);

struct private_data {
	switch_mutex_t *mutex;
	char sessionId[MAX_SESSION_ID];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  void *pGoogleSession;  // Pointer to C++ GoogleTranscribeSession
  int streaming_state;
  char project_id[MAX_PROJECT_ID];
  char location[MAX_LOCATION];
  char bugname[MAX_BUG_LEN+1];
  char metadata[MAX_METADATA_LEN];
  int sampling;
  int  channels;
  unsigned int id;
  int buffer_overrun_notified:1;
  int is_finished:1;
};

typedef struct private_data private_t;

#endif
