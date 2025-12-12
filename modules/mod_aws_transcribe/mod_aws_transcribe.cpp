#include "mod_aws_transcribe.h"
#include "aws_transcribe_glue.h"
// #include "aws.h" // For aws_init and aws_cleanup - removed, using AWS SDK directly
#include "transcript_data.h"
#include <unordered_set>
#include <mutex>
#include <string>
#include <cstdlib>
#include <ctime>

extern "C" {
#include "async_pusher.h"
}
#include "worker_thread.h"
#include "aws_client_manager.h"

#include <switch_json.h>
#include <thread>
#include <vector>
#include <atomic>
#include "memory_pool.hpp"

// Forward declarations
void aws_init();
void aws_cleanup();

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_aws_transcribe_shutdown);
SWITCH_MODULE_LOAD_FUNCTION(mod_aws_transcribe_load);
SWITCH_MODULE_DEFINITION(mod_aws_transcribe, mod_aws_transcribe_load, mod_aws_transcribe_shutdown, NULL);

namespace {
    std::vector<std::thread> g_worker_threads;
    std::atomic<bool> g_running(false);
}

aws::ObjectPool<AwsPipe> g_pipe_pool; // Definition of the global object pool

// Global pusher client
async_pusher_t* g_pusher = nullptr;

// Track session start events to avoid duplicates
static std::unordered_set<std::string> g_session_start_sent;
static std::mutex g_session_start_mutex;

// Send session start event to Pusher (matches Deepgram pattern)
static void send_session_start_to_pusher(switch_core_session_t* session, const char* call_id) {
    if (!g_pusher || !call_id) return;

    switch_channel_t *channel = switch_core_session_get_channel(session);
    
    // Track that we've sent session_start for this call
    {
        std::lock_guard<std::mutex> lock(g_session_start_mutex);
        std::string call_id_str(call_id);
        if (g_session_start_sent.find(call_id_str) != g_session_start_sent.end()) {
            // Already sent for this call
            return;
        }
        g_session_start_sent.insert(call_id_str);
    }
    
    // Get caller/callee metadata
    const char* caller_name = switch_channel_get_variable(channel, "caller_id_name");
    const char* caller_number = switch_channel_get_variable(channel, "caller_id_number");
    const char* callee_name = switch_channel_get_variable(channel, "callee_id_name");
    if (!callee_name) callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
    const char* callee_number = switch_channel_get_variable(channel, "destination_number");
    if (!callee_number) callee_number = switch_channel_get_variable(channel, "callee_id_number");
    
    if (async_pusher_send_session_start(g_pusher, call_id,
            caller_name, caller_number,
            callee_name, callee_number) != 0) {
        switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
            "Failed to queue session_start event to Pusher for call_id=%s\n", call_id);
    }
}

