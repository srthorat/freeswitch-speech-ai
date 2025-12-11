#ifndef AWS_ASYNC_PUSHER_H
#define AWS_ASYNC_PUSHER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct async_pusher async_pusher_t;

async_pusher_t* async_pusher_create(const char* app_id, const char* key,
    const char* secret, const char* cluster);
void async_pusher_destroy(async_pusher_t* pusher);
void async_pusher_send(async_pusher_t* pusher, const char* channel,
    const char* event, const char* data);

#ifdef __cplusplus
}
#endif

#endif /* AWS_ASYNC_PUSHER_H */
