/*
 *
 * mod_aws_transcribe.c -- Freeswitch module for using aws streaming transcribe api
 *
 */
#include "mod_aws_transcribe.h"
#include "aws_transcribe_glue.h"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <time.h>

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_aws_transcribe_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_aws_transcribe_load);

SWITCH_MODULE_DEFINITION(mod_aws_transcribe, mod_aws_transcribe_load, mod_aws_transcribe_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session, char* bugname);

/* ============================================================================
 * Pusher Direct Integration (no separate backend server)
 * Sends transcription directly to Pusher API with HMAC SHA256 signing
 * ============================================================================ */

// Curl write callback (discard response)
static size_t pusher_curl_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
	return size * nmemb;
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

static void send_to_pusher(switch_core_session_t* session, const char* json, const char* callId, switch_bool_t is_final) {
	if (!json || !callId) return;

	// Get Pusher credentials from environment
	const char* app_id = getenv("PUSHER_APP_ID");
	const char* app_key = getenv("PUSHER_KEY");
	const char* app_secret = getenv("PUSHER_SECRET");
	const char* cluster = getenv("PUSHER_CLUSTER");

	if (!app_id || !app_key || !app_secret) {
		return; // Pusher not configured, skip silently
	}
	if (!cluster) cluster = "ap2";

	const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
	const char* event_final = getenv("PUSHER_EVENT_FINAL");
	const char* event_interim = getenv("PUSHER_EVENT_INTERIM");
	if (!channel_prefix) channel_prefix = "call-";
	if (!event_final) event_final = "transcription-final";
	if (!event_interim) event_interim = "transcription-interim";

	// Build channel name: "call-<callId>"
	char channel[256];
	snprintf(channel, sizeof(channel), "%s%s", channel_prefix, callId);

	// Get caller/callee metadata from channel variables for speaker mapping
	switch_channel_t *chan = switch_core_session_get_channel(session);
	const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
	const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
	const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
	if (!callee_name) callee_name = switch_channel_get_variable(chan, "effective_callee_id_name");
	const char* callee_number = switch_channel_get_variable(chan, "destination_number");
	if (!callee_number) callee_number = switch_channel_get_variable(chan, "callee_id_number");

	// Parse transcription JSON to extract text and speaker/channel
	cJSON* root = cJSON_Parse(json);
	if (!root) return;

	const char* transcript = NULL;
	int speaker_channel = -1;

	// AWS sends an array of results: [{"is_final": true, "channel_id": "ch_0", "alternatives": [...]}]
	if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
		cJSON* first_result = cJSON_GetArrayItem(root, 0);

		// Get transcript text from alternatives[0].transcript
		cJSON* alternatives = cJSON_GetObjectItem(first_result, "alternatives");
		if (alternatives && cJSON_IsArray(alternatives) && cJSON_GetArraySize(alternatives) > 0) {
			cJSON* first_alt = cJSON_GetArrayItem(alternatives, 0);
			cJSON* transcript_field = cJSON_GetObjectItem(first_alt, "transcript");
			if (transcript_field && cJSON_IsString(transcript_field)) {
				transcript = cJSON_GetStringValue(transcript_field);
			}
		}

		// Get channel from channel_id (e.g., "ch_0", "ch_1")
		cJSON* channel_id = cJSON_GetObjectItem(first_result, "channel_id");
		if (channel_id && cJSON_IsString(channel_id)) {
			const char* channel_str = cJSON_GetStringValue(channel_id);
			if (channel_str && strstr(channel_str, "ch_")) {
				speaker_channel = atoi(channel_str + 3); // Skip "ch_" prefix
			}
		}
	}

	// Default to channel 0 if not found
	if (speaker_channel == -1) speaker_channel = 0;

	// Don't send to Pusher if transcript is empty
	if (!transcript || strlen(transcript) == 0) {
		cJSON_Delete(root);
		return;
	}

	// Map channel to speaker_id: channel 0 = caller, channel 1 = callee
	char speaker_id[256];
	if (speaker_channel == 0) {
		// Caller (Channel 0)
		snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
			caller_name ? caller_name : "Unknown",
			caller_number ? caller_number : "Unknown");
	} else {
		// Callee (Channel 1)
		snprintf(speaker_id, sizeof(speaker_id), "%s(%s)",
			callee_name ? callee_name : "Unknown",
			callee_number ? callee_number : "Unknown");
	}

	// Get current timestamp in ISO 8601 format
	time_t now = time(NULL);
	struct tm tm_info;
	gmtime_r(&now, &tm_info);
	char timestamp[32];
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

	// Build transformed JSON in required format
	cJSON* pusher_data = cJSON_CreateObject();
	cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
	cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id);
	cJSON_AddStringToObject(pusher_data, "text", transcript ? transcript : "");
	cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);

	char* pusher_json = cJSON_PrintUnformatted(pusher_data);
	cJSON_Delete(pusher_data);
	cJSON_Delete(root);

	if (!pusher_json) return;

	// Escape JSON for embedding in outer JSON string
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

	// Build request body
	const char* event_name = is_final ? event_final : event_interim;
	char body[8192];
	snprintf(body, sizeof(body),
		"{\"name\":\"%s\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
		event_name, channel, escaped);
	free(escaped);

	// Calculate body MD5
	char body_md5[33];
	md5_hex(body, body_md5);

	// Build query string
	char auth_timestamp[32];
	snprintf(auth_timestamp, sizeof(auth_timestamp), "%ld", time(NULL));

	char query[512];
	snprintf(query, sizeof(query),
		"auth_key=%s&auth_timestamp=%s&auth_version=1.0&body_md5=%s",
		app_key, auth_timestamp, body_md5);

	// Build string to sign
	char to_sign[1024];
	snprintf(to_sign, sizeof(to_sign),
		"POST\n/apps/%s/events\n%s",
		app_id, query);

	// Calculate signature
	char signature[65];
	hmac_sha256_hex(app_secret, to_sign, signature);

	// Build final URL
	char url[1024];
	snprintf(url, sizeof(url),
		"https://api-%s.pusher.com/apps/%s/events?%s&auth_signature=%s",
		cluster, app_id, query, signature);

	// Send HTTP POST
	CURL* curl = curl_easy_init();
	if (!curl) return;

	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pusher_curl_write_cb);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
			"Pusher API call failed: %s\n", curl_easy_strerror(res));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

