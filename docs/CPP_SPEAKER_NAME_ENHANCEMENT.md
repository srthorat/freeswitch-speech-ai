# C++ Module Enhancement: Automatic Speaker Name Injection

This document explains how to modify the C++ transcription modules (AWS, Deepgram, Azure) to automatically include speaker names in the transcription JSON output.

## Problem

Currently, the modules only return channel identifiers:

```json
{
  "is_final": true,
  "channel_id": "ch_0",
  "alternatives": [{
    "transcript": "Hello"
  }]
}
```

Applications must map `ch_0` → speaker names in their code.

## Solution

Modify the C++ code to automatically read `caller_name` and `callee_name` from channel variables and inject them into the JSON:

```json
{
  "is_final": true,
  "channel_id": "ch_0",
  "speaker_name": "John Doe",
  "speaker_number": "1002",
  "alternatives": [{
    "transcript": "Hello"
  }]
}
```

---

## Implementation

### 1. AWS Transcribe Module

**File:** `modules/mod_aws_transcribe/aws_transcribe_glue.cpp`

**Location:** In the `processData()` function, around line 270-295

**Changes:**

```cpp
// BEFORE: Line ~270
if (m_transcript.TranscriptHasBeenSet()) {
    switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
    if (psession) {
        bool isFinal = false;
        std::ostringstream s;
        s << "[";
        for (auto&& r : m_transcript.GetTranscript().GetResults()) {
            // ... build JSON
        }
    }
}

// AFTER: Add speaker name reading
if (m_transcript.TranscriptHasBeenSet()) {
    switch_core_session_t* psession = switch_core_session_locate(m_sessionId.c_str());
    if (psession) {
        // ✅ ADD THIS: Get speaker names from channel variables
        switch_channel_t* channel = switch_core_session_get_channel(psession);
        const char* caller_name = switch_channel_get_variable(channel, "caller_name");
        const char* caller_number = switch_channel_get_variable(channel, "caller_number");
        const char* callee_name = switch_channel_get_variable(channel, "callee_name");
        const char* callee_number = switch_channel_get_variable(channel, "callee_number");

        // Set defaults
        if (!caller_name) caller_name = "";
        if (!caller_number) caller_number = "";
        if (!callee_name) callee_name = "";
        if (!callee_number) callee_number = "";

        bool isFinal = false;
        std::ostringstream s;
        s << "[";
        for (auto&& r : m_transcript.GetTranscript().GetResults()) {
            std::ostringstream t1;

            // ✅ ADD THIS: Extract channel_id from AWS result
            std::string channelId = "";
            if (r.ChannelIdHasBeenSet()) {
                channelId = r.GetChannelId();
            }

            // ✅ ADD THIS: Map channel to speaker name
            std::string speakerName = "";
            std::string speakerNumber = "";
            if (!channelId.empty()) {
                if (channelId == "ch_0") {
                    speakerName = caller_name;
                    speakerNumber = caller_number;
                } else if (channelId == "ch_1") {
                    speakerName = callee_name;
                    speakerNumber = callee_number;
                }
            }

            if (!isFinal && !r.GetIsPartial()) isFinal = true;

            // ✅ MODIFY THIS: Add speaker fields to JSON
            t1 << "{\"is_final\": " << (r.GetIsPartial() ? "false" : "true");

            if (!channelId.empty()) {
                t1 << ", \"channel_id\": \"" << channelId << "\"";
            }
            if (!speakerName.empty()) {
                t1 << ", \"speaker_name\": \"" << speakerName << "\"";
            }
            if (!speakerNumber.empty()) {
                t1 << ", \"speaker_number\": \"" << speakerNumber << "\"";
            }

            t1 << ", \"alternatives\": [";
            // ... rest of code
        }
    }
}
```

---

### 2. Deepgram Module

**File:** `modules/mod_deepgram_transcribe/dg_transcribe_glue.cpp`

**Location:** In the `lws_service_thread` function, around where transcription events are fired

**Changes:**

```cpp
// Find where Deepgram responses are parsed and events are fired
// Add similar logic to read channel variables and inject into JSON

// Get the channel from the session
switch_channel_t* channel = switch_core_session_get_channel(session);
const char* caller_name = switch_channel_get_variable(channel, "caller_name");
const char* callee_name = switch_channel_get_variable(channel, "callee_name");

// Parse the Deepgram JSON response
cJSON* json = cJSON_Parse(msg);
if (json) {
    // Get channel_index from Deepgram response
    cJSON* channelIndex = cJSON_GetObjectItem(json, "channel_index");
    if (channelIndex && cJSON_IsArray(channelIndex)) {
        int channel = cJSON_GetArrayItem(channelIndex, 0)->valueint;

        // Add speaker name based on channel
        const char* speakerName = (channel == 0) ? caller_name : callee_name;
        if (speakerName) {
            cJSON_AddStringToObject(json, "speaker_name", speakerName);
        }
    }

    // Convert back to string and send
    char* jsonStr = cJSON_PrintUnformatted(json);
    responseHandler(session, jsonStr, bugname);
    free(jsonStr);
    cJSON_Delete(json);
}
```

