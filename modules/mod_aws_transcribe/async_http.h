#ifndef AWS_ASYNC_HTTP_H
#define AWS_ASYNC_HTTP_H

#include <stddef.h>
#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct async_http async_http_t;

async_http_t* async_http_create(void);
void async_http_destroy(async_http_t* client);
switch_status_t async_http_post(async_http_t* client, const char* url, const char* body,
    const char* const* headers, size_t header_count);

#ifdef __cplusplus
}
#endif

#endif /* AWS_ASYNC_HTTP_H */
