# Google Module: Pusher Integration Implementation Guide

**Date:** 2025-11-25
**Status:** Implementation-Ready
**Based on:** Deepgram (commit 3c62d4c, 1cb4bd5) and AWS implementations

---

## 📋 Summary of Existing Implementations

### **Common Pattern Across AWS & Deepgram:**

1. ✅ Helper functions: `bin_to_hex()`, `hmac_sha256_hex()`, `md5_hex()`
2. ✅ Response capture: `pusher_curl_write_cb()` with `struct pusher_response`
3. ✅ Session start: `send_session_start_to_pusher()` - sends caller/callee metadata
4. ✅ Transcript sending: `send_to_pusher()` - sends interim/final with speaker tagging
5. ✅ Call ID fallback: sip_call_id → uuid → call_uuid
6. ✅ Error handling: HTTP status code checking, detailed logging

### **Key Differences - Deepgram vs AWS vs Google:**

| Feature | Deepgram | AWS | Google V2 |
|---------|----------|-----|-----------|
| **Channel Field** | `channel_index` (array) | `channel_id` (string) | `channel_tag` (int) |
| **Transcript Path** | `channel.alternatives[0].transcript` | Direct `transcript` field | `alternatives[0].transcript` |
| **Speaker Detection** | Channel index + diarization | Channel ID + speaker labels | channel_tag + diarization |
| **JSON Parsing** | Parse Deepgram format | Parse AWS format | Parse Google V2 format |

---

## 🎯 Implementation for Google Module

### **1. Required Headers** (mod_google_transcribe.c)

Add after existing includes:

```c
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <time.h>
```

### **2. Pusher Response Structure**

Add before helper functions:

```c
struct pusher_response {
    char* data;
    size_t size;
};
```

### **3. Helper Functions** (~80 lines)

```c
// Curl write callback to capture Pusher API response
static size_t pusher_curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct pusher_response* resp = (struct pusher_response*)userp;

    char* ptr = realloc(resp->data, resp->size + realsize + 1);
    if (!ptr) return 0;

    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->data[resp->size] = 0;

    return realsize;
}

// Convert binary data to hex string
static void bin_to_hex(const unsigned char* data, size_t len, char* out) {
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0xf];
        out[i * 2 + 1] = hex[data[i] & 0xf];
    }
    out[len * 2] = '\0';
}

// HMAC SHA256
static void hmac_sha256_hex(const char* key, const char* data, char* out) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)data, strlen(data), digest, &len);
    bin_to_hex(digest, len, out);
}

// MD5 hex
static void md5_hex(const char* data, char* out) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((unsigned char*)data, strlen(data), digest);
    bin_to_hex(digest, MD5_DIGEST_LENGTH, out);
}
```

### **4. send_to_pusher() Function** (~180 lines)

**Key Google-Specific Adaptations:**
- Parse `channel_tag` from Google V2 response (0 or 1)
- Extract transcript from `alternatives[0].transcript`
- Map `channel_tag` to speaker (0=caller, 1=callee)
- Skip empty transcripts

