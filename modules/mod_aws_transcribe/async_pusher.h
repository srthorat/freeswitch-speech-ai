/*
 * async_pusher.h -- Async Pusher Client for mod_aws_transcribe
 *
 * High-performance non-blocking Pusher HTTP delivery
 * Preserves exact same JSON format, call data, and speaker mapping
 * Only difference: async delivery instead of blocking curl
 *
 * Optimization for 2000+ concurrent calls:
 * - Worker thread pool processes HTTP requests
 * - Lock-free SPSC queue for request submission
 * - No blocking on transcription callback thread
 */

#ifndef __ASYNC_PUSHER_H__
#define __ASYNC_PUSHER_H__

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the async Pusher subsystem
 * Call once at module load time
 * 
 * @param num_workers Number of HTTP worker threads (default: 4)
 * @return SWITCH_STATUS_SUCCESS on success
 */
switch_status_t async_pusher_init(int num_workers);

/**
 * Shutdown the async Pusher subsystem
 * Call once at module unload time
 */
void async_pusher_shutdown(void);

/**
 * Send transcription to Pusher asynchronously
 * Non-blocking - returns immediately after queuing
 *
 * JSON format preserved exactly:
 * {
 *   "type": "final" | "interim",
 *   "speaker_id": "CallerName(CallerNumber)" | "CalleeName(CalleeNumber)",
 *   "text": "transcription text",
 *   "timestamp": "2025-01-01T12:00:00Z"
 * }
 *
 * Speaker mapping preserved exactly:
 * - Channel 0 = Caller (A-leg)
 * - Channel 1 = Callee (B-leg)
 *
 * @param session FreeSWITCH session for logging and channel variables
 * @param json Raw AWS transcription JSON (array format)
 * @param call_id SIP call ID for Pusher channel name
 * @param is_final Whether this is a final transcription
 */
void async_send_to_pusher(switch_core_session_t* session, 
                          const char* json, 
                          const char* call_id, 
                          switch_bool_t is_final);

/**
 * Send session start event to Pusher asynchronously
 * Non-blocking - returns immediately after queuing
 *
 * @param session FreeSWITCH session
 * @param call_id SIP call ID for Pusher channel name
 */
void async_send_session_start_to_pusher(switch_core_session_t* session, 
                                        const char* call_id);

/**
 * Send session end event to Pusher asynchronously
 * Non-blocking - returns immediately after queuing
 *
 * @param session FreeSWITCH session (may be NULL if session ended)
 * @param call_id SIP call ID for Pusher channel name
 */
void async_send_session_end_to_pusher(switch_core_session_t* session,
                                      const char* call_id);

/**
 * Get async Pusher statistics
 *
 * @param queued Output: number of requests currently queued
 * @param sent Output: total requests sent successfully
 * @param failed Output: total requests that failed
 */
void async_pusher_get_stats(uint64_t* queued, uint64_t* sent, uint64_t* failed);

#ifdef __cplusplus
}
#endif

#endif /* __ASYNC_PUSHER_H__ */
