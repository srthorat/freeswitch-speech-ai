#ifndef __MOD_AWS_TRANSCRIBE_H__
#define __MOD_AWS_TRANSCRIBE_H__

#include <switch.h>
#include <speex/speex_resampler.h>

#include <unistd.h>

#include "transcript_data.h"

#define MY_BUG_NAME "deepgram_transcribe"
#define TRANSCRIBE_EVENT_RESULTS "deepgram_transcribe::transcription"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED "deepgram_transcribe::no_audio_detected"
#define TRANSCRIBE_EVENT_VAD_DETECTED "deepgram_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_CONNECT_SUCCESS "deepgram_transcribe::connect"
#define TRANSCRIBE_EVENT_CONNECT_FAIL    "deepgram_transcribe::connect_failed"
#define TRANSCRIBE_EVENT_BUFFER_OVERRUN  "deepgram_transcribe::buffer_overrun"
#define TRANSCRIBE_EVENT_DISCONNECT      "deepgram_transcribe::disconnect"
#define TRANSCRIBE_EVENT_SESSION_START   "deepgram_transcribe::session_start"
#define TRANSCRIBE_EVENT_SESSION_STOP    "deepgram_transcribe::session_stop"

#define MAX_LANG (12)
#define MAX_SESSION_ID (256)
#define MAX_WS_URL_LEN (512)
#define MAX_PATH_LEN (4096)
#define MAX_BUG_LEN (64)
#define MAX_METADATA_LEN (8192)

typedef void (*responseHandler_t)(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished);

/* Optimized response handler that accepts pre-parsed transcript data */
typedef void (*responseHandlerParsed_t)(switch_core_session_t* session, const char* eventName, 
    const char* json, const char* bugname, int finished, const transcript_data_t* td);

struct private_data {
	switch_mutex_t *mutex;
	char sessionId[MAX_SESSION_ID];
  SpeexResamplerState *resampler;
  responseHandler_t responseHandler;
  void *pAudioPipe;
  int ws_state;
  char host[MAX_WS_URL_LEN];
  unsigned int port;
  char path[MAX_PATH_LEN];
  char bugname[MAX_BUG_LEN+1];
  char metadata[MAX_METADATA_LEN];
  int sampling;
  int  channels;
  unsigned int id;
  int buffer_overrun_notified:1;
  int is_finished:1;

  /* Resampler performance statistics for high-scale monitoring */
  uint64_t resampler_frames_processed;     /* Total frames processed */
  uint64_t resampler_samples_in;           /* Total input samples */
  uint64_t resampler_samples_out;          /* Total output samples */
  uint64_t resampler_bytes_written;        /* Total bytes sent to transcription */
  uint32_t resampler_source_rate;          /* Source sample rate (codec) */
  uint32_t resampler_target_rate;          /* Target sample rate (requested) */
  switch_time_t resampler_start_time;      /* When resampling started */
  switch_time_t resampler_last_log_time;   /* Last stats log time */
};

typedef struct private_data private_t;

#endif