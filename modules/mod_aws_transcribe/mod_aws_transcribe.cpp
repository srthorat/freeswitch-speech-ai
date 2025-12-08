#include "mod_aws_transcribe.h"
#include "aws_transcribe_glue.h"
// #include "aws.h" // For aws_init and aws_cleanup - removed, using AWS SDK directly
#include "transcript_data.h"
#include "async_pusher.hpp"
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

deepgram::ObjectPool<AwsPipe> g_pipe_pool; // Definition of the global object pool

// Global pusher client
std::unique_ptr<AsyncPusher> g_pusher;

static void responseHandler(switch_core_session_t* session, const transcript_data_t* td, const char* bugname) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

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
        g_pusher.reset(new AsyncPusher(pusher_app_id, pusher_key, pusher_secret, pusher_cluster));
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "AsyncPusher initialized.\n");
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
