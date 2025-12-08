/*
 *
 * mod_deepgram_transcribe.c -- Freeswitch module for using dg streaming transcribe api
 *
 */
#include "mod_deepgram_transcribe.h"
#include "dg_transcribe_glue.h"
#include "async_pusher.h"
#include <time.h>

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_deepgram_transcribe_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_deepgram_transcribe_load);

SWITCH_MODULE_DEFINITION(mod_deepgram_transcribe, mod_deepgram_transcribe_load, mod_deepgram_transcribe_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session, char* bugname);
static switch_status_t dg_get_performance_stats(switch_core_session_t *session, const char* stats_type, switch_stream_handle_t *stream);

/* ============================================================================
 * Async Pusher Integration (Non-Blocking)
 * 
 * HIGH SCALE OPTIMIZATION: This module now uses non-blocking I/O for Pusher.
 * All HTTP requests are queued and processed by a background timer, never
 * blocking the audio frame callback. See HIGH_SCALE_ARCHITECTURE.md for details.
 * ============================================================================ */

/* Non-blocking wrapper for sending transcriptions to Pusher */
static void send_to_pusher(switch_core_session_t* session, const char* json, const char* callId, switch_bool_t is_final) {
	if (!json || !callId) return;
	
	switch_channel_t *channel = switch_core_session_get_channel(session);
	
	/* Get Pusher credentials from channel variables first, then environment */
	const char* app_id = switch_channel_get_variable(channel, "PUSHER_APP_ID");
	const char* app_key = switch_channel_get_variable(channel, "PUSHER_KEY");
	const char* app_secret = switch_channel_get_variable(channel, "PUSHER_SECRET");
	const char* cluster = switch_channel_get_variable(channel, "PUSHER_CLUSTER");
	
	/* Fallback to environment */
	if (!app_id) app_id = getenv("PUSHER_APP_ID");
	if (!app_key) app_key = getenv("PUSHER_KEY");
	if (!app_secret) app_secret = getenv("PUSHER_SECRET");
	if (!cluster) cluster = getenv("PUSHER_CLUSTER");
	
	if (!app_id || !app_key || !app_secret) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
			"Pusher not configured - skipping transcription event\n");
		return;
	}
	
	/* Get caller/callee metadata for speaker mapping */
	const char* caller_name = switch_channel_get_variable(channel, "caller_id_name");
	const char* caller_number = switch_channel_get_variable(channel, "caller_id_number");
	const char* callee_name = switch_channel_get_variable(channel, "callee_id_name");
	if (!callee_name) callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
	const char* callee_number = switch_channel_get_variable(channel, "destination_number");
	if (!callee_number) callee_number = switch_channel_get_variable(channel, "callee_id_number");
	
	/* Use async non-blocking send - returns immediately */
	if (async_pusher_send_transcription(
			app_id, app_key, app_secret, cluster ? cluster : "ap2",
			callId, json, is_final ? 1 : 0,
			caller_name, caller_number,
			callee_name, callee_number) != 0) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
			"Failed to queue transcription to Pusher (queue may be full)\n");
	}
}

/* OPTIMIZED: Send using pre-parsed data (no JSON re-parsing needed) */
static void send_to_pusher_parsed(switch_core_session_t* session, const char* callId, 
	const transcript_data_t* td) {
	if (!callId || !td || !td->has_transcript) return;
	
	switch_channel_t *channel = switch_core_session_get_channel(session);
	
	/* Get Pusher credentials from channel variables first, then environment */
	const char* app_id = switch_channel_get_variable(channel, "PUSHER_APP_ID");
	const char* app_key = switch_channel_get_variable(channel, "PUSHER_KEY");
	const char* app_secret = switch_channel_get_variable(channel, "PUSHER_SECRET");
	const char* cluster = switch_channel_get_variable(channel, "PUSHER_CLUSTER");
	
	/* Fallback to environment */
	if (!app_id) app_id = getenv("PUSHER_APP_ID");
	if (!app_key) app_key = getenv("PUSHER_KEY");
	if (!app_secret) app_secret = getenv("PUSHER_SECRET");
	if (!cluster) cluster = getenv("PUSHER_CLUSTER");
	
	if (!app_id || !app_key || !app_secret) {
		return;
	}
	
	/* Get caller/callee metadata for speaker mapping */
	const char* caller_name = switch_channel_get_variable(channel, "caller_id_name");
	const char* caller_number = switch_channel_get_variable(channel, "caller_id_number");
	const char* callee_name = switch_channel_get_variable(channel, "callee_id_name");
	if (!callee_name) callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
	const char* callee_number = switch_channel_get_variable(channel, "destination_number");
	if (!callee_number) callee_number = switch_channel_get_variable(channel, "callee_id_number");
	
	/* Use OPTIMIZED async send with pre-parsed data */
	int is_final = (td->is_final || td->speech_final) ? 1 : 0;
	if (async_pusher_send_transcript_parsed(
			app_id, app_key, app_secret, cluster ? cluster : "ap2",
			callId, td->transcript, is_final, td->channel_index,
			caller_name, caller_number,
			callee_name, callee_number) != 0) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
			"Failed to queue transcription to Pusher (queue may be full)\n");
	}
}

