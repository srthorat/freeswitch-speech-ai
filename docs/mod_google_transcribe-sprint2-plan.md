# Sprint 2: Metadata & Sessions - Implementation Plan

**Phase 2 of the mod_google_transcribe alignment with AWS/Deepgram patterns.**

---

## 📋 Sprint Overview

**Sprint Duration:** 1-2 days
**Priority:** High (Required for Pusher integration in Sprint 3)
**Status:** Not Started

### Goals

1. Extract call metadata from FreeSWITCH channel variables
2. Add session start/stop event firing
3. Prepare metadata structure for Pusher integration

### Tasks

- [ ] **Task 2.1:** Extract call metadata (caller, callee, SIP Call-ID)
- [ ] **Task 2.2:** Add session start/stop events

---

## 🎯 Task 2.1: Extract Call Metadata

### Objective

Extract caller information, callee information, and SIP Call-ID from FreeSWITCH channel variables when transcription starts. This metadata will be used for:
- Session identification
- Pusher channel naming (Sprint 3)
- Event enrichment
- Analytics/logging

### Reference Implementation

**File:** `modules/mod_aws_transcribe/mod_aws_transcribe.c:583-647`

```c
// Extract caller info
const char* caller_id_name = switch_channel_get_variable(channel, "caller_id_name");
const char* caller_id_number = switch_channel_get_variable(channel, "caller_id_number");

// Extract callee info
const char* callee_id_name = switch_channel_get_variable(channel, "callee_id_name");
if (!callee_id_name) {
    callee_id_name = switch_channel_get_variable(channel, "effective_callee_id_name");
}
const char* destination_number = switch_channel_get_variable(channel, "destination_number");

// Get SIP Call-ID
const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");

// Build metadata JSON
cJSON* metadata = cJSON_CreateObject();
if (caller_id_name) cJSON_AddStringToObject(metadata, "callerName", caller_id_name);
if (caller_id_number) cJSON_AddStringToObject(metadata, "callerNumber", caller_id_number);
if (callee_id_name) cJSON_AddStringToObject(metadata, "calleeName", callee_id_name);
if (destination_number) cJSON_AddStringToObject(metadata, "calleeNumber", destination_number);
if (sip_call_id) cJSON_AddStringToObject(metadata, "call-Id", sip_call_id);
```

### Implementation Steps

#### Step 1: Add cJSON Include

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** After line 9 (after `#include <switch.h>`)

```c
#include <switch.h>
#include <switch_json.h>  // Add this line for cJSON support
```

#### Step 2: Add Metadata Structure to cap_cb

**File:** `modules/mod_google_transcribe/mod_google_transcribe.h`
**Current struct (estimated):**

```c
struct cap_cb {
    switch_mutex_t *mutex;
    char *bugname;
    void *session_data;
    // Add metadata fields here
};
```

**Add these fields:**

```c
struct cap_cb {
    switch_mutex_t *mutex;
    char *bugname;
    void *session_data;

    // Call metadata (Task 2.1)
    char *caller_name;
    char *caller_number;
    char *callee_name;
    char *callee_number;
    char *sip_call_id;
};
```

#### Step 3: Extract Metadata in start_capture()

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** After line 217 (inside `start_capture()` function, after `switch_channel_t *channel = ...`)

**Add metadata extraction code:**

```c
static switch_status_t start_capture(switch_core_session_t *session, switch_media_bug_flag_t flags,
  char* lang, int interim, char* bugname, int sampling, char* metadata, GoogleCloudServiceVersion version)
{
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Extract call metadata (Task 2.1)
	const char* caller_id_name = switch_channel_get_variable(channel, "caller_id_name");
	const char* caller_id_number = switch_channel_get_variable(channel, "caller_id_number");
	const char* callee_id_name = switch_channel_get_variable(channel, "callee_id_name");
	if (!callee_id_name) {
		callee_id_name = switch_channel_get_variable(channel, "effective_callee_id_name");
	}
	const char* destination_number = switch_channel_get_variable(channel, "destination_number");
	const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");

	// Log metadata for debugging
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
		"Call metadata: caller=%s (%s), callee=%s (%s), call-id=%s\n",
		caller_id_name ? caller_id_name : "unknown",
		caller_id_number ? caller_id_number : "unknown",
		callee_id_name ? callee_id_name : "unknown",
		destination_number ? destination_number : "unknown",
		sip_call_id ? sip_call_id : "unknown");

	// Continue with existing code...
	switch_media_bug_t *bug;
	switch_status_t status;
	// ... rest of function
}
```

