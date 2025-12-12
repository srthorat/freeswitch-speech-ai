#ifndef AWS_ASYNC_PUSHER_H
#define AWS_ASYNC_PUSHER_H

#include "transcript_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct async_pusher async_pusher_t;

async_pusher_t* async_pusher_create(const char* app_id, const char* key,
    const char* secret, const char* cluster);
void async_pusher_destroy(async_pusher_t* pusher);
void async_pusher_send(async_pusher_t* pusher, const char* channel,
    const char* event, const char* data);

int async_pusher_send_transcript_parsed(async_pusher_t* pusher,
    const char* call_id,
    const transcript_data_t* td,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number);

int async_pusher_send_session_start(async_pusher_t* pusher,
    const char* call_id,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number);

#ifdef __cplusplus
}
#endif

#endif /* AWS_ASYNC_PUSHER_H */
