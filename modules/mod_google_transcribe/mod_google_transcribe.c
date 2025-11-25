/*
 *
 * mod_google_transcribe.c -- Freeswitch module for real-time transcription using Google Speech-to-Text V2 API
 *
 */
#include "mod_google_transcribe.h"
#include "google_glue.h"
#include <stdlib.h>
#include <switch.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <time.h>

// V2 API: Default sample rate 16kHz (optimal for speech recognition)
static const uint32_t DEFAULT_SAMPLE_RATE = 16000;
static const char* DEFAULT_LANGUAGE = "en-US";
static const uint32_t DEFAULT_CHANNELS = 2; // Stereo by default

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_transcribe_shutdown);
SWITCH_MODULE_RUNTIME_FUNCTION(mod_transcribe_runtime);
SWITCH_MODULE_LOAD_FUNCTION(mod_transcribe_load);

SWITCH_MODULE_DEFINITION(mod_google_transcribe, mod_transcribe_load, mod_transcribe_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session, char* bugname);

/* Pusher response structure */
struct pusher_response {
    char* data;
    size_t size;
};

/* Pusher helper functions */
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

/* Send transcript to Pusher with Google V2 specific JSON parsing */
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

/* Send session start event to Pusher */
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
        "{\\\"type\\\":\\\"session_start\\\",\\\"caller_id\\\":\\\"%s\\\",\\\"callee_id\\\":\\\"%s\\\",\\\"timestamp\\\":\\\"%s\\\"}",
        caller_id, callee_id, timestamp);

    char body[2048];
    snprintf(body, sizeof(body),
        "{\"name\":\"%s\",\"channel\":\"%s\",\"data\":\"%s\"}",
        event_session_start, channel, data);

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
            "Pusher session_start failed: %s\n", curl_easy_strerror(res));
    } else {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "Pusher session_start HTTP %ld: %s\n", http_code,
                response.data ? response.data : "(no response)");
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
                "Pusher session_start sent for call %s\n", callId);
        }
    }

    if (response.data) free(response.data);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}


static void responseHandler(switch_core_session_t* session, const char * json, const char* bugname) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Pusher integration: Send session start on first transcript event
	static int session_started = 0;
	if (!session_started) {
		const char* call_id = switch_channel_get_variable(channel, "sip_call_id");
		if (!call_id) call_id = switch_core_session_get_uuid(session);
		if (!call_id) call_id = switch_channel_get_variable(channel, "call_uuid");

		if (call_id) {
			send_session_start_to_pusher(session, call_id);
			session_started = 1;
		}
	}

	// Pusher integration: Send transcript to Pusher for actual transcript results
	if (json && strcmp("vad_detected", json) && strcmp("end_of_utterance", json) &&
		strcmp("end_of_transcript", json) && strcmp("start_of_transcript", json) &&
		strcmp("max_duration_exceeded", json) && strcmp("no_audio", json) &&
		strcmp("play_interrupt", json)) {

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

	if (0 == strcmp("vad_detected", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_VAD_DETECTED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("end_of_utterance", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_END_OF_UTTERANCE);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("end_of_transcript", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_END_OF_TRANSCRIPT);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("start_of_transcript", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_START_OF_TRANSCRIPT);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("max_duration_exceeded", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("no_audio", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_NO_AUDIO_DETECTED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else if (0 == strcmp("play_interrupt", json)){
		switch_event_t *qevent;
		switch_status_t status;
		if (switch_event_create(&qevent, SWITCH_EVENT_DETECTED_SPEECH) == SWITCH_STATUS_SUCCESS) {
			if ((status = switch_core_session_queue_event(session, &qevent)) != SWITCH_STATUS_SUCCESS){
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "unable to queue play inturrupt event  %d \n", status);
			}
		}else{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "unable to create play inturrupt event \n");
		}
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_PLAY_INTERRUPT);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
	}
	else {
    int error = 0;
    cJSON* jMessage = cJSON_Parse(json);
    if (jMessage) {
      const char* type = cJSON_GetStringValue(cJSON_GetObjectItem(jMessage, "type"));
      if (type && 0 == strcmp(type, "error")) {
        error = 1;
    		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_ERROR);
      }
      cJSON_Delete(jMessage);
    }
    if (!error) {
    		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_RESULTS);
    }
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "%s json payload: %s.\n", bugname ? bugname : "google_transcribe", json);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
		switch_event_add_body(event, "%s", json);
	}
	if (bugname) switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
	switch_event_fire(&event);
}

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);
	struct cap_cb* cb = (struct cap_cb*) switch_core_media_bug_get_user_data(bug);

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_INIT.\n");
			responseHandler(session, "start_of_transcript", cb->bugname);
		break;

	case SWITCH_ABC_TYPE_CLOSE:
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_CLOSE, calling google_speech_session_cleanup.\n");
			responseHandler(session, "end_of_transcript", cb->bugname);
			google_speech_session_cleanup(session, 1, bug);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:

		return google_speech_frame(bug, user_data);
		break;

	case SWITCH_ABC_TYPE_WRITE:
	default:
		break;
	}

	return SWITCH_TRUE;
}