void send_session_start_to_pusher(switch_core_session_t* session, const char* callId) {
	if (!callId) return;

	// Get Pusher credentials from environment
	const char* app_id = getenv("PUSHER_APP_ID");
	const char* app_key = getenv("PUSHER_KEY");
	const char* app_secret = getenv("PUSHER_SECRET");
	const char* cluster = getenv("PUSHER_CLUSTER");

	if (!app_id || !app_key || !app_secret) {
		return; // Pusher not configured, skip silently
	}
	if (!cluster) cluster = "ap2";

	const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
	const char* event_session_start = getenv("PUSHER_EVENT_SESSION_START");
	if (!channel_prefix) channel_prefix = "call-";
	if (!event_session_start) event_session_start = "session-start";

	// Build channel name: "call-<callId>"
	char channel[256];
	snprintf(channel, sizeof(channel), "%s%s", channel_prefix, callId);

	// Get caller/callee metadata from channel variables
	switch_channel_t *chan = switch_core_session_get_channel(session);
	const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
	const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
	const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
	if (!callee_name) callee_name = switch_channel_get_variable(chan, "effective_callee_id_name");
	const char* callee_number = switch_channel_get_variable(chan, "destination_number");
	if (!callee_number) callee_number = switch_channel_get_variable(chan, "callee_id_number");

	// Build caller_id and callee_id strings
	char caller_id[256];
	char callee_id[256];
	snprintf(caller_id, sizeof(caller_id), "%s(%s)",
		caller_name ? caller_name : "Unknown",
		caller_number ? caller_number : "Unknown");
	snprintf(callee_id, sizeof(callee_id), "%s(%s)",
		callee_name ? callee_name : "Unknown",
		callee_number ? callee_number : "Unknown");

	// Get current timestamp in ISO 8601 format
	time_t now = time(NULL);
	struct tm tm_info;
	gmtime_r(&now, &tm_info);
	char timestamp[32];
	strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &tm_info);

	// Build session start JSON payload
	char data[1024];
	snprintf(data, sizeof(data),
		"{\"type\":\"session_start\",\"caller_id\":\"%s\",\"callee_id\":\"%s\",\"timestamp\":\"%s\"}",
		caller_id, callee_id, timestamp);

	// Build Pusher request body
	char body[2048];
	snprintf(body, sizeof(body),
		"{\"name\":\"%s\",\"channel\":\"%s\",\"data\":\"%s\"}",
		event_session_start, channel, data);

	// Calculate MD5 of body
	unsigned char md5_digest[16];
	MD5((unsigned char*)body, strlen(body), md5_digest);
	char body_md5[33];
	for (int i = 0; i < 16; i++) {
		sprintf(body_md5 + (i * 2), "%02x", md5_digest[i]);
	}

	// Build auth parameters
	char session_auth_timestamp[32];
	snprintf(session_auth_timestamp, sizeof(session_auth_timestamp), "%ld", time(NULL));

	char query[512];
	snprintf(query, sizeof(query),
		"auth_key=%s&auth_timestamp=%s&auth_version=1.0&body_md5=%s",
		app_key, session_auth_timestamp, body_md5);

	// Build string to sign
	char to_sign[1024];
	snprintf(to_sign, sizeof(to_sign),
		"POST\n/apps/%s/events\n%s",
		app_id, query);

	// Calculate signature
	char signature[65];
	hmac_sha256_hex(app_secret, to_sign, signature);

	// Build final URL
	char url[1024];
	snprintf(url, sizeof(url),
		"https://api-%s.pusher.com/apps/%s/events?%s&auth_signature=%s",
		cluster, app_id, query, signature);

	// Send HTTP POST
	CURL* curl = curl_easy_init();
	if (!curl) return;

	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, pusher_curl_write_cb);

	CURLcode res = curl_easy_perform(curl);

	if (res != CURLE_OK) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
			"Pusher session_start call failed: %s\n", curl_easy_strerror(res));
	}

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

