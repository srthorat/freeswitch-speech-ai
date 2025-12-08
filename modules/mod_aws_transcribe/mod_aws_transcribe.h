#ifndef __MOD_AWS_TRANSCRIBE_H__
#define __MOD_AWS_TRANSCRIBE_H__

#include <switch.h>
#include "memory_pool.hpp"

// Forward declaration
class AwsPipe;

extern deepgram::ObjectPool<AwsPipe> g_pipe_pool;

#ifdef __cplusplus
extern "C" {
#endif

// Event subclasses
#define TRANSCRIBE_EVENT_RESULTS					"aws_transcribe::transcription"
#define TRANSCRIBE_EVENT_RESULTS_FINAL				"aws_transcribe::final_transcription"
#define TRANSCRIBE_EVENT_ERROR						"aws_transcribe::error"
#define TRANSCRIBE_EVENT_VAD_DETECTED				"aws_transcribe::vad_detected"
#define TRANSCRIBE_EVENT_SESSION_START				"aws_transcribe::session_start"
#define TRANSCRIBE_EVENT_SESSION_STOP				"aws_transcribe::session_stop"
#define TRANSCRIBE_EVENT_END_OF_TRANSCRIPT			"aws_transcribe::end_of_transcript"
#define TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED		"aws_transcribe::max_duration_exceeded"
#define TRANSCRIBE_EVENT_NO_AUDIO_DETECTED			"aws_transcribe::no_audio_detected"

#ifdef __cplusplus
}
#endif

#endif // __MOD_AWS_TRANSCRIBE_H__