static switch_status_t transcribe_input_callback(switch_core_session_t *session, void *input, switch_input_type_t input_type, void *data, unsigned int len){
	if (input_type == SWITCH_INPUT_TYPE_EVENT) {
			switch_event_t *event;
			event = (switch_event_t *)input;
			if (event->event_id == SWITCH_EVENT_DETECTED_SPEECH) {
				return SWITCH_STATUS_BREAK;
			}
	}
	
	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t do_stop(switch_core_session_t *session, char *bugname)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, bugname);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Received user command command, calling google_speech_session_cleanup (possibly to stop prev transcribe)\n");
		status = google_speech_session_cleanup(session, 0, bug);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stopped transcription.\n");
	}

	return status;
}

static switch_status_t start_capture2(switch_core_session_t *session, switch_media_bug_flag_t flags, 
  uint32_t sample_rate, char* lang, int interim, int single_utterance, int separate_recognition, int max_alternatives,
  int profinity_filter, int word_time_offset, int punctuation, const char* model, int enhanced, const char* hints, char* play_file)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_codec_implementation_t read_impl = { 0 };
	void *pUserData;
	uint32_t samples_per_second;
	switch_input_args_t args = { 0 };

	if (switch_channel_get_private(channel, MY_BUG_NAME)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "removing bug from previous transcribe\n");
		do_stop(session, MY_BUG_NAME);
	}

	switch_core_session_get_read_impl(session, &read_impl);

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_FALSE;
	}

	samples_per_second = !strcasecmp(read_impl.iananame, "g722") ? read_impl.actual_samples_per_second : read_impl.samples_per_second;

	if (SWITCH_STATUS_FALSE == google_speech_session_init(session, responseHandler, sample_rate, samples_per_second, flags & SMBF_STEREO ? 2 : 1, lang, interim, MY_BUG_NAME, single_utterance,
	 separate_recognition, max_alternatives, profinity_filter, word_time_offset, punctuation, model, enhanced, hints, play_file, &pUserData)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing google speech session.\n");
		return SWITCH_STATUS_FALSE;
	}
	if ((status = switch_core_media_bug_add(session, "google_transcribe", NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}

	switch_channel_set_private(channel, MY_BUG_NAME, bug);	

	/* play the prompt, looking for detection result */
	if (play_file != NULL){
		args.input_callback = transcribe_input_callback;
		switch_ivr_play_file(session, NULL, play_file, &args);
	}

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, 
  char* lang, int interim, char* bugname)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_codec_implementation_t read_impl = { 0 };
	void *pUserData;
	uint32_t samples_per_second;
	int single_utterance = 0, separate_recognition = 0, max_alternatives = 0, profanity_filter = 0, word_time_offset = 0, punctuation = 0, enhanced = 0;
	const char* hints = NULL;
  const char* model = NULL;
	const char* var;

	if (switch_channel_get_private(channel, bugname)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "removing bug from previous transcribe\n");
		do_stop(session, bugname);
	}

	if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_SINGLE_UTTERANCE"))) {
      single_utterance = 1;
    }

	// transcribe each separately?
	if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL"))) {
      separate_recognition = 1;
    }

	// max alternatives
	if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_MAX_ALTERNATIVES"))) {
     	max_alternatives = atoi(var);
    }

	// profanity filter
	if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_PROFANITY_FILTER"))) {
      profanity_filter = 1;
    }

	// enable word offsets
	if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS"))) {
      word_time_offset = 1;
    }

	// enable automatic punctuation
	if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION"))) {
      punctuation = 1;
    }

    // speech model
	if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_MODEL"))) {	
		model = var;    
	}

	// use enhanced model
	if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_USE_ENHANCED"))) {
		enhanced = 1;
	}

	// hints
	if ((var = switch_channel_get_variable(channel, "GOOGLE_SPEECH_HINTS"))) {
	  hints = var;
	}

	switch_core_session_get_read_impl(session, &read_impl);

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_FALSE;
	}

	samples_per_second = !strcasecmp(read_impl.iananame, "g722") ? read_impl.actual_samples_per_second : read_impl.samples_per_second;

	if (SWITCH_STATUS_FALSE == google_speech_session_init(session, responseHandler, DEFAULT_SAMPLE_RATE, samples_per_second, flags & SMBF_STEREO ? 2 : 1, lang, interim, bugname, single_utterance,
	 separate_recognition, max_alternatives, profanity_filter, word_time_offset, punctuation, model, enhanced, hints, NULL, &pUserData)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing google speech session.\n");
		return SWITCH_STATUS_FALSE;
	}

	if ((status = switch_core_media_bug_add(session, bugname, NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}

	switch_channel_set_private(channel, bugname, bug);

	return SWITCH_STATUS_SUCCESS;
}

// #define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] [lang-code] [interim] [single-utterance](bool) [seperate-recognition](bool) [max-alternatives](int) [profinity-filter](bool) [word-time](bool) [punctuation](bool) [model](string) [enhanced](true) [hints](string without space) [play-file]"
#define TRANSCRIBE2_API_SYNTAX "<uuid> [start|stop] [lang-code] [interim]  [single-utterance] [seperate-recognition] [max-alternatives] [profinity-filter] [word-time] [punctuation] [sample-rate] [model] [enhanced] [hints] [play-file]"
SWITCH_STANDARD_API(transcribe2_function)
{
	char *mycmd = NULL, *argv[20] = { 0 };
	int argc = 0, enhanced = 0;
	uint32_t sample_rate = DEFAULT_SAMPLE_RATE;
	const char* hints = NULL;
	const char* model = NULL;
	char* play_file = NULL;

	switch_status_t status = SWITCH_STATUS_FALSE;
	// V2 API: Default to stereo (read + write streams)
	switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO;

	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || 
      (argc < 2) ||
      (!strcasecmp(argv[1], "start") && argc < 10) ||
      zstr(argv[0])) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s.\n", cmd, argv[0]);
		stream->write_function(stream, "-USAGE: %s\n", TRANSCRIBE2_API_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			if (!strcasecmp(argv[1], "stop")) {
    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "stop transcribing\n");
				status = do_stop(lsession, MY_BUG_NAME);
			} else if (!strcasecmp(argv[1], "start")) {
        char* lang = argv[2];
        int interim = argc > 3 && !strcmp(argv[3], "true");
				int single_utterance =  !strcmp(argv[4], "true"); // single-utterance
				int separate_recognition = !strcmp(argv[5], "true"); // sepreate-recognition
				int max_alternatives = atoi(argv[6]); // max-alternatives
				int profinity_filter = !strcmp(argv[7], "true"); // profinity-filter
				int word_time_offset = !strcmp(argv[8], "true"); // word-time
				int punctuation      = !strcmp(argv[9], "true");  //punctuation
				if (argc > 10) {
					sample_rate = atol(argv[10]);
				}
				if (argc > 12){
					model = argv[11]; // model 
					enhanced = !strcmp(argv[12], "true"); // enhanced
				}
				if (argc > 13){
					hints = argv[13]; // hints
				}
				if (argc > 14){
					play_file = argv[14];
				}
    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "start transcribing %s %s\n", lang, interim ? "interim": "complete");
				status = start_capture2(lsession, flags, sample_rate, lang, interim, single_utterance, separate_recognition,max_alternatives,
				profinity_filter, word_time_offset, punctuation, model, enhanced, hints, play_file);
			}
			switch_core_session_rwunlock(lsession);
		}
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "+OK Success\n");
	} else {
		stream->write_function(stream, "-ERR Operation Failed\n");
	}

  done:

	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;
}

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] [lang-code] [interim|full] [stereo|mono] [bug-name]"
SWITCH_STANDARD_API(transcribe_function)
{
	char *mycmd = NULL, *argv[6] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	// V2 API: Default to stereo (read + write streams)
	switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO;

	if (!zstr(cmd) && (mycmd = strdup(cmd))) {
		argc = switch_separate_string(mycmd, ' ', argv, (sizeof(argv) / sizeof(argv[0])));
	}

	if (zstr(cmd) || 
      (!strcasecmp(argv[1], "stop") && argc < 2) ||
      (!strcasecmp(argv[1], "start") && argc < 3) ||
      zstr(argv[0])) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Error with command %s %s %s.\n", cmd, argv[0], argv[1]);
		stream->write_function(stream, "-USAGE: %s\n", TRANSCRIBE_API_SYNTAX);
		goto done;
	} else {
		switch_core_session_t *lsession = NULL;

		if ((lsession = switch_core_session_locate(argv[0]))) {
			if (!strcasecmp(argv[1], "stop")) {
				char *bugname = argc > 2 ? argv[2] : MY_BUG_NAME;
    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "stop transcribing\n");
				status = do_stop(lsession, bugname);
			} else if (!strcasecmp(argv[1], "start")) {
        char* lang = argv[2];
        int interim = argc > 3 && !strcmp(argv[3], "interim");
				char *bugname = argc > 5 ? argv[5] : MY_BUG_NAME;
				if (argc > 4 && !strcmp(argv[4], "stereo")) {
          flags |= SMBF_WRITE_STREAM ;
          flags |= SMBF_STEREO;
				}
    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "%s start transcribing %s %s\n", bugname, lang, interim ? "interim": "complete");
				status = start_capture(lsession, flags, lang, interim, bugname);
			}
			switch_core_session_rwunlock(lsession);
		}
	}

	if (status == SWITCH_STATUS_SUCCESS) {
		stream->write_function(stream, "+OK Success\n");
	} else {
		stream->write_function(stream, "-ERR Operation Failed\n");
	}

  done:

	switch_safe_free(mycmd);
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_LOAD_FUNCTION(mod_transcribe_load)
{
	switch_api_interface_t *api_interface;

	/* create/register custom event message type */
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_RESULTS) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_RESULTS);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_END_OF_UTTERANCE);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_START_OF_TRANSCRIPT) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_START_OF_TRANSCRIPT);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_END_OF_TRANSCRIPT) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_END_OF_TRANSCRIPT);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_NO_AUDIO_DETECTED) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_NO_AUDIO_DETECTED);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED);
		return SWITCH_STATUS_TERM;
	}

	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_PLAY_INTERRUPT) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_PLAY_INTERRUPT);
		return SWITCH_STATUS_TERM;
	}

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech Transcription API loading..\n");

  if (SWITCH_STATUS_FALSE == google_speech_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing google speech interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech Transcription API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "uuid_google_transcribe", "Google Speech Transcription API", transcribe_function, TRANSCRIBE_API_SYNTAX);
	SWITCH_ADD_API(api_interface, "uuid_google_transcribe2", "Google Speech Transcription API", transcribe2_function, TRANSCRIBE2_API_SYNTAX);
	switch_console_set_complete("add uuid_google_transcribe start lang-code");
	switch_console_set_complete("add uuid_google_transcribe stop ");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_google_transcribe_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_transcribe_shutdown)
{
	google_speech_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	switch_event_free_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE);
	switch_event_free_subclass(TRANSCRIBE_EVENT_START_OF_TRANSCRIPT);
	switch_event_free_subclass(TRANSCRIBE_EVENT_END_OF_TRANSCRIPT);
	switch_event_free_subclass(TRANSCRIBE_EVENT_NO_AUDIO_DETECTED);
	switch_event_free_subclass(TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED);
	switch_event_free_subclass(TRANSCRIBE_EVENT_END_OF_UTTERANCE);
	switch_event_free_subclass(TRANSCRIBE_EVENT_PLAY_INTERRUPT);
	return SWITCH_STATUS_SUCCESS;
}