#### Step 4: Store Metadata in cap_cb Structure

**Location:** When initializing `pUserData` (before `switch_core_media_bug_add`)

**Pattern from AWS module:**

```c
// After google_speech_session_init_vX() call, store metadata in cap_cb
struct cap_cb *cb = (struct cap_cb *) pUserData;
if (cb) {
	cb->caller_name = caller_id_name ? switch_core_strdup(pool, caller_id_name) : NULL;
	cb->caller_number = caller_id_number ? switch_core_strdup(pool, caller_id_number) : NULL;
	cb->callee_name = callee_id_name ? switch_core_strdup(pool, callee_id_name) : NULL;
	cb->callee_number = destination_number ? switch_core_strdup(pool, destination_number) : NULL;
	cb->sip_call_id = sip_call_id ? switch_core_strdup(pool, sip_call_id) : NULL;
}
```

**Note:** May need to check `google_glue.h` to confirm `cap_cb` structure definition.

#### Step 5: Build Metadata JSON Function

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** Add new helper function before `start_capture()`

```c
static char* build_metadata_json(struct cap_cb *cb) {
	cJSON *metadata = cJSON_CreateObject();
	char *json_str = NULL;

	if (cb->caller_name) {
		cJSON_AddStringToObject(metadata, "callerName", cb->caller_name);
	}
	if (cb->caller_number) {
		cJSON_AddStringToObject(metadata, "callerNumber", cb->caller_number);
	}
	if (cb->callee_name) {
		cJSON_AddStringToObject(metadata, "calleeName", cb->callee_name);
	}
	if (cb->callee_number) {
		cJSON_AddStringToObject(metadata, "calleeNumber", cb->callee_number);
	}
	if (cb->sip_call_id) {
		cJSON_AddStringToObject(metadata, "call-Id", cb->sip_call_id);
	}

	json_str = cJSON_PrintUnformatted(metadata);
	cJSON_Delete(metadata);

	return json_str;
}
```

### Testing Task 2.1

```bash
# Start transcription
uuid_google_transcribe <uuid> start en-US interim

# Check FreeSWITCH logs for metadata extraction
fs_cli -x "console loglevel debug"

# Expected log output:
# Call metadata: caller=John Doe (1234567890), callee=Support (8005551234), call-id=abc123@host.com
```

---

## 🎯 Task 2.2: Add Session Start/Stop Events

### Objective

Fire custom events when transcription session starts and stops. These events will:
- Notify external systems of session lifecycle
- Trigger Pusher session-start/stop events (Sprint 3)
- Provide session metadata to event subscribers

### Reference Implementation

**File:** `modules/mod_aws_transcribe/mod_aws_transcribe.c:457-519`

```c
// responseHandler function (simplified)
static void responseHandler(switch_core_session_t* session, const char* json, const char* bugname, char* metadata) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Session start event
	if (0 == strcmp("session-start", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_SESSION_START);
		switch_channel_event_set_data(channel, event);
		if (metadata) {
			switch_event_add_body(event, "%s", metadata);
		}
		switch_event_fire(&event);
		return;
	}

	// Session stop event
	if (0 == strcmp("session-stop", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_SESSION_STOP);
		switch_channel_event_set_data(channel, event);
		if (metadata) {
			switch_event_add_body(event, "%s", metadata);
		}
		switch_event_fire(&event);
		return;
	}

	// Other events...
}
```

### Implementation Steps

#### Step 1: Define New Event Types

**File:** `modules/mod_google_transcribe/mod_google_transcribe.h`
**Location:** After existing event definitions

**Current events (estimated):**
```c
#define TRANSCRIBE_EVENT_RESULTS "google_transcribe::transcription"
#define TRANSCRIBE_EVENT_END_OF_UTTERANCE "google_transcribe::end_of_utterance"
// ... other events
```

**Add new events:**
```c
#define TRANSCRIBE_EVENT_SESSION_START "google_transcribe::session_start"
#define TRANSCRIBE_EVENT_SESSION_STOP "google_transcribe::session_stop"
```

#### Step 2: Register New Event Subclasses

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** In `mod_transcribe_load()` function (around line 506-548)

