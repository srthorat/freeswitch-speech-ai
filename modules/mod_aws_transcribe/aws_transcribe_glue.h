#ifndef __AWS_GLUE_H__
#define __AWS_GLUE_H__

#include <switch.h>
#include <string>
#include <functional>
#include <memory>
#include "transcript_data.h"
#include "audio_pipe.h"

// Define the response handler callback type
typedef std::function<void(switch_core_session_t*, const transcript_data_t*, const char*)> ResponseHandler_t;

// Session management functions
extern "C" {
switch_status_t aws_transcribe_session_init(switch_core_session_t *session, ResponseHandler_t responseHandler, uint32_t samples_per_second, uint32_t channels, const char* lang, int interim, const char* bugname, const char* metadata, void **ppUserData, switch_media_bug_flag_t flags);
switch_status_t aws_transcribe_session_stop(switch_core_session_t *session, int channelIsClosing, char* bugname);
switch_bool_t aws_transcribe_frame(switch_media_bug_t *bug, void* user_data, switch_abc_type_t type);
}

#endif // __AWS_GLUE_H__