```c
static void send_to_pusher(switch_core_session_t* session, const char* json, const char* callId, switch_bool_t is_final) {
    if (!json || !callId) return;

    // Get Pusher credentials
    const char* app_id = getenv("PUSHER_APP_ID");
    const char* app_key = getenv("PUSHER_KEY");
    const char* app_secret = getenv("PUSHER_SECRET");
    const char* cluster = getenv("PUSHER_CLUSTER");

    if (!app_id || !app_key || !app_secret) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
            "Pusher not configured - skipping transcript event\n");
        return;
    }
    if (!cluster) cluster = "ap2";

    const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
    const char* event_final = getenv("PUSHER_EVENT_FINAL");
    const char* event_interim = getenv("PUSHER_EVENT_INTERIM");
    if (!channel_prefix) channel_prefix = "call-";
    if (!event_final) event_final = "transcript-final";
    if (!event_interim) event_interim = "transcript-interim";

    // Build channel name
    char channel[256];
    snprintf(channel, sizeof(channel), "%s%s", channel_prefix, callId);

    // Get caller/callee metadata for speaker mapping
    switch_channel_t *chan = switch_core_session_get_channel(session);
    const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
    const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
    const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
    if (!callee_name) callee_name = switch_channel_get_variable(chan, "effective_callee_id_name");
    const char* callee_number = switch_channel_get_variable(chan, "destination_number");
    if (!callee_number) callee_number = switch_channel_get_variable(chan, "callee_id_number");

    // Parse Google V2 response JSON
    cJSON* root = cJSON_Parse(json);
    if (!root) return;

    const char* transcript = NULL;
    const char* language_code = NULL;
    int channel_tag = 0; // Default to channel 0 (caller)

    // Extract from Google V2 format: alternatives[0].transcript
    cJSON* alternatives = cJSON_GetObjectItem(root, "alternatives");
    if (alternatives && cJSON_IsArray(alternatives) && cJSON_GetArraySize(alternatives) > 0) {
        cJSON* first_alt = cJSON_GetArrayItem(alternatives, 0);
        cJSON* transcript_field = cJSON_GetObjectItem(first_alt, "transcript");
        if (transcript_field && cJSON_IsString(transcript_field)) {
            transcript = cJSON_GetStringValue(transcript_field);
        }
    }

    // Extract language_code
    cJSON* lang_field = cJSON_GetObjectItem(root, "language_code");
    if (lang_field && cJSON_IsString(lang_field)) {
        language_code = cJSON_GetStringValue(lang_field);
    }

    // Extract channel_tag (Google V2: 0 or 1)
    cJSON* channel_tag_field = cJSON_GetObjectItem(root, "channel_tag");
    if (channel_tag_field && cJSON_IsNumber(channel_tag_field)) {
        channel_tag = (int)channel_tag_field->valuedouble;
    }

    // Don't send if transcript is empty
    if (!transcript || strlen(transcript) == 0) {
        cJSON_Delete(root);
        return;
    }

    // Map channel_tag to speaker_id
    char speaker_id[256];
    if (channel_tag == 0) {
        // Channel 0 = Caller
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            caller_name ? caller_name : "Unknown",
            caller_number ? caller_number : "Unknown");
    } else {
        // Channel 1 = Callee
        snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
            callee_name ? callee_name : "Unknown",
            callee_number ? callee_number : "Unknown");
    }

    // Get timestamp
    time_t now = time(NULL);
    struct tm tm_info;
    gmtime_r(&now, &tm_info);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

    // Build Pusher data JSON
    cJSON* pusher_data = cJSON_CreateObject();
    cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
    cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id);
    cJSON_AddStringToObject(pusher_data, "text", transcript);
    cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);
    if (language_code) {
        cJSON_AddStringToObject(pusher_data, "language_code", language_code);
    }
    cJSON_AddNumberToObject(pusher_data, "channel_tag", channel_tag);

    char* pusher_json = cJSON_PrintUnformatted(pusher_data);
    cJSON_Delete(pusher_data);
    cJSON_Delete(root);

    if (!pusher_json) return;

    // Escape JSON for Pusher data field
    size_t json_len = strlen(pusher_json);
    char* escaped = malloc(json_len * 2 + 1);
    if (!escaped) {
        free(pusher_json);
        return;
    }

    char* p = escaped;
    for (size_t i = 0; i < json_len; i++) {
        if (pusher_json[i] == '"') { *p++ = '\\'; *p++ = '"'; }
        else if (pusher_json[i] == '\\') { *p++ = '\\'; *p++ = '\\'; }
        else *p++ = pusher_json[i];
    }
    *p = '\0';
    free(pusher_json);

    // Build Pusher request body
    const char* event_name = is_final ? event_final : event_interim;
    char body[8192];
    snprintf(body, sizeof(body),
        "{\"name\":\"%s\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
        event_name, channel, escaped);
    free(escaped);

    // Calculate MD5 of body
    char body_md5[33];
    md5_hex(body, body_md5);

    // Build auth params
    char auth_timestamp[32];
    snprintf(auth_timestamp, sizeof(auth_timestamp), "%ld", time(NULL));

    char query[512];
    snprintf(query, sizeof(query),
        "auth_key=%s&auth_timestamp=%s&auth_version=1.0&body_md5=%s",
        app_key, auth_timestamp, body_md5);

    // String to sign
    char to_sign[1024];
    snprintf(to_sign, sizeof(to_sign), "POST\n/apps/%s/events\n%s", app_id, query);

    // Calculate signature
    char signature[65];
    hmac_sha256_hex(app_secret, to_sign, signature);

    // Build URL
    char url[1024];
    snprintf(url, sizeof(url),
        "https://api-%s.pusher.com/apps/%s/events?%s&auth_signature=%s",
        cluster, app_id, query, signature);

    // Send HTTP POST
    struct pusher_response response = {NULL, 0};
    CURL* curl = curl_easy_init();
    if (!curl) return;

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pusher_curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
            "Pusher API call failed: %s\n", curl_easy_strerror(res));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "Pusher returned HTTP %ld: %s\n", http_code,
                response.data ? response.data : "(no response)");
        }
    }

    if (response.data) free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}
```

### **5. send_session_start_to_pusher() Function** (~120 lines)

**Identical to Deepgram/AWS pattern - no changes needed:**