**Add registrations:**
```c
SWITCH_MODULE_LOAD_FUNCTION(mod_transcribe_load)
{
	// ... existing registrations

	// Register session events (Task 2.2)
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_SESSION_START) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_SESSION_START);
		return SWITCH_STATUS_TERM;
	}
	if (switch_event_reserve_subclass(TRANSCRIBE_EVENT_SESSION_STOP) != SWITCH_STATUS_SUCCESS) {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Couldn't register subclass %s!\n", TRANSCRIBE_EVENT_SESSION_STOP);
		return SWITCH_STATUS_TERM;
	}

	// ... rest of function
}
```

#### Step 3: Free Event Subclasses on Shutdown

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** In `mod_transcribe_shutdown()` function (around line 572-586)

**Add cleanup:**
```c
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_transcribe_shutdown)
{
	google_speech_cleanup();
	switch_event_free_subclass(TRANSCRIBE_EVENT_RESULTS);
	// ... existing event frees

	// Free session events (Task 2.2)
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_START);
	switch_event_free_subclass(TRANSCRIBE_EVENT_SESSION_STOP);

	return SWITCH_STATUS_SUCCESS;
}
```

#### Step 4: Update responseHandler to Fire Session Events

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** In `responseHandler()` function (around line 65-144)

**Current events handled:**
- `"vad_detected"`
- `"end_of_utterance"`
- `"start_of_speech"`
- etc.

**Add new handlers at the beginning of the function:**

```c
static void responseHandler(switch_core_session_t* session, const char * json, const char* bugname) {
	switch_event_t *event;
	switch_channel_t *channel = switch_core_session_get_channel(session);

	// Session start event (Task 2.2)
	if (0 == strcmp("session-start", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_SESSION_START);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
		if (bugname) {
			switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
		}
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Transcription session started: %s\n", bugname ? bugname : "google_transcribe");
		switch_event_fire(&event);
		return;
	}

	// Session stop event (Task 2.2)
	if (0 == strcmp("session-stop", json)) {
		switch_event_create_subclass(&event, SWITCH_EVENT_CUSTOM, TRANSCRIBE_EVENT_SESSION_STOP);
		switch_channel_event_set_data(channel, event);
		switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "transcription-vendor", "google");
		if (bugname) {
			switch_event_add_header_string(event, SWITCH_STACK_BOTTOM, "media-bugname", bugname);
		}
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Transcription session stopped: %s\n", bugname ? bugname : "google_transcribe");
		switch_event_fire(&event);
		return;
	}

	// Existing event handlers...
	if (0 == strcmp("vad_detected", json)) {
		// ... existing code
	}
	// ... rest of function
}
```

#### Step 5: Fire Session Start Event

**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Location:** In `capture_callback()` function (around line 146-177)

**Current code:**
```c
case SWITCH_ABC_TYPE_INIT:
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_INIT.\n");
	responseHandler(session, "start_of_transcript", cb->bugname);
	break;
```

**Update to:**
```c
case SWITCH_ABC_TYPE_INIT:
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_INIT.\n");
	responseHandler(session, "session-start", cb->bugname);  // Task 2.2: Session start
	responseHandler(session, "start_of_transcript", cb->bugname);
	break;
```

#### Step 6: Fire Session Stop Event

**Location:** In `capture_callback()` function, CLOSE case

**Current code:**
```c
case SWITCH_ABC_TYPE_CLOSE:
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_CLOSE, calling google_speech_session_cleanup.\n");
		responseHandler(session, "end_of_transcript", cb->bugname);
		cleanup_callback(session, 1, bug);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
	}
	break;
```

**Update to:**
```c
case SWITCH_ABC_TYPE_CLOSE:
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Got SWITCH_ABC_TYPE_CLOSE, calling google_speech_session_cleanup.\n");
		responseHandler(session, "end_of_transcript", cb->bugname);
		responseHandler(session, "session-stop", cb->bugname);  // Task 2.2: Session stop
		cleanup_callback(session, 1, bug);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "Finished SWITCH_ABC_TYPE_CLOSE.\n");
	}
	break;
```

### Testing Task 2.2

#### Method 1: ESL Event Listener

```python
import ESL

con = ESL.ESLconnection("localhost", "8021", "ClueCon")
con.events("plain", "CUSTOM google_transcribe::session_start google_transcribe::session_stop")

while True:
    e = con.recvEvent()
    if e:
        print(f"Event: {e.getHeader('Event-Subclass')}")
        print(f"Vendor: {e.getHeader('transcription-vendor')}")
        print(f"Bugname: {e.getHeader('media-bugname')}")
```

