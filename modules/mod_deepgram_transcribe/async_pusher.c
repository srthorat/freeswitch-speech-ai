/*
 * async_pusher.c - Non-blocking Pusher implementation
 * 
 * HIGH SCALE ARCHITECTURE:
 * - No global state for credentials (supports per-user credentials)
 * - All HTTP calls are non-blocking via async_http
 * - Lock-free statistics via atomic counters
 * - Minimal memory allocation per request
 */

#include "async_pusher.h"
#include "async_http.h"
#include <switch_json.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <time.h>
#include <string.h>
#include <stdatomic.h>

/* Statistics (atomic for lock-free access) */
static atomic_uint_fast64_t g_stat_queued = 0;
static atomic_uint_fast64_t g_stat_sent = 0;
static atomic_uint_fast64_t g_stat_failed = 0;
static atomic_uint_fast64_t g_stat_dropped = 0;

/* Event name defaults */
static const char* DEFAULT_CHANNEL_PREFIX = "call-";
static const char* DEFAULT_EVENT_FINAL = "transcription-final";
static const char* DEFAULT_EVENT_INTERIM = "transcription-interim";
static const char* DEFAULT_EVENT_SESSION_START = "session-start";

/* Helper: convert binary to hex string */
static void bin_to_hex(const unsigned char* data, size_t len, char* out) {
    static const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[data[i] & 0xf];
    }
    out[len * 2] = '\0';
}

/* Helper: HMAC SHA256 to hex */
static void hmac_sha256_hex(const char* key, const char* data, char* out) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)data, strlen(data), digest, &len);
    bin_to_hex(digest, len, out);
}

/* Helper: MD5 to hex */
static void md5_hex(const char* data, char* out) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx) {
        EVP_DigestInit_ex(ctx, EVP_md5(), NULL);
        EVP_DigestUpdate(ctx, data, strlen(data));
        EVP_DigestFinal_ex(ctx, digest, NULL);
        EVP_MD_CTX_free(ctx);
    }
    bin_to_hex(digest, MD5_DIGEST_LENGTH, out);
}

/* Helper: Escape JSON for embedding */
static char* escape_json_string(const char* input) {
    if (!input) return NULL;
    
    size_t len = strlen(input);
    char* output = malloc(len * 2 + 1);
    if (!output) return NULL;
    
    char* p = output;
    for (size_t i = 0; i < len; i++) {
        switch (input[i]) {
            case '"':  *p++ = '\\'; *p++ = '"';  break;
            case '\\': *p++ = '\\'; *p++ = '\\'; break;
            case '\n': *p++ = '\\'; *p++ = 'n';  break;
            case '\r': *p++ = '\\'; *p++ = 'r';  break;
            case '\t': *p++ = '\\'; *p++ = 't';  break;
            default:   *p++ = input[i];          break;
        }
    }
    *p = '\0';
    return output;
}

/* Build signed Pusher URL */
static char* build_pusher_url(
    const char* app_id,
    const char* app_key,
    const char* app_secret,
    const char* cluster,
    const char* body
) {
    /* Calculate body MD5 */
    char body_md5[33];
    md5_hex(body, body_md5);
    
    /* Build timestamp */
    char auth_timestamp[32];
    snprintf(auth_timestamp, sizeof(auth_timestamp), "%ld", (long)time(NULL));
    
    /* Build query string (alphabetical order is required!) */
    char query[512];
    snprintf(query, sizeof(query),
        "auth_key=%s&auth_timestamp=%s&auth_version=1.0&body_md5=%s",
        app_key, auth_timestamp, body_md5);
    
    /* Build string to sign */
    char to_sign[1024];
    snprintf(to_sign, sizeof(to_sign),
        "POST\n/apps/%s/events\n%s",
        app_id, query);
    
    /* Calculate HMAC signature */
    char signature[65];
    hmac_sha256_hex(app_secret, to_sign, signature);
    
    /* Build final URL */
    char* url = malloc(1024);
    if (url) {
        snprintf(url, 1024,
            "https://api-%s.pusher.com/apps/%s/events?%s&auth_signature=%s",
            cluster, app_id, query, signature);
    }
    return url;
}