```c
static void send_session_start_to_pusher(switch_core_session_t* session, const char* callId) {
    if (!callId) return;

    // Get Pusher credentials
    const char* app_id = getenv("PUSHER_APP_ID");
    const char* app_key = getenv("PUSHER_KEY");
    const char* app_secret = getenv("PUSHER_SECRET");
    const char* cluster = getenv("PUSHER_CLUSTER");

    if (!app_id || !app_key || !app_secret) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
            "Pusher not configured - skipping session_start event\n");
        return;
    }
    if (!cluster) cluster = "ap2";

    const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
    const char* event_session_start = getenv("PUSHER_EVENT_SESSION_START");
    if (!channel_prefix) channel_prefix = "call-";
    if (!event_session_start) event_session_start = "session-start";

    char channel[256];
    snprintf(channel, sizeof(channel), "%s%s", channel_prefix, callId);

    // Get metadata
    switch_channel_t *chan = switch_core_session_get_channel(session);
    const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
    const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
    const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
    if (!callee_name) callee_name = switch_channel_get_variable(chan, "effective_callee_id_name");
    const char* callee_number = switch_channel_get_variable(chan, "destination_number");
    if (!callee_number) callee_number = switch_channel_get_variable(chan, "callee_id_number");

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

    char data[1024];
    snprintf(data, sizeof(data),
        "{\"type\":\"session_start\",\"caller_id\":\"%s\",\"callee_id\":\"%s\",\"timestamp\":\"%s\"}",
        caller_id, callee_id, timestamp);

    char body[2048];
    snprintf(body, sizeof(body),
        "{\"name\":\"%s\",\"channel\":\"%s\",\"data\":\"%s\"}",
        event_session_start, channel, data);

    // [Same MD5, HMAC, URL building, curl code as send_to_pusher above]
    // ... (copy from send_to_pusher)
}
```

### **6. Update responseHandler()** (~30 lines added)

Add at beginning of `responseHandler()`:

```c
static void responseHandler(switch_core_session_t* session, const char * json, const char* bugname) {
    switch_event_t *event;
    switch_channel_t *channel = switch_core_session_get_channel(session);

    // NEW: Send session start on first transcript event
    static int session_started = 0;
    if (!session_started) {
        const char* call_id = switch_channel_get_variable(channel, "sip_call_id");
        if (!call_id) call_id = switch_core_session_get_uuid(session);
        if (!call_id) call_id = switch_channel_get_variable(channel, "call_uuid");

        if (call_id) {
            send_session_start_to_pusher(session, call_id);
            session_started = 1;
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                "Cannot send session_start: no call ID found\n");
        }
    }

    // NEW: Send transcript to Pusher (if configured)
    if (json) {
        const char* call_id = switch_channel_get_variable(channel, "sip_call_id");
        if (!call_id) call_id = switch_core_session_get_uuid(session);
        if (!call_id) call_id = switch_channel_get_variable(channel, "call_uuid");

        if (call_id) {
            // Determine if final based on JSON
            switch_bool_t is_final = SWITCH_FALSE;
            cJSON* root = cJSON_Parse(json);
            if (root) {
                cJSON* is_final_field = cJSON_GetObjectItem(root, "is_final");
                if (is_final_field && cJSON_IsBool(is_final_field)) {
                    is_final = cJSON_IsTrue(is_final_field) ? SWITCH_TRUE : SWITCH_FALSE;
                }
                cJSON_Delete(root);
            }
            send_to_pusher(session, json, call_id, is_final);
        }
    }

    // EXISTING CODE continues below...
    if (0 == strcmp("vad_detected", json)) {
        // ...existing VAD handling
    }
    // ... rest of existing code
}
```

---

## 📦 Summary of Code to Add

| Component | Lines | Location |
|-----------|-------|----------|
| Headers | 5 | Top of file |
| pusher_response struct | 4 | Before functions |
| pusher_curl_write_cb | 20 | Helper functions |
| bin_to_hex | 10 | Helper functions |
| hmac_sha256_hex | 8 | Helper functions |
| md5_hex | 6 | Helper functions |
| send_to_pusher | 180 | Before responseHandler |
| send_session_start_to_pusher | 120 | Before responseHandler |
| responseHandler updates | 30 | In existing function |
| **TOTAL** | **~383 lines** | mod_google_transcribe.c |

---

## ✅ Testing Checklist

- [ ] Compile successfully with OpenSSL and libcurl
- [ ] Session start event sent with correct metadata
- [ ] Interim transcripts sent to Pusher
- [ ] Final transcripts sent to Pusher
- [ ] channel_tag correctly mapped to speaker_id
- [ ] Empty transcripts skipped
- [ ] Call ID fallback works
- [ ] HTTP 200 OK from Pusher
- [ ] HTTP errors logged (401, 403, 400)
- [ ] Works without Pusher credentials (silent skip)

---

**Ready for Implementation!** 🚀