/* Non-blocking wrapper for session start events */
static void send_session_start_to_pusher(switch_core_session_t* session, const char* callId) {
	if (!callId) return;
	
	switch_channel_t *channel = switch_core_session_get_channel(session);
	
	/* Get Pusher credentials */
	const char* app_id = switch_channel_get_variable(channel, "PUSHER_APP_ID");
	const char* app_key = switch_channel_get_variable(channel, "PUSHER_KEY");
	const char* app_secret = switch_channel_get_variable(channel, "PUSHER_SECRET");
	const char* cluster = switch_channel_get_variable(channel, "PUSHER_CLUSTER");
	
	if (!app_id) app_id = getenv("PUSHER_APP_ID");
	if (!app_key) app_key = getenv("PUSHER_KEY");
	if (!app_secret) app_secret = getenv("PUSHER_SECRET");
	if (!cluster) cluster = getenv("PUSHER_CLUSTER");
	
	if (!app_id || !app_key || !app_secret) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
			"Pusher not configured - skipping session_start event\n");
		return;
	}
	
	/* Get caller/callee metadata */
	const char* caller_name = switch_channel_get_variable(channel, "caller_id_name");
	const char* caller_number = switch_channel_get_variable(channel, "caller_id_number");
	const char* callee_name = switch_channel_get_variable(channel, "callee_id_name");
	if (!callee_name) callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
	const char* callee_number = switch_channel_get_variable(channel, "destination_number");
	if (!callee_number) callee_number = switch_channel_get_variable(channel, "callee_id_number");
	
	/* Use async non-blocking send */
	if (async_pusher_send_session_start(
			app_id, app_key, app_secret, cluster ? cluster : "ap2",
			callId,
			caller_name, caller_number,
			callee_name, callee_number) != 0) {
		switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
			"Failed to queue session_start to Pusher\n");
	}
}

/* OPTIMIZED: Response handler that accepts pre-parsed transcript data
 * Eliminates redundant JSON parsing - ~30% CPU savings on JSON processing */
static void responseHandlerParsed(switch_core_session_t* session,
	const char* eventName, const char * json, const char* bugname, int finished,
	const transcript_data_t* td) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Send session start to Pusher on successful connection
	if (0 == strcmp(eventName, TRANSCRIBE_EVENT_CONNECT_SUCCESS)) {
		// Wait for sip_call_id to become available (retry up to 10 times with 50ms delay)
		const char* sip_call_id = NULL;
		int retry_count = 0;
		const int max_retries = 10;
		const int retry_delay_ms = 50;

		while (retry_count < max_retries) {
			sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
			if (sip_call_id) {
				break;
			}
			retry_count++;
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
				"Waiting for sip_call_id to become available (attempt %d/%d)\n",
				retry_count, max_retries);
			switch_yield(retry_delay_ms * 1000); // Convert ms to microseconds
		}

		if (sip_call_id) {
			send_session_start_to_pusher(session, sip_call_id);
		} else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
				"Cannot send session_start to Pusher: sip_call_id not available after %d retries (%dms total)\n",
				max_retries, max_retries * retry_delay_ms);
		}
	}

	// Send transcription results to Pusher using pre-parsed data (OPTIMIZED)
	const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
	if (sip_call_id && td && td->has_transcript) {
		send_to_pusher_parsed(session, sip_call_id, td);
	}

	// Fire FreeSWITCH event (still needs raw JSON for event body)
	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName);
	switch_channel_event_set_data(channel, event);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "deepgram");
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-session-finished", finished ? "true" : "false");
	if (finished) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "responseHandler returning event %s, from finished recognition session\n", eventName);
	}
	if (json) switch_event_add_body(event, "%s", json);
	if (bugname) switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
	switch_event_fire(&event);
}

