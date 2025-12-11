#include "async_pusher.h"

#include "async_http.h"

#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

struct async_pusher {
    char* app_id;
    char* key;
    char* secret;
    char* cluster;
    async_http_t* http;
};

static char* dup_string(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char* copy = (char*)malloc(len + 1);
    if (copy) {
        memcpy(copy, src, len + 1);
    }
    return copy;
}

static char* escape_json(const char* input) {
    if (!input) return dup_string("");
    size_t len = strlen(input);
    char* out = (char*)malloc(len * 2 + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = input[i];
        switch (c) {
            case '"': out[j++] = '\\'; out[j++] = '"'; break;
            case '\\': out[j++] = '\\'; out[j++] = '\\'; break;
            case '\n': out[j++] = '\\'; out[j++] = 'n'; break;
            case '\r': out[j++] = '\\'; out[j++] = 'r'; break;
            case '\t': out[j++] = '\\'; out[j++] = 't'; break;
            default: out[j++] = c; break;
        }
    }
    out[j] = '\0';
    return out;
}

static void bin_to_hex(const unsigned char* data, size_t len, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        out[i * 2] = hex[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static void md5_hex(const char* input, char* out) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;
    MD5_Init(&ctx);
    MD5_Update(&ctx, (const unsigned char*)input, strlen(input));
    MD5_Final(digest, &ctx);
    bin_to_hex(digest, MD5_DIGEST_LENGTH, out);
}

static void hmac_sha256_hex(const char* key, const char* data, char* out) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key, (int)strlen(key), (const unsigned char*)data, strlen(data), digest, &len);
    bin_to_hex(digest, len, out);
}

async_pusher_t* async_pusher_create(const char* app_id, const char* key,
    const char* secret, const char* cluster) {
    if (!app_id || !key || !secret || !cluster) {
        return NULL;
    }

    async_pusher_t* pusher = (async_pusher_t*)calloc(1, sizeof(async_pusher_t));
    if (!pusher) {
        return NULL;
    }

    pusher->app_id = dup_string(app_id);
    pusher->key = dup_string(key);
    pusher->secret = dup_string(secret);
    pusher->cluster = dup_string(cluster);
    pusher->http = async_http_create();

    if (!pusher->app_id || !pusher->key || !pusher->secret || !pusher->cluster || !pusher->http) {
        async_pusher_destroy(pusher);
        return NULL;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "AsyncPusher initialized for app_id=%s, cluster=%s\n", app_id, cluster);
    return pusher;
}

void async_pusher_destroy(async_pusher_t* pusher) {
    if (!pusher) return;

    if (pusher->http) {
        async_http_destroy(pusher->http);
    }
    free(pusher->app_id);
    free(pusher->key);
    free(pusher->secret);
    free(pusher->cluster);
    free(pusher);
}

void async_pusher_send(async_pusher_t* pusher, const char* channel,
    const char* event, const char* data) {
    if (!pusher || !channel || !event || !data) {
        return;
    }

    char* escaped_data = escape_json(data);
    if (!escaped_data) {
        return;
    }

    size_t body_len = strlen(event) + strlen(channel) + strlen(escaped_data) + 64;
    char* body = (char*)malloc(body_len);
    if (!body) {
        free(escaped_data);
        return;
    }
    snprintf(body, body_len, "{\"name\":\"%s\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
        event, channel, escaped_data);

    free(escaped_data);

    char body_md5[MD5_DIGEST_LENGTH * 2 + 1];
    md5_hex(body, body_md5);

    long timestamp = (long)time(NULL);

    char query[512];
    snprintf(query, sizeof(query),
        "auth_key=%s&auth_timestamp=%ld&auth_version=1.0&body_md5=%s",
        pusher->key, timestamp, body_md5);

    char to_sign[1024];
    snprintf(to_sign, sizeof(to_sign),
        "POST\n/apps/%s/events\n%s", pusher->app_id, query);

    char signature[EVP_MAX_MD_SIZE * 2 + 1];
    hmac_sha256_hex(pusher->secret, to_sign, signature);

    char url[1024];
    snprintf(url, sizeof(url),
        "https://api-%s.pusher.com/apps/%s/events?%s&auth_signature=%s",
        pusher->cluster, pusher->app_id, query, signature);

    const char* headers[] = {
        "Content-Type: application/json",
        "User-Agent: FreeSWITCH-AWS-Transcribe/1.0",
        "Connection: keep-alive"
    };

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "Pusher request body: %s\n", body);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "Pusher URL: %s\n", url);

    async_http_post(pusher->http, url, body, headers, sizeof(headers) / sizeof(headers[0]));

    free(body);
}
