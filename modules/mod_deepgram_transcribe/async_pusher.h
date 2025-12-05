/*
 * async_pusher.h - Non-blocking Pusher integration
 * 
 * Uses async_http for non-blocking HTTP POST to Pusher API
 * Replaces the blocking send_to_pusher() implementation
 */

#ifndef __ASYNC_PUSHER_H__
#define __ASYNC_PUSHER_H__

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the async Pusher subsystem
 * Must be called once at module load
 * 
 * @return 0 on success, -1 on failure
 */
int async_pusher_init(void);

/**
 * Shutdown the async Pusher subsystem
 * Drains pending requests and frees resources
 */
void async_pusher_shutdown(void);

/**
 * Send transcription to Pusher (non-blocking)
 * Returns immediately after queuing the request
 * 
 * @param app_id        Pusher App ID
 * @param app_key       Pusher Key
 * @param app_secret    Pusher Secret
 * @param cluster       Pusher cluster (e.g., "ap2", "mt1")
 * @param call_id       Call identifier for Pusher channel
 * @param json          Raw Deepgram transcription JSON
 * @param is_final      1 for final, 0 for interim
 * @param caller_name   Caller name (may be NULL)
 * @param caller_number Caller number (may be NULL)
 * @param callee_name   Callee name (may be NULL)
 * @param callee_number Callee number (may be NULL)
 * 
 * @return 0 on success, -1 if queue is full
 */
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
);

/**
 * Send session start event to Pusher (non-blocking)
 * 
 * @param app_id        Pusher App ID
 * @param app_key       Pusher Key
 * @param app_secret    Pusher Secret
 * @param cluster       Pusher cluster
 * @param call_id       Call identifier for Pusher channel
 * @param caller_name   Caller name (may be NULL)
 * @param caller_number Caller number (may be NULL)
 * @param callee_name   Callee name (may be NULL)
 * @param callee_number Callee number (may be NULL)
 * 
 * @return 0 on success, -1 if queue is full
 */
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
);

/**
 * Get statistics about async Pusher operations
 */
typedef struct {
    uint64_t queued;
    uint64_t sent;
    uint64_t failed;
    uint64_t dropped;
} async_pusher_stats_t;

void async_pusher_get_stats(async_pusher_stats_t* stats);

/**
 * Send transcription to Pusher using pre-parsed data (OPTIMIZED)
 * Avoids redundant JSON parsing - data already extracted upstream
 * 
 * @param app_id        Pusher App ID
 * @param app_key       Pusher Key
 * @param app_secret    Pusher Secret
 * @param cluster       Pusher cluster (e.g., "ap2", "mt1")
 * @param call_id       Call identifier for Pusher channel
 * @param transcript    Pre-extracted transcript text
 * @param is_final      1 for final, 0 for interim
 * @param channel_index Speaker channel (0=caller, 1=callee)
 * @param caller_name   Caller name (may be NULL)
 * @param caller_number Caller number (may be NULL)
 * @param callee_name   Callee name (may be NULL)
 * @param callee_number Callee number (may be NULL)
 * 
 * @return 0 on success, -1 if queue is full or invalid params
 */
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
);

#ifdef __cplusplus
}
#endif

#endif /* __ASYNC_PUSHER_H__ */