#### Method 2: FreeSWITCH Console

```bash
# Enable event logging
fs_cli -x "event plain CUSTOM google_transcribe::session_start google_transcribe::session_stop"

# Start transcription
uuid_google_transcribe <uuid> start en-US interim

# Expected output:
# Event-Name: CUSTOM
# Event-Subclass: google_transcribe::session_start
# transcription-vendor: google
# media-bugname: google_transcribe

# Stop transcription
uuid_google_transcribe <uuid> stop

# Expected output:
# Event-Name: CUSTOM
# Event-Subclass: google_transcribe::session_stop
# transcription-vendor: google
# media-bugname: google_transcribe
```

---

## 📊 Sprint 2 Checklist

### Task 2.1: Extract Call Metadata
- [ ] Add `#include <switch_json.h>` to mod_google_transcribe.c
- [ ] Add metadata fields to `cap_cb` struct in mod_google_transcribe.h
- [ ] Extract metadata from channel variables in `start_capture()`
- [ ] Store metadata in `cap_cb` structure
- [ ] Create `build_metadata_json()` helper function
- [ ] Test metadata extraction with test call
- [ ] Verify metadata logged to console

### Task 2.2: Add Session Start/Stop Events
- [ ] Define `TRANSCRIBE_EVENT_SESSION_START` in mod_google_transcribe.h
- [ ] Define `TRANSCRIBE_EVENT_SESSION_STOP` in mod_google_transcribe.h
- [ ] Register event subclasses in `mod_transcribe_load()`
- [ ] Free event subclasses in `mod_transcribe_shutdown()`
- [ ] Add session-start handler to `responseHandler()`
- [ ] Add session-stop handler to `responseHandler()`
- [ ] Fire session-start in `SWITCH_ABC_TYPE_INIT`
- [ ] Fire session-stop in `SWITCH_ABC_TYPE_CLOSE`
- [ ] Test events with ESL listener
- [ ] Verify events in FreeSWITCH console

### Documentation & Commit
- [ ] Update alignment plan with Sprint 2 completion status
- [ ] Update TODO.md to mark Phase 2 complete
- [ ] Create commit for Task 2.1 (metadata extraction)
- [ ] Create commit for Task 2.2 (session events)
- [ ] Push commits to remote branch

---

## 🔍 Files to Modify

### Primary Files

1. **modules/mod_google_transcribe/mod_google_transcribe.h**
   - Add metadata fields to `cap_cb` struct
   - Define new event types

2. **modules/mod_google_transcribe/mod_google_transcribe.c**
   - Add cJSON include
   - Extract metadata in `start_capture()`
   - Create `build_metadata_json()` function
   - Update `responseHandler()` for session events
   - Fire events in `capture_callback()`
   - Register/free event subclasses

### Reference Files (Read-Only)

1. **modules/mod_aws_transcribe/mod_aws_transcribe.c** (lines 457-647)
   - Metadata extraction pattern
   - Session event firing pattern

---

## ⚠️ Important Notes

1. **Check google_glue.h First:** Verify `cap_cb` structure definition before modifying
2. **Memory Management:** Use `switch_core_strdup()` for string copies
3. **Event Order:** session-start → transcripts → session-stop
4. **Backward Compatibility:** Existing events must continue to work
5. **Metadata is Optional:** Handle NULL values gracefully

---

## 🎯 Success Criteria

Sprint 2 is complete when:

1. ✅ Caller name, number extracted from channel variables
2. ✅ Callee name, number extracted from channel variables
3. ✅ SIP Call-ID extracted from channel variables
4. ✅ Metadata stored in `cap_cb` structure
5. ✅ Metadata logged to console when transcription starts
6. ✅ `TRANSCRIBE_EVENT_SESSION_START` fires when transcription starts
7. ✅ `TRANSCRIBE_EVENT_SESSION_STOP` fires when transcription stops
8. ✅ Events contain vendor and bugname headers
9. ✅ Events receivable via ESL
10. ✅ All existing events still work

---

## 🚀 Next Sprint

**Sprint 3: Pusher Integration** (3-4 days)
- Task 3.1: Add Pusher credential reading from env vars
- Task 3.2: Implement Pusher HTTP API with HMAC SHA256 signing
- Task 3.3: Integrate with response handler

Sprint 2 metadata and events are prerequisites for Sprint 3.

---

**Document Version:** 1.0
**Created:** Sprint 1 Complete
**Sprint 2 Status:** Not Started
