/*
 *
 * mod_google_transcribev2.c -- FreeSWITCH module for Google Speech-to-Text v2 streaming API
 *
 */
#include "mod_google_transcribev2.h"
#include "google_transcribe_glue.h"
#include <curl/curl.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <time.h>

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_google_transcribev2_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_google_transcribev2_load);

SWITCH_MODULE_DEFINITION(mod_google_transcribev2, mod_google_transcribev2_load, mod_google_transcribev2_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session, char* bugname);

/* ============================================================================
 * Pusher Direct Integration (no separate backend server)
 * Sends transcription directly to Pusher API with HMAC SHA256 signing
 * ============================================================================ */

// Structure to capture Pusher response
struct pusher_response {
	char* data;
	size_t size;
};

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

static void send_to_pusher(switch_core_session_t* session, const char* json, const char* callId, switch_bool_t is_final) {
	if (!json || !callId) return;

	// Get Pusher credentials from channel variables first, then environment
	switch_channel_t *channel = switch_core_session_get_channel(session);
	const char* app_id = switch_channel_get_variable(channel, "PUSHER_APP_ID");
	const char* app_key = switch_channel_get_variable(channel, "PUSHER_KEY");
	const char* app_secret = switch_channel_get_variable(channel, "PUSHER_SECRET");
	const char* cluster = switch_channel_get_variable(channel, "PUSHER_CLUSTER");

	// Fallback to environment variables if not set in channel
	if (!app_id) app_id = getenv("PUSHER_APP_ID");
	if (!app_key) app_key = getenv("PUSHER_KEY");
	if (!app_secret) app_secret = getenv("PUSHER_SECRET");
	if (!cluster) cluster = getenv("PUSHER_CLUSTER");

	// Debug credential sources
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
		"Pusher credentials - APP_ID: %s, KEY: %s, SECRET: %s, CLUSTER: %s\n",
		app_id ? app_id : "(null)",
		app_key ? app_key : "(null)",
		app_secret ? app_secret : "(null)",
		cluster ? cluster : "(null)");

	if (!app_id || !app_key || !app_secret) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
			"Pusher not configured (missing PUSHER_APP_ID, PUSHER_KEY, or PUSHER_SECRET) - skipping transcription event\n");
		return;
	}

	if (!cluster) cluster = "us2";  // Default cluster

	// Build Pusher channel name (transcription-<callId>)
	char pusher_channel[128];
	snprintf(pusher_channel, sizeof(pusher_channel), "transcription-%s", callId);

	// Event names (aligned with AWS/Deepgram)
	const char* event_interim = "transcription-partial";
	const char* event_final = "transcription-final";

	// Escape JSON data for Pusher body (must escape quotes and backslashes)
	size_t json_len = strlen(json);
	char* pusher_json = strdup(json);
	if (!pusher_json) return;

	// Calculate required size for escaped string
	size_t escaped_size = 0;
	for (size_t i = 0; i < json_len; i++) {
		if (pusher_json[i] == '"' || pusher_json[i] == '\\') {
			escaped_size += 2;
		} else {
			escaped_size += 1;
		}
	}

	char* escaped = malloc(escaped_size + 1);
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
		event_name, pusher_channel, escaped);
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

	// Log the URL for debugging (without signature for security)
	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
		"Pusher transcription URL: https://api-%s.pusher.com/apps/%s/events\n",
		cluster, app_id);

	// Send HTTP POST
	CURL* curl = curl_easy_init();
	if (!curl) return;

	// Capture response
	struct pusher_response response = {0};

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
		// Check HTTP status code
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

