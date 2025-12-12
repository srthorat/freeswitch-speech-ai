#include "async_pusher.h"

#include "async_http.h"

#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <switch.h>

static const char* DEFAULT_CHANNEL_PREFIX = "call-";
static const char* DEFAULT_EVENT_FINAL = "transcription-final";
static const char* DEFAULT_EVENT_INTERIM = "transcription-interim";
static const char* DEFAULT_EVENT_SESSION_START = "session-start";

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

static void async_pusher_post(async_pusher_t* pusher, const char* body) {
    char body_md5[MD5_DIGEST_LENGTH * 2 + 1];
    md5_hex(body, body_md5);

    long timestamp = (long)time(NULL);

    char query[512];
    snprintf(query, sizeof(query),
        "auth_key=%s&auth_timestamp=%ld&auth_version=1.0&body_md5=%s",
        pusher->key, timestamp, body_md5);

    char to_sign[1024];
    snprintf(to_sign, sizeof(to_sign),
        "POST\n/apps/%s/events\n%s",
        pusher->app_id, query);

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

    async_pusher_post(pusher, body);

    free(body);
}

static const char* resolve_env_default(const char* value, const char* fallback) {
    return (value && *value) ? value : fallback;
}

int async_pusher_send_transcript_parsed(async_pusher_t* pusher,
    const char* call_id,
    const transcript_data_t* td,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number) {

    if (!pusher || !call_id || !td || !td->has_transcript || strlen(td->transcript) == 0) {
        return -1;
    }

    char speaker_id[256];
    if (td->channel_index == 0) {
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            caller_name ? caller_name : "Unknown",
            caller_number ? caller_number : "Unknown");
    } else {
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            callee_name ? callee_name : "Unknown",
            callee_number ? callee_number : "Unknown");
    }

    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    char* speaker_escaped = escape_json(speaker_id);
    char* text_escaped = escape_json(td->transcript);
    if (!speaker_escaped || !text_escaped) {
        free(speaker_escaped);
        free(text_escaped);
        return -1;
    }

    const char* type_str = td->is_final ? "final" : "interim";
    size_t data_len = strlen(type_str) + strlen(speaker_escaped) + strlen(text_escaped) + strlen(timestamp) + 128;
    char* data_json = (char*)malloc(data_len);
    if (!data_json) {
        free(speaker_escaped);
        free(text_escaped);
        return -1;
    }

    snprintf(data_json, data_len,
        "{\"type\":\"%s\",\"speaker_id\":\"%s\",\"text\":\"%s\",\"timestamp\":\"%s\"}",
        type_str, speaker_escaped, text_escaped, timestamp);

    free(speaker_escaped);
    free(text_escaped);

    const char* prefix = resolve_env_default(getenv("PUSHER_CHANNEL_PREFIX"), DEFAULT_CHANNEL_PREFIX);
    char channel_name[256];
    snprintf(channel_name, sizeof(channel_name), "%s%s", prefix, call_id);

    const char* event_final = resolve_env_default(getenv("PUSHER_EVENT_FINAL"), DEFAULT_EVENT_FINAL);
    const char* event_interim = resolve_env_default(getenv("PUSHER_EVENT_INTERIM"), DEFAULT_EVENT_INTERIM);
    const char* event_name = td->is_final ? event_final : event_interim;

    async_pusher_send(pusher, channel_name, event_name, data_json);
    free(data_json);

    return 0;
}

int async_pusher_send_session_start(async_pusher_t* pusher,
    const char* call_id,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number) {

    if (!pusher || !call_id) {
        return -1;
    }

    char caller_id[256];
    char callee_id[256];
    snprintf(caller_id, sizeof(caller_id), "%s(%s)",
        caller_name ? caller_name : "Unknown",
        caller_number ? caller_number : "Unknown");
    snprintf(callee_id, sizeof(callee_id), "%s(%s)",
        callee_name ? callee_name : "Unknown",
        callee_number ? callee_number : "Unknown");

    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    char* caller_escaped = escape_json(caller_id);
    char* callee_escaped = escape_json(callee_id);
    if (!caller_escaped || !callee_escaped) {
        free(caller_escaped);
        free(callee_escaped);
        return -1;
    }

    size_t data_len = strlen(caller_escaped) + strlen(callee_escaped) + strlen(timestamp) + 128;
    char* data_json = (char*)malloc(data_len);
    if (!data_json) {
        free(caller_escaped);
        free(callee_escaped);
        return -1;
    }

    snprintf(data_json, data_len,
        "{\"type\":\"session_start\",\"caller_id\":\"%s\",\"callee_id\":\"%s\",\"timestamp\":\"%s\"}",
        caller_escaped, callee_escaped, timestamp);

    free(caller_escaped);
    free(callee_escaped);

    const char* prefix = resolve_env_default(getenv("PUSHER_CHANNEL_PREFIX"), DEFAULT_CHANNEL_PREFIX);
    char channel_name[256];
    snprintf(channel_name, sizeof(channel_name), "%s%s", prefix, call_id);

    const char* event_session_start = resolve_env_default(getenv("PUSHER_EVENT_SESSION_START"), DEFAULT_EVENT_SESSION_START);

    async_pusher_send(pusher, channel_name, event_session_start, data_json);
    free(data_json);

    return 0;
}