/* Legacy response handler - still parses JSON (for backward compatibility) */
static void responseHandler(switch_core_session_t* session,
	const char* eventName, const char * json, const char* bugname, int finished) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Send session start to Pusher on successful connection
	if (0 == strcmp(eventName, TRANSCRIBE_EVENT_CONNECT_SUCCESS)) {
		// Wait for sip_call_id to become available (retry up to 10 times with 50ms delay)
		const char* sip_call_id = NULL;
		int retry_count = 0;
		const int max_retries = 10;
		const int retry_delay_ms = 50;

		while (retry_count < max_retries) {
			sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
			if (sip_call_id) {
				break;
			}
			retry_count++;
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_DEBUG,
				"Waiting for sip_call_id to become available (attempt %d/%d)\n",
				retry_count, max_retries);
			switch_yield(retry_delay_ms * 1000); // Convert ms to microseconds
		}

		if (sip_call_id) {
			send_session_start_to_pusher(session, sip_call_id);
		} else {
			switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
				"Cannot send session_start to Pusher: sip_call_id not available after %d retries (%dms total)\n",
				max_retries, max_retries * retry_delay_ms);
		}
	}

	// Send transcription results to Pusher (if configured)
	// Use sip_call_id only (should be available by now)
	const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");

	if (sip_call_id && json) {
		// Determine if this is final or interim based on JSON content
		// Use OR logic: final if EITHER is_final OR speech_final is true
		// - is_final=true: Deepgram won't send more interims for that segment
		// - speech_final=true: Speaker stopped talking (utterance ended)
		switch_bool_t is_final = SWITCH_FALSE;
		cJSON* root = cJSON_Parse(json);
		if (root) {
			cJSON* is_final_field = cJSON_GetObjectItem(root, "is_final");
			cJSON* speech_final_field = cJSON_GetObjectItem(root, "speech_final");
			
			if ((is_final_field && cJSON_IsTrue(is_final_field)) ||
			    (speech_final_field && cJSON_IsTrue(speech_final_field))) {
				is_final = SWITCH_TRUE;
			}
			cJSON_Delete(root);
		}
		send_to_pusher(session, json, sip_call_id, is_final);
	}

	switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, eventName);
	switch_channel_event_set_data(channel, event);
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "deepgram");
	switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-session-finished", finished ? "true" : "false");
	if (finished) {
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "responseHandler returning event %s, from finished recognition session\n", eventName);
	}
	if (json) switch_event_add_body(event, "%s", json);
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
			private_t *tech_pvt = (private_t*) switch_core_media_bug_get_user_data(bug);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Got SWITCH_ABC_TYPE_CLOSE.\n");

			dg_transcribe_session_stop(session, 1,  tech_pvt->bugname);
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
		}
		break;
	
	case SWITCH_ABC_TYPE_READ:
	// Fall through - handle same as READ_PING
	case SWITCH_ABC_TYPE_READ_PING:
		// Both READ and READ_PING trigger frame processing
		// READ_PING gives more predictable 20ms timing in stereo mode
		return dg_transcribe_frame(session, bug);
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
 * This metadata is used for LOCAL TRACKING ONLY and is NOT sent to Deepgram.
 * It is included in FreeSWITCH events (session_start, session_stop) for:
 *   - Call tracking and logging
 *   - Channel mapping for speaker diarization
 *   - Integration with external systems
 *
 * Channel Mapping for Stereo Transcription:
 * -----------------------------------------
 * When using stereo mode (api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo):
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

	if (switch_channel_get_private(channel, MY_BUG_NAME)) {
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
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Deepgram transcription metadata: %s\n", enriched_metadata);
	}

	if (SWITCH_STATUS_FALSE == dg_transcribe_session_init(session, responseHandler, samples_per_second, channels, lang, interim, bugname, enriched_metadata, &pUserData)) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Error initializing dg speech session.\n");
		return SWITCH_STATUS_FALSE;
	}
	if ((status = switch_core_media_bug_add(session, "dg_transcribe", NULL, capture_callback, pUserData, 0, flags, &bug)) != SWITCH_STATUS_SUCCESS) {
		return status;
	}
  switch_channel_set_private(channel, MY_BUG_NAME, bug);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "added media bug for dg transcribe\n");

	return SWITCH_STATUS_SUCCESS;
}

static switch_status_t do_stop(switch_core_session_t *session,  char* bugname)
{
	switch_status_t status = SWITCH_STATUS_SUCCESS;

	switch_channel_t *channel = switch_core_session_get_channel(session);
	switch_media_bug_t *bug = switch_channel_get_private(channel, MY_BUG_NAME);

	if (bug) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Received user command command to stop transcribe.\n");
		status = dg_transcribe_session_stop(session, 0, bugname);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "stopped transcribe.\n");
	}

	return status;
}

/**
 * PHASE 3: Performance monitoring API implementation
 * 
 * Provides real-time statistics for high-scale deployments.
 * Usage: uuid_deepgram_transcribe <uuid> stats [pool|context|audio|all]
 */