/* HTTP completion callback */
static void pusher_callback(const char* url, long http_code, const char* response, size_t response_len, void* user_data) {
    (void)url;
    (void)user_data;
    
    if (http_code == 200) {
        atomic_fetch_add(&g_stat_sent, 1);
    } else {
        atomic_fetch_add(&g_stat_failed, 1);
        if (http_code != 0) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "async_pusher: HTTP %ld - %.*s\n",
                http_code, (int)(response_len > 100 ? 100 : response_len), response ? response : "(no body)");
        }
    }
}

/* ============================================================================
 * Public API
 * ============================================================================ */

int async_pusher_init(void) {
    /* Initialize the underlying async HTTP system */
    if (async_http_init() != SWITCH_STATUS_SUCCESS) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "async_pusher: Failed to initialize async HTTP\n");
        return -1;
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "async_pusher: Initialized (non-blocking HTTP enabled)\n");
    return 0;
}

void async_pusher_shutdown(void) {
    async_http_shutdown();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "async_pusher: Shutdown - queued=%lu sent=%lu failed=%lu dropped=%lu\n",
        (unsigned long)g_stat_queued,
        (unsigned long)g_stat_sent,
        (unsigned long)g_stat_failed,
        (unsigned long)g_stat_dropped);
}

int async_pusher_send_transcription(
    const char* app_id,
    const char* app_key,
    const char* app_secret,
    const char* cluster,
    const char* call_id,
    const char* json,
    int is_final,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number
) {
    if (!app_id || !app_key || !app_secret || !call_id || !json) {
        return -1;
    }
    
    /* Parse Deepgram JSON to extract transcript and metadata */
    cJSON* root = cJSON_Parse(json);
    if (!root) return -1;
    
    const char* transcript = NULL;
    int speaker_channel = 0;
    double confidence = 0.0;
    double start_time = 0.0;
    double duration = 0.0;
    int speech_final = 0;
    
    /* Check if this is a speech_final (end of utterance) */
    cJSON* speech_final_obj = cJSON_GetObjectItem(root, "speech_final");
    if (speech_final_obj && cJSON_IsBool(speech_final_obj)) {
        speech_final = cJSON_IsTrue(speech_final_obj);
    }
    
    /* Extract transcript from Deepgram format */
    cJSON* channel_obj = cJSON_GetObjectItem(root, "channel");
    if (channel_obj) {
        cJSON* alternatives = cJSON_GetObjectItem(channel_obj, "alternatives");
        if (alternatives && cJSON_IsArray(alternatives) && cJSON_GetArraySize(alternatives) > 0) {
            cJSON* first_alt = cJSON_GetArrayItem(alternatives, 0);
            cJSON* transcript_field = cJSON_GetObjectItem(first_alt, "transcript");
            if (transcript_field && cJSON_IsString(transcript_field)) {
                transcript = cJSON_GetStringValue(transcript_field);
            }
            /* Get confidence score */
            cJSON* conf = cJSON_GetObjectItem(first_alt, "confidence");
            if (conf && cJSON_IsNumber(conf)) {
                confidence = conf->valuedouble;
            }
        }
    }
    
    /* Get channel index for speaker mapping */
    cJSON* channel_index = cJSON_GetObjectItem(root, "channel_index");
    if (channel_index && cJSON_IsArray(channel_index) && cJSON_GetArraySize(channel_index) > 0) {
        cJSON* idx = cJSON_GetArrayItem(channel_index, 0);
        if (idx && cJSON_IsNumber(idx)) {
            speaker_channel = (int)idx->valuedouble;
        }
    }
    
    /* Get timing information */
    cJSON* start_obj = cJSON_GetObjectItem(root, "start");
    cJSON* duration_obj = cJSON_GetObjectItem(root, "duration");
    if (start_obj && cJSON_IsNumber(start_obj)) start_time = start_obj->valuedouble;
    if (duration_obj && cJSON_IsNumber(duration_obj)) duration = duration_obj->valuedouble;
    
    /* Skip empty transcripts */
    if (!transcript || strlen(transcript) == 0) {
        cJSON_Delete(root);
        return 0; /* Not an error, just nothing to send */
    }
    
    /* Build speaker ID from channel mapping 
     * Channel 0 = Caller (person who initiated the call)
     * Channel 1 = Callee (person who answered) */
    char speaker_id[256];
    char speaker_role[32];
    if (speaker_channel == 0) {
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            caller_name ? caller_name : "Unknown",
            caller_number ? caller_number : "Unknown");
        strcpy(speaker_role, "caller");
    } else {
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            callee_name ? callee_name : "Unknown",
            callee_number ? callee_number : "Unknown");
        strcpy(speaker_role, "callee");
    }
    
    /* Build timestamp */
    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    
    /* Build simple Pusher data JSON: type, speaker_id, text, timestamp */
    cJSON* pusher_data = cJSON_CreateObject();
    cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
    cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id);
    cJSON_AddStringToObject(pusher_data, "text", transcript);
    cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);
    
    char* data_json = cJSON_PrintUnformatted(pusher_data);
    cJSON_Delete(pusher_data);
    cJSON_Delete(root);
    
    if (!data_json) return -1;
    
    /* Escape for embedding in outer JSON */
    char* escaped = escape_json_string(data_json);
    free(data_json);
    if (!escaped) return -1;
    
    /* Get event names from environment or use defaults */
    const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
    const char* event_final = getenv("PUSHER_EVENT_FINAL");
    const char* event_interim = getenv("PUSHER_EVENT_INTERIM");
    if (!channel_prefix) channel_prefix = DEFAULT_CHANNEL_PREFIX;
    if (!event_final) event_final = DEFAULT_EVENT_FINAL;
    if (!event_interim) event_interim = DEFAULT_EVENT_INTERIM;
    
    /* Build Pusher channel name */
    char pusher_channel[256];
    snprintf(pusher_channel, sizeof(pusher_channel), "%s%s", channel_prefix, call_id);
    
    /* Build request body */
    char body[8192];
    snprintf(body, sizeof(body),
        "{\"name\":\"%s\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
        is_final ? event_final : event_interim,
        pusher_channel, escaped);
    free(escaped);
    
    /* Build signed URL */
    char* url = build_pusher_url(app_id, app_key, app_secret, cluster, body);
    if (!url) return -1;
    
    /* Queue async HTTP POST */
    switch_status_t result = async_http_post_json(url, body, pusher_callback, NULL);
    free(url);
    
    if (result == SWITCH_STATUS_SUCCESS) {
        atomic_fetch_add(&g_stat_queued, 1);
        return 0;
    } else {
        atomic_fetch_add(&g_stat_dropped, 1);
        return -1;
    }
}

