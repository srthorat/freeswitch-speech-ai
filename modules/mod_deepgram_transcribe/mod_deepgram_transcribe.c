/* 
 *
 * mod_deepgram_transcribe.c -- Freeswitch module for using dg streaming transcribe api
 *
 */
#include "mod_deepgram_transcribe.h"
#include "dg_transcribe_glue.h"

/* Prototypes */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_deepgram_transcribe_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_deepgram_transcribe_load);

SWITCH_MODULE_DEFINITION(mod_deepgram_transcribe, mod_deepgram_transcribe_load, mod_deepgram_transcribe_shutdown, NULL);

static switch_status_t do_stop(switch_core_session_t *session, char* bugname);

static void responseHandler(switch_core_session_t* session, 
	const char* eventName, const char * json, const char* bugname, int finished) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

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
 *     "call_info": {
 *       "caller_number": "1000",
 *       "caller_name": "Extension 1000",
 *       "callee_number": "1001",
 *       "callee_name": "Extension 1001",
 *       "direction": "inbound",
 *       "uuid": "session-uuid"
 *     },
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

	// Add call information
	cJSON *jCallInfo = cJSON_CreateObject();

	// Caller information (inbound leg / A-leg)
	const char *caller_number = switch_channel_get_variable(channel, "caller_id_number");
	const char *caller_name = switch_channel_get_variable(channel, "caller_id_name");
	if (caller_number) {
		cJSON_AddStringToObject(jCallInfo, "caller_number", caller_number);
	}
	if (caller_name) {
		cJSON_AddStringToObject(jCallInfo, "caller_name", caller_name);
	}

	// Callee information (outbound leg / B-leg)
	const char *callee_number = switch_channel_get_variable(channel, "destination_number");
	if (!callee_number) {
		callee_number = switch_channel_get_variable(channel, "callee_id_number");
	}
	const char *callee_name = switch_channel_get_variable(channel, "callee_id_name");
	if (!callee_name) {
		callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
	}

	if (callee_number) {
		cJSON_AddStringToObject(jCallInfo, "callee_number", callee_number);
	}
	if (callee_name) {
		cJSON_AddStringToObject(jCallInfo, "callee_name", callee_name);
	}

	// Add call direction
	const char *direction = switch_channel_direction(channel) == SWITCH_CALL_DIRECTION_INBOUND ? "inbound" : "outbound";
	cJSON_AddStringToObject(jCallInfo, "direction", direction);

	// Add UUID
	const char *uuid = switch_core_session_get_uuid(session);
	if (uuid) {
		cJSON_AddStringToObject(jCallInfo, "uuid", uuid);
	}

	// Add call_info object to metadata
	cJSON_AddItemToObject(jMetadata, "call_info", jCallInfo);

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

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]"
SWITCH_STANDARD_API(dg_transcribe_function)
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
	dg_transcribe_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_START);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_STOP);
	return SWITCH_STATUS_SUCCESS;
}