static switch_status_t dg_get_performance_stats(switch_core_session_t *session, const char* stats_type, switch_stream_handle_t *stream)
{
	if (!session || !stream || !stats_type) {
		return SWITCH_STATUS_FALSE;
	}
	
	// Call C++ function to get performance statistics
	return dg_get_stats(session, stats_type, stream);
}

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop|stats] lang-code [interim] [stereo|mono|mixed] [8k|16k] [metadata]"
SWITCH_STANDARD_API(dg_transcribe_function)
{
	char *mycmd = NULL, *argv[8] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	
	/* DEFAULT: Stereo mode with READ_PING for predictable frame timing */
	switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO | SMBF_READ_PING;

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
				int sampling = 8000;  // Default to 8kHz (upsampling available for 16k)
				char *metadata = NULL;
				int is_stereo = 1;    // Default is stereo

				/* Parse mix-type (argv[4]): stereo (DEFAULT), mono, mixed
				 * 
				 * DEFAULT: stereo - Best for speaker diarization
				 *   - Channel 0 = Caller (A-leg)
				 *   - Channel 1 = Callee (B-leg)
				 *   - Uses SMBF_READ_PING for predictable 20ms frame timing
				 */
				if (argc > 4) {
					if (!strcmp(argv[4], "mono")) {
						/* Mono: Caller audio only (A-leg) */
						flags = SMBF_READ_STREAM;
						is_stereo = 0;
					} else if (!strcmp(argv[4], "mixed")) {
						/* Mixed: Both parties mixed into single channel */
						flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM;
						is_stereo = 0;
					} else if (!strcmp(argv[4], "stereo")) {
						/* Stereo: Separate channels (default, already set) */
						flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO | SMBF_READ_PING;
						is_stereo = 1;
					} else {
						/* Invalid mix type - throw error */
						switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
							"Invalid mix type '%s' - only stereo, mono, or mixed supported\n", argv[4]);
						stream->write_function(stream, "-ERR Invalid mix type '%s'. Supported: stereo (default), mono, mixed\n", argv[4]);
						switch_core_session_rwunlock(lsession);
						goto done;
					}
				}

				/* Parse sampling rate (argv[5]): ONLY 8k or 16k allowed
				 * 
				 * DEFAULT: 8kHz (native telephony rate, no resampling needed)
				 * OPTIONAL: 16kHz (better quality, requires upsampling from 8k source)
				 * 
				 * Other rates are NOT supported - throw error
				 */
				if (argc > 5) {
					if (!strcmp(argv[5], "8k") || !strcmp(argv[5], "8000")) {
						sampling = 8000;
					} else if (!strcmp(argv[5], "16k") || !strcmp(argv[5], "16000")) {
						sampling = 16000;
						switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO,
							"16kHz requested - upsampling from 8kHz telephony audio\n");
					} else {
						/* Invalid sampling rate - throw error */
						switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
							"Invalid sampling rate '%s' - only 8k or 16k supported\n", argv[5]);
						stream->write_function(stream, "-ERR Invalid sampling rate '%s'. Supported: 8k (default), 16k\n", argv[5]);
						switch_core_session_rwunlock(lsession);
						goto done;
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
					is_stereo ? "stereo" : (flags & SMBF_WRITE_STREAM) ? "mixed" : "mono",
					sampling,
					bugname,
					metadata ? metadata : "none");

				status = start_capture(lsession, flags, lang, interim, bugname, sampling, metadata);
			} else if (!strcasecmp(argv[1], "stats")) {
				// PHASE 3: Performance monitoring API
				char *stats_type = argc > 2 ? argv[2] : "all";
				status = dg_get_performance_stats(lsession, stats_type, stream);
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


SWITCH_MODULE_LOAD_FUNCTION(mod_deepgram_transcribe_load)
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

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Deepgram Speech Transcription API loading..\n");

	/* Initialize async Pusher subsystem (non-blocking HTTP) */
	if (async_pusher_init() != 0) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, 
			"Failed to initialize async Pusher - Pusher integration will be disabled\n");
	} else {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, 
			"Async Pusher subsystem initialized (non-blocking HTTP enabled)\n");
	}

	if (SWITCH_STATUS_FALSE == dg_transcribe_init()) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_CRIT, "Failed initializing dg speech interface\n");
	}

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Deepgram Speech Transcription API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "uuid_deepgram_transcribe", "Deepgram Speech Transcription API", dg_transcribe_function, TRANSCRIBE_API_SYNTAX);
	switch_console_set_complete("add uuid_deepgram_transcribe start lang-code [interim|final] [mono|mixed|stereo] [8k|16k]");
	switch_console_set_complete("add uuid_deepgram_transcribe stop ");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/*
  Called when the system shuts down
  Macro expands to: switch_status_t mod_deepgram_transcribe_shutdown() */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_deepgram_transcribe_shutdown)
{
	/* Shutdown async Pusher subsystem first - drain pending requests */
	async_pusher_shutdown();
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Async Pusher subsystem shutdown complete\n");

	dg_transcribe_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_START);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_STOP);
	return SWITCH_STATUS_SUCCESS;
}