static void send_session_start_to_pusher(switch_core_session_t* session, const char* callId) {
	if (!callId) return;

	// Get Pusher credentials from environment
	const char* app_id = getenv("PUSHER_APP_ID");
	const char* app_key = getenv("PUSHER_KEY");
	const char* app_secret = getenv("PUSHER_SECRET");
	const char* cluster = getenv("PUSHER_CLUSTER");

	if (!app_id || !app_key || !app_secret) return;
	if (!cluster) cluster = "us2";

	// Build simple session_start event
	char json[256];
	snprintf(json, sizeof(json), "{\"event\":\"session_start\",\"call_id\":\"%s\"}", callId);

	// Build Pusher channel name
	char pusher_channel[128];
	snprintf(pusher_channel, sizeof(pusher_channel), "transcription-%s", callId);

	// Build request body
	char body[512];
	snprintf(body, sizeof(body),
		"{\"name\":\"session-start\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
		pusher_channel, json);

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

	curl_easy_perform(curl);

	curl_slist_free_all(headers);
	curl_easy_cleanup(curl);
}

/* ============================================================================
 * Response Handler - Called by C++ glue layer when transcription arrives
 * ============================================================================ */

static void responseHandler(switch_core_session_t* session, const char* eventName, const char* json, const char* bugname, int finished) {
	switch_channel_t *channel = switch_core_session_get_channel(session);
	const char* callId = switch_channel_get_variable(channel, "sip_call_id");

	if (!callId) callId = switch_core_session_get_uuid(session);

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
		"Google transcribe event: %s, bugname: %s, finished: %d\n", eventName, bugname, finished);

	// Send to Pusher if configured
	if (json && strcmp(eventName, TRANSCRIBE_EVENT_RESULTS) == 0) {
		send_to_pusher(session, json, callId, finished);
	}

	// Fire FreeSWITCH event
	if (json && strlen(json) > 0) {
		switch_event_t *event;
		if (switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName) == SWITCH_STATUS_SUCCESS) {
			switch_channel_event_set_data(channel, event);
			switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
			switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-session-finished", finished ? "true" : "false");
			switch_event_add_body(event, "%s", json);
			switch_event_fire(&event);
		}
	}
}

/* ============================================================================
 * Media Bug Callback - FreeSWITCH calls this for audio frames
 * ============================================================================ */