static void responseHandler(switch_core_session_t* session, const transcript_data_t* td, const char* bugname) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

    // Handle connection success event - send session_start to Pusher (EXACT Deepgram pattern)
    if (td->is_connection_event && g_pusher) {
        // Wait for sip_call_id to become available (retry up to 10 times with 50ms delay)
        const char* sip_call_id = nullptr;
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

        // ONLY send if sip_call_id is available (matching Deepgram - NO UUID fallback!)
        if (sip_call_id) {
            send_session_start_to_pusher(session, sip_call_id);
        } else {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_ERROR,
                "Cannot send session_start to Pusher: sip_call_id not available after %d retries (%dms total)\n",
                max_retries, max_retries * retry_delay_ms);
        }

        // Don't fire FreeSWITCH event for connection events
        return;
    }

    // Skip empty transcripts
    if (!td->has_transcript) {
        return;
    }

    cJSON* jMessage = cJSON_CreateObject();
    cJSON_AddBoolToObject(jMessage, "is_final", td->is_final);
    cJSON_AddStringToObject(jMessage, "transcript", td->transcript);
    cJSON_AddNumberToObject(jMessage, "confidence", td->confidence);

    char* json = cJSON_PrintUnformatted(jMessage);

    switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, td->is_final ? TRANSCRIBE_EVENT_RESULTS_FINAL : TRANSCRIBE_EVENT_RESULTS);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "aws_transcribe message: %s\n", json);
    switch_channel_event_set_data(channel, event);
    switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "aws");
    switch_event_add_body(event, "%s", json);
	
    if (bugname) switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
	switch_event_fire(&event);

    // Pusher Integration - Send Transcription (EXACT Deepgram pattern - ONLY use sip_call_id)
    if (g_pusher) {
        const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");

        // ONLY send if sip_call_id is available (matching Deepgram - NO UUID fallback!)
        if (!sip_call_id) {
            // Skip Pusher send if sip_call_id not available
            free(json);
            cJSON_Delete(jMessage);
            return;
        }

        // Fallback: Send session_start on first transcript if we couldn't send it on connection
        {
            std::lock_guard<std::mutex> lock(g_session_start_mutex);
            std::string call_id_str(sip_call_id);
            if (g_session_start_sent.find(call_id_str) == g_session_start_sent.end()) {
                send_session_start_to_pusher(session, sip_call_id);
            }
        }

        const char* call_id = sip_call_id;  // Use sip_call_id directly
            
        // Get caller/callee metadata
        const char* caller_name = switch_channel_get_variable(channel, "caller_id_name");
        const char* caller_number = switch_channel_get_variable(channel, "caller_id_number");
        const char* callee_name = switch_channel_get_variable(channel, "callee_id_name");
        if (!callee_name) callee_name = switch_channel_get_variable(channel, "effective_callee_id_name");
        const char* callee_number = switch_channel_get_variable(channel, "destination_number");
        if (!callee_number) callee_number = switch_channel_get_variable(channel, "callee_id_number");

        if (async_pusher_send_transcript_parsed(g_pusher, call_id, td,
                caller_name, caller_number,
                callee_name, callee_number) != 0) {
            switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_WARNING,
                "Failed to queue transcription to Pusher for call_id=%s\n", call_id);
        }
    }

    free(json);
    cJSON_Delete(jMessage);
}

#define TRANSCRIBE_API_SYNTAX "<uuid> [start|stop|stats] lang-code [interim] [stereo|mono|mixed] [8k|16k] [bugname] [metadata]"

static switch_status_t start_capture(switch_core_session_t* session, const char* lang, int interim, switch_media_bug_flag_t flags, int sampling, const char* bugname, const char* metadata) {
    void* pUserData;
    int channels = (flags & SMBF_STEREO) ? 2 : 1;
    return aws_transcribe_session_init(session, responseHandler, sampling, channels, lang, interim, bugname, metadata, &pUserData, flags);
}