static void responseHandler(switch_core_session_t* session, const char * json, const char* bugname) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	if (0 == strcmp("vad_detected", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_VAD_DETECTED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
	}
	else if (0 == strcmp("end_of_transcript", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_END_OF_TRANSCRIPT);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
	}
	else if (0 == strcmp("max_duration_exceeded", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_MAX_DURATION_EXCEEDED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
	}
	else if (0 == strcmp("no_audio", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_NO_AUDIO_DETECTED);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
	}
	else {
    int error = 0;
    switch_bool_t is_final = SWITCH_FALSE;
    cJSON* jMessage = cJSON_Parse(json);
    if (jMessage) {
      const char* type = cJSON_GetStringValue(cJSON_GetObjectItem(jMessage, "type"));
      if (type && 0 == strcmp(type, "error")) {
        error = 1;
    		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_ERROR);
      }

      // Check if this is a final transcription (AWS sends array with is_final field)
      if (cJSON_IsArray(jMessage) && cJSON_GetArraySize(jMessage) > 0) {
        cJSON* first_result = cJSON_GetArrayItem(jMessage, 0);
        cJSON* is_final_field = cJSON_GetObjectItem(first_result, "is_final");
        if (is_final_field && cJSON_IsBool(is_final_field)) {
          is_final = cJSON_IsTrue(is_final_field) ? SWITCH_TRUE : SWITCH_FALSE;
        }
      }

      cJSON_Delete(jMessage);
    }
    if (!error) {
    		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_RESULTS);

    		// Send to Pusher (if configured) - only for actual transcription results
    		const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
    		if (sip_call_id) {
    			send_to_pusher(session, json, sip_call_id, is_final);
    		}
    }
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "aws_transcribe message: %s\n", json);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
		switch_event_add_body(event, "%s", json);
	}
	if (bugname) switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
	switch_event_fire(&event);
}


static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_INIT.\n");
		break;

	case SWITCH_ABC_TYPE_CLOSE:
		{
			struct cap_cb* cb = (struct cap_cb*) switch_core_media_bug_get_user_data(bug);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_CLOSE.\n");
			aws_transcribe_session_stop(session, 1, cb->bugname);
			//switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:

		return aws_transcribe_frame(bug, user_data);
		break;

	case SWITCH_ABC_TYPE_WRITE:
	default:
		break;
	}

	return SWITCH_TRUE;
}