int async_pusher_send_session_start(
    const char* app_id,
    const char* app_key,
    const char* app_secret,
    const char* cluster,
    const char* call_id,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number
) {
    if (!app_id || !app_key || !app_secret || !call_id) {
        return -1;
    }
    
    /* Build caller/callee ID strings */
    char caller_id[256], callee_id[256];
    snprintf(caller_id, sizeof(caller_id), "%s(%s)",
        caller_name ? caller_name : "Unknown",
        caller_number ? caller_number : "Unknown");
    snprintf(callee_id, sizeof(callee_id), "%s(%s)",
        callee_name ? callee_name : "Unknown",
        callee_number ? callee_number : "Unknown");
    
    /* Build timestamp */
    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    
    /* Build session start JSON */
    cJSON* data = cJSON_CreateObject();
    cJSON_AddStringToObject(data, "type", "session_start");
    cJSON_AddStringToObject(data, "caller_id", caller_id);
    cJSON_AddStringToObject(data, "callee_id", callee_id);
    cJSON_AddStringToObject(data, "timestamp", timestamp);
    
    char* data_json = cJSON_PrintUnformatted(data);
    cJSON_Delete(data);
    if (!data_json) return -1;
    
    char* escaped = escape_json_string(data_json);
    free(data_json);
    if (!escaped) return -1;
    
    /* Get event names from environment or use defaults */
    const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
    const char* event_session_start = getenv("PUSHER_EVENT_SESSION_START");
    if (!channel_prefix) channel_prefix = DEFAULT_CHANNEL_PREFIX;
    if (!event_session_start) event_session_start = DEFAULT_EVENT_SESSION_START;
    
    /* Build Pusher channel name */
    char pusher_channel[256];
    snprintf(pusher_channel, sizeof(pusher_channel), "%s%s", channel_prefix, call_id);
    
    /* Build request body - note: single channel, not array */
    char body[2048];
    snprintf(body, sizeof(body),
        "{\"name\":\"%s\",\"channel\":\"%s\",\"data\":\"%s\"}",
        event_session_start, pusher_channel, escaped);
    free(escaped);
    
    /* Build signed URL */
    char* url = build_pusher_url(app_id, app_key, app_secret, cluster, body);
    if (!url) return -1;
    
    /* Queue async HTTP POST */
    switch_status_t result = async_http_post_json(url, body, pusher_callback, NULL);
    free(url);
    
    if (result == SWITCH_STATUS_SUCCESS) {
        atomic_fetch_add(&g_stat_queued, 1);
        return 0;
    } else {
        atomic_fetch_add(&g_stat_dropped, 1);
        return -1;
    }
}