---

### 3. Azure Module

**File:** `modules/mod_azure_transcribe/azure_transcribe_glue.cpp`

**Location:** In the Azure event handler where transcriptions are processed

**Changes:**

```cpp
// In the Azure ConversationTranscriber event handler
// When receiving transcription results:

void OnRecognized(const ConversationTranscriptionEventArgs& e) override {
    // Get speaker names from channel
    switch_core_session_t* session = switch_core_session_locate(sessionId.c_str());
    if (session) {
        switch_channel_t* channel = switch_core_session_get_channel(session);
        const char* caller_name = switch_channel_get_variable(channel, "caller_name");
        const char* callee_name = switch_channel_get_variable(channel, "callee_name");

        // Build JSON with speaker info
        cJSON* json = cJSON_CreateObject();
        cJSON_AddStringToObject(json, "DisplayText", e.Result->Text.c_str());
        cJSON_AddNumberToObject(json, "Channel", e.Result->Channel);

        // ✅ ADD THIS: Map channel to speaker name
        const char* speakerName = (e.Result->Channel == 0) ? caller_name : callee_name;
        if (speakerName && strlen(speakerName) > 0) {
            cJSON_AddStringToObject(json, "speaker_name", speakerName);
        }

        char* jsonStr = cJSON_PrintUnformatted(json);
        responseHandler(session, jsonStr, bugname);
        free(jsonStr);
        cJSON_Delete(json);

        switch_core_session_rwunlock(session);
    }
}
```

---

## Result

### Before (application must map):

```javascript
conn.on('esl::event::aws_transcribe::transcription', (event) => {
  const callerName = event.getHeader('caller_name');  // Get from header
  const transcript = JSON.parse(event.getBody());
  const channelId = transcript[0].channel_id;

  // Application code must map channel → speaker
  const speaker = channelId === 'ch_0' ? callerName : calleeName;
  console.log(`${speaker}: ${transcript[0].alternatives[0].transcript}`);
});
```

### After (speaker name in JSON):

```javascript
conn.on('esl::event::aws_transcribe::transcription', (event) => {
  const transcript = JSON.parse(event.getBody());

  // Speaker name already in JSON!
  const speaker = transcript[0].speaker_name;
  console.log(`${speaker}: ${transcript[0].alternatives[0].transcript}`);
});
```

---

## Building Modified Modules

```bash
# 1. Apply patches
cd modules/mod_aws_transcribe
patch -p0 < aws_transcribe_glue.cpp.patch

# 2. Rebuild modules
cd ../..
make mod_aws_transcribe-install
make mod_deepgram_transcribe-install
make mod_azure_transcribe-install

# 3. Restart FreeSWITCH
systemctl restart freeswitch
# or
fs_cli -x 'reload mod_aws_transcribe'
```

---

## Pros and Cons

### ✅ Pros

1. **Simpler application code** - No mapping logic needed
2. **Speaker names in JSON** - Direct access without header lookup
3. **Consistent output** - All transcriptions include speaker info
4. **Backward compatible** - Existing apps still work (new fields are additions)

### ❌ Cons

1. **Requires C++ changes** - Must rebuild modules
2. **Channel variables must be set** - Dialplan must export caller_name/callee_name
3. **Maintenance** - Need to maintain patches across updates

---

## Alternative: Keep Current Approach

Instead of modifying C++, use the **unified handler** approach shown in `examples/unified-transcription-handler.js`:

**Pros:**
- ✅ No C++ changes needed
- ✅ No module rebuilding
- ✅ Easier to maintain
- ✅ More flexible (can change mapping logic)

**Cons:**
- ❌ Application must do mapping
- ❌ Requires reading event headers

---

## Recommendation

**For Production:** Use the **unified handler** approach (no C++ changes)

**For Development/Testing:** C++ enhancement can be useful if you want speaker names directly in JSON without any application-level mapping

---

## Testing

After applying C++ changes:

```bash
# Make a test call
# Check transcription output includes speaker_name
fs_cli -x "uuid_dump <uuid>"

# Transcription should now show:
{
  "is_final": true,
  "channel_id": "ch_0",
  "speaker_name": "John Doe",
  "speaker_number": "1002",
  "alternatives": [{
    "transcript": "Hello, how can I help you?"
  }]
}
```

---

## Files

- `aws_transcribe_glue.cpp.patch` - Patch for AWS module
- `examples/unified-transcription-handler.js` - No-C++-changes approach
- `docs/ACCESSING_SPEAKER_NAMES.md` - Application-level mapping guide