/* ============================================================================
 * Helper function to build metadata JSON with caller/callee information
 *
 * IMPORTANT - Metadata Usage:
 * ---------------------------
 * This metadata is used for LOCAL TRACKING ONLY and is NOT sent to AWS Transcribe.
 * It is included in FreeSWITCH events (session_start, session_stop) for:
 *   - Call tracking and logging
 *   - Channel mapping for speaker diarization
 *   - Integration with external systems
 *
 * Channel Mapping for Stereo Transcription:
 * -----------------------------------------
 * When using stereo mode (api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo):
 *   - Channel 0 (left audio)  = Caller  (A-leg) - caller_number, caller_name
 *   - Channel 1 (right audio) = Callee  (B-leg) - callee_number, callee_name
 *
 * This channel mapping enables accurate speaker diarization by separating
 * the audio streams and associating each channel with the correct speaker identity.
 *
 * The metadata structure includes:
 *   {
 *     "callerName": "Extension 1000",
 *     "callerNumber": "1000",
 *     "calleeName": "Extension 1001",
 *     "calleeNumber": "1001",
 *     "call-Id": "3848276298220188511@atlanta.example.com",
 *     ...user_metadata
 *   }
 * ============================================================================ */
static char* build_session_metadata(switch_core_session_t *session, switch_memory_pool_t *pool, char *user_metadata) {
	switch_channel_t *channel = switch_core_session_get_channel(session);
	cJSON *jMetadata = NULL;
	char *metadata_str = NULL;

	// If user provided metadata, parse it; otherwise create new object
	if (user_metadata && (user_metadata[0] == '{' || user_metadata[0] == '[')) {
		jMetadata = cJSON_Parse(user_metadata);
		if (!jMetadata) {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
				"Failed to parse user metadata, creating new metadata object\n");
			jMetadata = cJSON_CreateObject();
		}
	} else {
		jMetadata = cJSON_CreateObject();
	}

	// Add caller/callee information directly to metadata (Channel 0 = Caller, Channel 1 = Callee)

	// Caller information (Channel 0 / A-leg)
	const char *caller_number = switch_channel_get_variable(channel, "caller_id_number");
	const char *caller_name = switch_channel_get_variable(channel, "caller_id_name");
	if (caller_name) {
		cJSON_AddStringToObject(jMetadata, "callerName", caller_name);
	}
	if (caller_number) {
		cJSON_AddStringToObject(jMetadata, "callerNumber", caller_number);
	}

	// Callee information (Channel 1 / B-leg)
	const char *callee_number = switch_channel_get_variable(channel, "destination_number");
	if (!callee_number) {
		callee_number = switch_channel_get_variable(channel, "callee_id_number");
	}
	const char *callee_name = switch_channel_get_variable(channel, "callee_id_name");
	if (!callee_name) {
		callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
	}

	if (callee_name) {
		cJSON_AddStringToObject(jMetadata, "calleeName", callee_name);
	}
	if (callee_number) {
		cJSON_AddStringToObject(jMetadata, "calleeNumber", callee_number);
	}

	// Add SIP Call-ID (unique identifier for each call)
	const char *sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
	if (sip_call_id) {
		cJSON_AddStringToObject(jMetadata, "call-Id", sip_call_id);
	}

	// Convert to string
	metadata_str = cJSON_PrintUnformatted(jMetadata);

	// Copy to pool memory so it persists
	char *result = NULL;
	if (metadata_str) {
		result = switch_core_strdup(pool, metadata_str);
		free(metadata_str);
	}

	cJSON_Delete(jMetadata);
	return result;
}

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags,
  char* lang, int interim, char* bugname, int sampling, char* metadata)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_codec_implementation_t read_impl = { 0 };
	void *pUserData;
	uint32_t samples_per_second;
	int channels;

	if (switch_channel_get_private(channel, bugname)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "removing bug from previous transcribe\n");
		do_stop(session, bugname);
	}

	switch_core_session_get_read_impl(session, &read_impl);

	if (switch_channel_pre_answer(channel) != SWITCH_STATUS_SUCCESS) {
		return SWITCH_STATUS_FALSE;
	}

	// Determine actual samples per second from codec
	samples_per_second = !strcasecmp(read_impl.iananame, "g722") ? read_impl.actual_samples_per_second : read_impl.samples_per_second;

	// Override with requested sampling rate if different
	if (sampling != samples_per_second) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Resampling from %d to %d\n", samples_per_second, sampling);
		samples_per_second = sampling;
	}

	// Determine channel count based on flags
	channels = flags & SMBF_STEREO ? 2 : 1;

	// Build enriched metadata with caller/callee information
	char *enriched_metadata = build_session_metadata(session, switch_core_session_get_pool(session), metadata);

	// Log metadata before starting transcription
	if (enriched_metadata && strlen(enriched_metadata) > 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "AWS transcription metadata: %s\n", enriched_metadata);
	}

	if (SWITCH_STATUS_FALSE == aws_transcribe_session_init(session, responseHandler, samples_per_second, channels, lang, interim, bugname, enriched_metadata, &pUserData)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing aws speech session.\n");
		return SWITCH_STATUS_FALSE;
	}
	if ((status = switch_core_media_bug_add(session, bugname, NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}
  switch_channel_set_private(channel, bugname, bug);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "added media bug for aws transcribe\n");

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t do_stop(switch_core_session_t *session, char* bugname)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, bugname);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Received user command command to stop transcribe on %s.\n", bugname);
		status = aws_transcribe_session_stop(session, 0, bugname);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stopped transcribe.\n");
	}

	return status;
}

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]"
SWITCH_STANDARD_API(aws_transcribe_function)
{
	char *mycmd = NULL, *argv[8] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	switch_media_bug_flag_t flags = SMBF_READ_STREAM;

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
				char *bugname = MY_BUG_NAME;
				int sampling = 16000;  // Default to 16kHz
				char *metadata = NULL;

				// Parse mix-type (argv[4]): mono (default), mixed, stereo
				if (argc > 4) {
					if (!strcmp(argv[4], "mixed")) {
						flags |= SMBF_WRITE_STREAM;  // Mixed: READ + WRITE (single channel)
					} else if (!strcmp(argv[4], "stereo")) {
						flags |= SMBF_WRITE_STREAM;  // Stereo: READ + WRITE + STEREO
						flags |= SMBF_STEREO;
					}
					// else: mono is default (SMBF_READ_STREAM only)
				}

				// Parse sampling rate (argv[5]): 8k, 16k, or numeric
				if (argc > 5) {
					if (!strcmp(argv[5], "8k")) {
						sampling = 8000;
					} else if (!strcmp(argv[5], "16k")) {
						sampling = 16000;
					} else {
						int rate = atoi(argv[5]);
						if (rate > 0 && rate % 8000 == 0) {
							sampling = rate;
						}
					}
				}

				// Parse metadata (argv[6] or argv[7])
				if (argc > 6) {
					// Check if argv[6] is metadata (starts with { or [) or bugname
					if (argv[6][0] == '{' || argv[6][0] == '[') {
						metadata = argv[6];
					} else {
						bugname = argv[6];
					}
				}
				if (argc > 7) {
					// If we have 7 args, argv[7] might be metadata
					if (argv[7][0] == '{' || argv[7][0] == '[') {
						metadata = argv[7];
					}
				}

    		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
					"start transcribing lang=%s interim=%s mix=%s rate=%d bugname=%s metadata=%s\n",
					lang,
					interim ? "yes" : "no",
					(flags & SMBF_STEREO) ? "stereo" : (flags & SMBF_WRITE_STREAM) ? "mixed" : "mono",
					sampling,
					bugname,
					metadata ? metadata : "none");

				status = start_capture(lsession, flags, lang, interim, bugname, sampling, metadata);
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


SWITCH_MODULE_LOAD_FUNCTION(mod_aws_transcribe_load)
{
	switch_api_interface_t *api_interface;

	/* create/register custom event message types */
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_RESULTS) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_RESULTS);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_SESSION_START) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_SESSION_START);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_SESSION_STOP) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_SESSION_STOP);
		return SWITCH_STATUS_TERM;
	}

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AWS Speech Transcription API loading..\n");

  if (SWITCH_STATUS_FALSE == aws_transcribe_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing aws speech interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AWS Speech Transcription API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "uuid_aws_transcribe", "AWS Speech Transcription API", aws_transcribe_function, TRANSCRIBE_API_SYNTAX);
	switch_console_set_complete("add uuid_aws_transcribe start lang-code [interim|final] [mono|mixed|stereo] [8k|16k]");
	switch_console_set_complete("add uuid_aws_transcribe stop ");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_aws_transcribe_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_aws_transcribe_shutdown)
{
	aws_transcribe_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_START);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_STOP);
	return SWITCH_STATUS_SUCCESS;
}