void async_pusher_get_stats(async_pusher_stats_t* stats) {
    if (!stats) return;
    stats->queued = atomic_load(&g_stat_queued);
    stats->sent = atomic_load(&g_stat_sent);
    stats->failed = atomic_load(&g_stat_failed);
    stats->dropped = atomic_load(&g_stat_dropped);
}

/* ============================================================================
 * OPTIMIZED: Send transcript with pre-parsed data (no JSON parsing needed)
 * ============================================================================ */
int async_pusher_send_transcript_parsed(
    const char* app_id,
    const char* app_key,
    const char* app_secret,
    const char* cluster,
    const char* call_id,
    const char* transcript,
    int is_final,
    int channel_index,
    const char* caller_name,
    const char* caller_number,
    const char* callee_name,
    const char* callee_number
) {
    if (!app_id || !app_key || !app_secret || !call_id || !transcript || strlen(transcript) == 0) {
        return -1;
    }
    
    /* Build speaker ID from channel mapping 
     * Channel 0 = Caller (person who initiated the call)
     * Channel 1 = Callee (person who answered) */
    char speaker_id[256];
    if (channel_index == 0) {
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            caller_name ? caller_name : "Unknown",
            caller_number ? caller_number : "Unknown");
    } else {
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            callee_name ? callee_name : "Unknown",
            callee_number ? callee_number : "Unknown");
    }
    
    /* Build timestamp */
    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);
    
    /* Build simple Pusher data JSON: type, speaker_id, text, timestamp */
    cJSON* pusher_data = cJSON_CreateObject();
    cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
    cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id);
    cJSON_AddStringToObject(pusher_data, "text", transcript);
    cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);
    
    char* data_json = cJSON_PrintUnformatted(pusher_data);
    cJSON_Delete(pusher_data);
    
    if (!data_json) return -1;
    
    /* Escape for embedding in outer JSON */
    char* escaped = escape_json_string(data_json);
    free(data_json);
    if (!escaped) return -1;
    
    /* Get event names from environment or use defaults */
    const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
    const char* event_final = getenv("PUSHER_EVENT_FINAL");
    const char* event_interim = getenv("PUSHER_EVENT_INTERIM");
    if (!channel_prefix) channel_prefix = DEFAULT_CHANNEL_PREFIX;
    if (!event_final) event_final = DEFAULT_EVENT_FINAL;
    if (!event_interim) event_interim = DEFAULT_EVENT_INTERIM;
    
    /* Build Pusher channel name */
    char pusher_channel[256];
    snprintf(pusher_channel, sizeof(pusher_channel), "%s%s", channel_prefix, call_id);
    
    /* Build request body */
    char body[8192];
    snprintf(body, sizeof(body),
        "{\"name\":\"%s\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
        is_final ? event_final : event_interim,
        pusher_channel, escaped);
    free(escaped);
    
    /* Build signed URL */
    char* url = build_pusher_url(app_id, app_key, app_secret, cluster, body);
    if (!url) return -1;
    
    /* Queue async HTTP POST */
    switch_status_t result = async_http_post_json(url, body, pusher_callback, NULL);
    free(url);
    
    if (result == SWITCH_STATUS_SUCCESS) {
        atomic_fetch_add(&g_stat_queued, 1);
        return 0;
    } else {
        atomic_fetch_add(&g_stat_dropped, 1);
        return -1;
    }
}