SWITCH_STANDARD_API(aws_transcribe_function)
{
	char *mycmd = NULL, *argv[8] = { 0 };
	int argc = 0;
	switch_status_t status = SWITCH_STATUS_FALSE;
	
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

		if (!strcasecmp(argv[1], "stats")) {
			// Performance monitoring command
			uint64_t jobs_processed = get_worker_stats_jobs_processed();
			uint64_t active_sessions = get_worker_stats_active_sessions();
			uint64_t client_count = g_client_manager.getClientCount();
			
			stream->write_function(stream, "+OK AWS Transcribe Performance Stats:\n");
			stream->write_function(stream, "  Jobs Processed: %lu\n", jobs_processed);
			stream->write_function(stream, "  Active Sessions: %lu\n", active_sessions);
			stream->write_function(stream, "  AWS Clients: %lu\n", client_count);
			stream->write_function(stream, "  Memory Pool Usage: [Available in next version]\n");
			status = SWITCH_STATUS_SUCCESS;
		} else if ((lsession = switch_core_session_locate(argv[0]))) {
			if (!strcasecmp(argv[1], "stop")) {
				const char *bugname = argc > 2 ? argv[2] : "aws_transcribe";
    		    switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "stop transcribing\n");
				status = aws_transcribe_session_stop(lsession, 0, (char*)bugname);
			} else if (!strcasecmp(argv[1], "start")) {
                const char* lang = argv[2];
                int interim = argc > 3 && !strcmp(argv[3], "interim");
                
                // Defaults
                switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO;
                int sampling = 16000;
                const char* bugname = "aws_transcribe";
                const char* metadata = NULL;

				if (argc > 4) {
					if (strcmp(argv[4], "mono") == 0) {
						flags = SMBF_READ_STREAM;
					}
					else if (strcmp(argv[4], "mixed") == 0) {
						flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM;
					}
				}
				if (argc > 5) {
                    if (strcmp(argv[5], "8k") == 0 || strcmp(argv[5], "8000") == 0) {
                        sampling = 8000;
                    }
				}
				if (argc > 6) {
					bugname = argv[6];
				}
                if (argc > 7) {
                    metadata = argv[7];
                }

                switch_log_printf(SWITCH_CHANNEL_SESSION_LOG(session), SWITCH_LOG_INFO, "start transcribing: lang %s, interim %d, stereo %d, rate %d\n", 
                    lang, interim, (flags & SMBF_STEREO) ? 1:0, sampling);
                
                status = start_capture(lsession, lang, interim, flags, sampling, bugname, metadata);
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

	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AWS Speech Transcription API loading..\n");

    aws_init();

    // Initialize memory pool
    g_pipe_pool.initialize(5000); // Default size of 5000 sessions

    // Start worker threads
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 1;
    g_running = true;
    for (unsigned int i = 0; i < num_threads; ++i) {
        g_worker_threads.emplace_back(worker_thread_run, std::ref(g_running));
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Started %u worker threads\n", num_threads);

    const char* pusher_app_id = std::getenv("PUSHER_APP_ID");
    const char* pusher_key = std::getenv("PUSHER_KEY");
    const char* pusher_secret = std::getenv("PUSHER_SECRET");
    const char* pusher_cluster = std::getenv("PUSHER_CLUSTER");

    if (pusher_app_id && pusher_key && pusher_secret && pusher_cluster) {
        g_pusher = async_pusher_create(pusher_app_id, pusher_key, pusher_secret, pusher_cluster);
        if (g_pusher) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "AsyncPusher initialized.\n");
        } else {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to initialize AsyncPusher.\n");
        }
    }

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AWS Speech Transcription API successfully loaded\n");

	SWITCH_ADD_API(api_interface, "uuid_aws_transcribe", "AWS Speech Transcription API", aws_transcribe_function, TRANSCRIBE_API_SYNTAX);
	
	return SWITCH_STATUS_SUCCESS;
}

SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_aws_transcribe_shutdown)
{
    if (g_running) {
        g_running = false;
        for (auto& t : g_worker_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
    }

    // Log memory pool stats
    const auto& stats = g_pipe_pool.get_stats();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "[POOL STATS] AwsPipe pool: capacity=%zu, peak_usage=%lu, hits=%lu, misses=%lu\n",
        g_pipe_pool.capacity(),
        (unsigned long)stats.high_water_mark.load(),
        (unsigned long)stats.pool_hits.load(),
        (unsigned long)stats.pool_misses.load());


	aws_cleanup();

    if (g_pusher) {
        async_pusher_destroy(g_pusher);
        g_pusher = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_session_start_mutex);
            g_session_start_sent.clear();
        }
    }
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	return SWITCH_STATUS_SUCCESS;
}

void aws_init() {
    Aws::SDKOptions options;
    Aws::InitAPI(options);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AWS SDK Initialized\n");
}

void aws_cleanup() {
    Aws::SDKOptions options;
    Aws::ShutdownAPI(options);
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "AWS SDK Shutdown\n");
}