static switch_bool_t capture_callback(switch_media_bug_t *bug, void *user_data, switch_abc_type_t type)
{
	switch_core_session_t *session = switch_core_media_bug_get_session(bug);

	switch (type) {
	case SWITCH_ABC_TYPE_INIT:
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_INIT.\n");
		break;

	case SWITCH_ABC_TYPE_CLOSE:
		{
			private_t *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_CLOSE.\n");

			google_transcribe_session_stop(session, 1,  tech_pvt->bugname);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;

	case SWITCH_ABC_TYPE_READ:

		return google_transcribe_frame(session, bug);
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
 * This metadata is used for LOCAL TRACKING ONLY and is NOT sent to Google.
 * It is included in FreeSWITCH events (session_start, session_stop) for:
 *   - Call tracking and logging
 *   - Channel mapping for speaker diarization
 *   - Integration with external systems
 *
 * Channel Mapping for Stereo Transcription:
 * -----------------------------------------
 * When using stereo mode (api_on_answer=uuid_google_transcribev2 ${uuid} start en-US interim stereo):
 *   - Channel 0 (left audio)  = Caller  (A-leg) - caller_number, caller_name
 *   - Channel 1 (right audio) = Callee  (B-leg) - callee_number, callee_name
 *
 * This channel mapping enables accurate speaker diarization by separating
 * the audio streams and associating each channel with the correct speaker identity.
 * ============================================================================ */

static char* build_metadata_json(switch_core_session_t *session) {
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Get caller information (A-leg)
	const char* caller_number = switch_channel_get_variable(channel, "caller_id_number");
	const char* caller_name = switch_channel_get_variable(channel, "caller_id_name");
	const char* callee_number = switch_channel_get_variable(channel, "callee_id_number");
	const char* callee_name = switch_channel_get_variable(channel, "callee_id_name");
	const char* call_id = switch_channel_get_variable(channel, "sip_call_id");

	if (!caller_number) caller_number = "Unknown";
	if (!caller_name) caller_name = "Unknown";
	if (!callee_number) callee_number = "Unknown";
	if (!callee_name) callee_name = "Unknown";
	if (!call_id) call_id = switch_core_session_get_uuid(session);

	// Build JSON metadata (simple format)
	char metadata[MAX_METADATA_LEN];
	snprintf(metadata, sizeof(metadata),
		"{\"caller_number\":\"%s\",\"caller_name\":\"%s\",\"callee_number\":\"%s\",\"callee_name\":\"%s\",\"call_id\":\"%s\"}",
		caller_number, caller_name, callee_number, callee_name, call_id);

	return strdup(metadata);
}

/* ============================================================================
 * Start Capture - Initialize transcription session
 * ============================================================================ */

static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags, char* lang, int interim, char* bugname, int sampling, char* metadata)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug;
	switch_status_t status;
	switch_codec_implementation_t read_impl = { 0 };
	void *pUserData = NULL;
	private_t *tech_pvt = NULL;
	const char* callId;
	uint32_t channels = flags & SMBF_STEREO ? 2 : 1;

	switch_core_session_get_read_impl(session, &read_impl);

	if (switch_channel_get_private(channel, bugname)) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG, "removing bug from previous transcribe\n");
		do_stop(session, bugname);
	}

	// Auto-generate metadata if not provided
	if (!metadata || strlen(metadata) == 0) {
		metadata = build_metadata_json(session);
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
		"Starting Google transcription: lang=%s interim=%d channels=%d sampling=%d metadata=%s\n",
		lang, interim, channels, sampling, metadata);

	// Initialize Google transcription session
	status = google_transcribe_session_init(session, responseHandler, sampling, channels, lang, interim, bugname, metadata, &pUserData);
	if (status != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Failed to initialize Google transcription session\n");
		return status;
	}

	tech_pvt = (private_t*)pUserData;

	// Add media bug for audio capture
	status = switch_core_media_bug_add(session, bugname, NULL, capture_callback, tech_pvt, 0, flags, &bug);

	if (status != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR, "Failed to add media bug\n");
		return status;
	}

	switch_channel_set_private(channel, bugname, bug);

	// Send session_start event to Pusher
	callId = switch_channel_get_variable(channel, "sip_call_id");
	if (!callId) callId = switch_core_session_get_uuid(session);
	send_session_start_to_pusher(session, callId);

	return SWITCH_STATUS_SUCCESS;
}

/* ============================================================================
 * Stop Capture - End transcription session
 * ============================================================================ */

static switch_status_t do_stop(switch_core_session_t *session, char* bugname)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, bugname);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "Stopping Google transcription\n");
		switch_channel_set_private(channel, bugname, NULL);
		switch_core_media_bug_remove(session, &bug);
		return SWITCH_STATUS_SUCCESS;
	}

	switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING, "No Google transcription session found for bugname: %s\n", bugname);
	return SWITCH_STATUS_FALSE;
}

/* ============================================================================
 * API Function - uuid_google_transcribev2 <uuid> start|stop ...
 * ============================================================================ */

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]"

SWITCH_STANDARD_API(google_transcribev2_function)
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

/* ============================================================================
 * Module Load/Unload
 * ============================================================================ */

SWITCH_MODULE_LOAD_FUNCTION(mod_google_transcribev2_load)
{
	switch_api_interface_t *api_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech-to-Text v2 module loading...\n");

	if (SWITCH_STATUS_SUCCESS != google_transcribe_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed to initialize Google transcription\n");
		return SWITCH_STATUS_TERM;
	}

	SWITCH_ADD_API(api_interface, "uuid_google_transcribev2", "Google Speech Transcription API", google_transcribev2_function, TRANSCRIBE_API_SYNTAX);
	switch_console_set_complete("add uuid_google_transcribev2 start lang-code [interim] [mono|mixed|stereo] [8k|16k]");
	switch_console_set_complete("add uuid_google_transcribev2 stop");

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech-to-Text v2 module loaded successfully\n");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_google_transcribev2_shutdown)
{
	google_transcribe_cleanup();
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Google Speech-to-Text v2 module unloaded\n");
	return SWITCH_STATUS_SUCCESS;
}
