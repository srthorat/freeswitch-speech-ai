# Speaker Detection & Channel Mapping: Technical Comparison

**Modules:** mod_aws_transcribe, mod_deepgram_transcribe

> **Key Principle:** Both modules use **channel-based mapping** only and **ignore API-provided speaker labels** for reliable, deterministic speaker identification.

---

## Table of Contents

- [Overview](#overview)
- [Channel Mapping Strategy](#channel-mapping-strategy)
- [Why Ignore API Speaker Labels](#why-ignore-api-speaker-labels)
- [JSON Response Analysis](#json-response-analysis)
- [Code Implementation](#code-implementation)
- [Pusher Integration](#pusher-integration)
- [Dialplan Configuration](#dialplan-configuration)
- [Testing & Verification](#testing--verification)

---

## Overview

Both `mod_aws_transcribe` and `mod_deepgram_transcribe` implement **identical speaker detection logic**:

✅ **Use:** Channel-based mapping (Channel 0 = Caller, Channel 1 = Callee)
❌ **Ignore:** API-provided speaker labels (unreliable AI-based diarization)

**Benefits:**
- 🎯 **Deterministic** - Always maps to correct speaker
- 🔄 **Consistent** - Works for both inbound and outbound calls
- 🚀 **Reliable** - No dependency on AI diarization accuracy
- 📞 **Telephony-optimized** - Leverages FreeSWITCH's channel architecture

---

## Channel Mapping Strategy

### How FreeSWITCH Assigns Channels (Stereo Mode)

```
┌─────────────────────────────────────────────────────────┐
│                    FreeSWITCH Call                      │
├─────────────────────────────────────────────────────────┤
│                                                         │
│  Channel 0 (Left/READ stream)   = Caller (A-leg)       │
│  Channel 1 (Right/WRITE stream) = Callee (B-leg)       │
│                                                         │
└─────────────────────────────────────────────────────────┘
```

**This mapping is ALWAYS consistent:**
- ✅ Inbound calls (external → FreeSWITCH)
- ✅ Outbound calls (FreeSWITCH → external)

**Why it works:**
- FreeSWITCH's media bug assigns:
  - **READ stream** = Audio coming INTO FreeSWITCH (caller's audio)
  - **WRITE stream** = Audio going OUT of FreeSWITCH (callee's audio)
- This is **independent of call direction**!

---

## Why Ignore API Speaker Labels

Both AWS and Deepgram APIs provide speaker labels through AI-based diarization:
- **AWS:** `items[].speaker_label` (e.g., "0", "1", "2")
- **Deepgram:** `words[].speaker` (e.g., 0, 1, 2)

### Why These Are Unreliable in Telephony

| Issue | Description | Impact |
|-------|-------------|--------|
| **AI Uncertainty** | Diarization is probabilistic, not deterministic | May assign wrong speaker ID |
| **Voice Similarity** | Similar voices confuse AI models | Incorrect speaker attribution |
| **Cross-talk** | Overlapping speech causes label switching | Inconsistent labeling |
| **Background Noise** | Noise affects speaker detection | False speaker changes |
| **Not Telephony-aware** | AI doesn't know about FreeSWITCH channels | Cannot guarantee caller/callee distinction |

### Why Channel Mapping Is Superior

| Advantage | Description |
|-----------|-------------|
| ✅ **Deterministic** | Channel 0 is ALWAYS caller, Channel 1 is ALWAYS callee |
| ✅ **Accurate** | No AI guessing - based on audio stream source |
| ✅ **Consistent** | Works identically for inbound/outbound calls |
| ✅ **Real-time** | No computational overhead for speaker detection |
| ✅ **Telephony-native** | Leverages FreeSWITCH's built-in channel architecture |

---

## JSON Response Analysis

### Deepgram JSON Response

**Actual API Response:**
```json
{
  "type": "Results",
  "channel_index": [1, 2],
  "duration": 1.2299995,
  "start": 41.72,
  "is_final": true,
  "speech_final": true,
  "channel": {
    "alternatives": [{
      "transcript": "just going on",
      "confidence": 0.96069336,
      "words": [
        {"word": "just", "start": 41.72, "end": 42.2, "confidence": 0.89404297, "speaker": 0},
        {"word": "going", "start": 42.2, "end": 42.52, "confidence": 0.99902344, "speaker": 0},
        {"word": "on", "start": 42.52, "end": 42.95, "confidence": 0.96069336, "speaker": 0}
      ]
    }]
  },
  "metadata": {
    "request_id": "2f82afc4-f847-4218-9069-56604796e464",
    "model_info": {"name": "phonecall-nova", "version": "2023-03-13.31000", "arch": "nova"},
    "model_uuid": "47cb79bc-c75a-43c9-9cd7-90959e2e2b8c"
  },
  "from_finalize": false
}
```

**What Our Code Uses:**
```json
{
  "channel_index": [1, 2],  // ✅ USED: Extract channel_index[0] = 1 → Callee
  "channel": {
    "alternatives": [{
      "transcript": "just going on",  // ✅ USED: Extract transcript text
      "words": [
        {"speaker": 0}  // ❌ IGNORED: AI-based speaker label
      ]
    }]
  }
}
```

**Extraction Logic:**
```c
// Get speaker channel from channel_index only (not from words[0].speaker)
cJSON* channel_index = cJSON_GetObjectItem(root, "channel_index");
if (channel_index && cJSON_IsArray(channel_index) && cJSON_GetArraySize(channel_index) > 0) {
    cJSON* channel_item = cJSON_GetArrayItem(channel_index, 0);
    if (channel_item && cJSON_IsNumber(channel_item)) {
        speaker_channel = (int)channel_item->valuedouble;  // speaker_channel = 1
    }
}
```

**Result:**
- `channel_index[0] = 1` → **Channel 1** → **Callee** → `"Jane Smith(1001)"`

---

### AWS Transcribe JSON Response

**Actual API Response:**
```json
[{
  "is_final": true,
  "channel_id": "ch_1",
  "result_id": "872fc51e-f315-4792-ac16-a3ed60adde69",
  "start_time": 12.127,
  "end_time": 13.487,
  "alternatives": [{
    "transcript": "I said, hey brother, how are you",
    "items": [
      {"content": "I", "type": "pronunciation", "start_time": 12.137, "end_time": 12.287, "confidence": 0.9966, "speaker_label": "0"},
      {"content": "said", "type": "pronunciation", "start_time": 12.287, "end_time": 12.327, "confidence": 0.9964, "speaker_label": "0"},
      {"content": ",", "type": "punctuation"},
      {"content": "hey", "type": "pronunciation", "start_time": 12.407, "end_time": 12.507, "confidence": 0.4634, "speaker_label": "0"},
      {"content": "brother", "type": "pronunciation", "start_time": 12.507, "end_time": 12.517, "confidence": 0.9843, "speaker_label": "0"},
      {"content": ",", "type": "punctuation"},
      {"content": "how", "type": "pronunciation", "start_time": 12.767, "end_time": 12.807, "confidence": 0.9955, "speaker_label": "0"},
      {"content": "are", "type": "pronunciation", "start_time": 12.807, "end_time": 12.817, "confidence": 0.9979, "speaker_label": "0"},
      {"content": "you", "type": "pronunciation", "start_time": 12.817, "end_time": 13.467, "confidence": 0.9983, "speaker_label": "1"}
    ]
  }]
}]
```

**What Our Code Uses:**
```json
[{
  "is_final": true,  // ✅ USED: Determine if final or interim
  "channel_id": "ch_1",  // ✅ USED: Extract "ch_1" → 1 → Callee
  "alternatives": [{
    "transcript": "I said, hey brother, how are you",  // ✅ USED: Extract transcript text
    "items": [
      {"speaker_label": "0"},  // ❌ IGNORED: AI-based speaker label
      {"speaker_label": "1"}   // ❌ IGNORED: AI-based speaker label
    ]
  }]
}]
```

**Extraction Logic:**
```c
// Get channel from channel_id (e.g., "ch_0", "ch_1")
cJSON* channel_id = cJSON_GetObjectItem(first_result, "channel_id");
if (channel_id && cJSON_IsString(channel_id)) {
    const char* channel_str = cJSON_GetStringValue(channel_id);
    if (channel_str && strstr(channel_str, "ch_")) {
        speaker_channel = atoi(channel_str + 3);  // Skip "ch_" prefix → 1
    }
}
```

**Result:**
- `channel_id = "ch_1"` → Skip "ch_" → `1` → **Channel 1** → **Callee** → `"Jane Smith(1001)"`

---

## Code Implementation

### mod_deepgram_transcribe (Lines 110-140)

```c
static void send_to_pusher(switch_core_session_t* session, const char* json,
                          const char* callId, switch_bool_t is_final) {
    // Get caller/callee metadata from channel variables for speaker mapping
    switch_channel_t *chan = switch_core_session_get_channel(session);
    const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
    const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
    const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
    const char* callee_number = switch_channel_get_variable(chan, "destination_number");

    // Parse transcription JSON to extract text and speaker/channel
    cJSON* root = cJSON_Parse(json);
    const char* transcript = NULL;
    int speaker_channel = -1;

    // Get speaker channel from channel_index only (not from words[0].speaker)
    cJSON* channel_index = cJSON_GetObjectItem(root, "channel_index");
    if (channel_index && cJSON_IsArray(channel_index) && cJSON_GetArraySize(channel_index) > 0) {
        cJSON* channel_item = cJSON_GetArrayItem(channel_index, 0);
        if (channel_item && cJSON_IsNumber(channel_item)) {
            speaker_channel = (int)channel_item->valuedouble;
        }
    }

    // Default to channel 0 if still not found
    if (speaker_channel == -1) speaker_channel = 0;

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

    // Build Pusher JSON
    cJSON* pusher_data = cJSON_CreateObject();
    cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
    cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id);
    cJSON_AddStringToObject(pusher_data, "text", transcript ? transcript : "");
    cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);

    // ... send to Pusher API
}
```

### mod_aws_transcribe (Lines 111-142)

```c
static void send_to_pusher(switch_core_session_t* session, const char* json,
                          const char* callId, switch_bool_t is_final) {
    // Get caller/callee metadata from channel variables for speaker mapping
    switch_channel_t *chan = switch_core_session_get_channel(session);
    const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
    const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
    const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
    const char* callee_number = switch_channel_get_variable(chan, "destination_number");

    // Parse transcription JSON to extract text and speaker/channel
    cJSON* root = cJSON_Parse(json);
    const char* transcript = NULL;
    int speaker_channel = -1;

    // AWS sends an array of results
    if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
        cJSON* first_result = cJSON_GetArrayItem(root, 0);

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

    // Build Pusher JSON
    cJSON* pusher_data = cJSON_CreateObject();
    cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
    cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id);
    cJSON_AddStringToObject(pusher_data, "text", transcript ? transcript : "");
    cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);

    // ... send to Pusher API
}
```

**Key Observations:**
- ✅ **Identical speaker mapping logic**
- ✅ **Both ignore API speaker labels**
- ✅ **Both use channel-based mapping only**
- ✅ **Same FreeSWITCH channel variables**

---

## Pusher Integration

Both modules have **FULL Pusher integration** (not limited to session start):

### Events Sent to Pusher

| Event | When Sent | JSON Format |
|-------|-----------|-------------|
| **session-start** | When transcription starts | `{"type":"session_start","caller_id":"...","callee_id":"...","timestamp":"..."}` |
| **transcription-interim** | Partial transcription results | `{"type":"interim","speaker_id":"John Doe(1000)","text":"Hello how","timestamp":"..."}` |
| **transcription-final** | Complete transcription results | `{"type":"final","speaker_id":"Jane Smith(1001)","text":"Hello, how are you?","timestamp":"..."}` |

### Pusher JSON Examples

**Interim Transcript (Channel 0 - Caller):**
```json
{
  "type": "interim",
  "speaker_id": "John Doe(1000)",
  "text": "Hello how are",
  "timestamp": "2025-01-22T10:30:45Z"
}
```

**Final Transcript (Channel 1 - Callee):**
```json
{
  "type": "final",
  "speaker_id": "Jane Smith(1001)",
  "text": "Hello, how are you doing today?",
  "timestamp": "2025-01-22T10:30:47Z"
}
```

**Session Start:**
```json
{
  "type": "session_start",
  "caller_id": "John Doe(1000)",
  "callee_id": "Jane Smith(1001)",
  "timestamp": "2025-01-22T10:30:40Z"
}
```

### Pusher Configuration

Set environment variables for Pusher credentials:

```bash
export PUSHER_APP_ID="your-app-id"
export PUSHER_KEY="your-key"
export PUSHER_SECRET="your-secret"
export PUSHER_CLUSTER="ap2"  # Default: ap2

# Optional customization
export PUSHER_CHANNEL_PREFIX="call-"  # Default: call-
export PUSHER_EVENT_INTERIM="transcription-interim"  # Default
export PUSHER_EVENT_FINAL="transcription-final"  # Default
export PUSHER_EVENT_SESSION_START="session-start"  # Default
```

**Pusher Channel Naming:**
- Format: `{PUSHER_CHANNEL_PREFIX}{sip_call_id}`
- Example: `call-abc123xyz789`

---

## Dialplan Configuration

### Complete Dialplan Example

```xml
<extension name="outbound_transcription">
  <condition field="destination_number" expression="^(\d+)$">

    <!-- ============================================ -->
    <!-- STEP 1: Set Speaker Information             -->
    <!-- ============================================ -->
    <!-- These variables are used for channel mapping -->

    <!-- Caller Information (Channel 0 - A-leg) -->
    <action application="set" data="caller_id_name=John Doe"/>
    <action application="set" data="caller_id_number=1000"/>

    <!-- Callee Information (Channel 1 - B-leg) -->
    <action application="set" data="callee_id_name=Jane Smith"/>
    <action application="set" data="destination_number=1001"/>

    <!-- Alternative: Get from user directory -->
    <action application="set" data="caller_id_name=${user_data(${caller_id_number}@${domain_name} var effective_caller_id_name)}"/>
    <action application="set" data="callee_id_name=${user_data($1@${domain_name} var effective_caller_id_name)}"/>

    <!-- ============================================ -->
    <!-- STEP 2: Enable Channel Identification       -->
    <!-- ============================================ -->

    <!-- AWS Transcribe Settings -->
    <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
    <!-- This tells AWS to send separate transcriptions for each channel -->

    <!-- Deepgram Settings -->
    <action application="set" data="DEEPGRAM_SPEECH_MULTICHANNEL=true"/>
    <action application="set" data="DEEPGRAM_SPEECH_CHANNELS=2"/>
    <!-- This tells Deepgram to process stereo audio with 2 channels -->

    <!-- ============================================ -->
    <!-- STEP 3: Start Transcription on Answer       -->
    <!-- ============================================ -->

    <!-- Option A: AWS Transcribe -->
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo 16k"/>
    <action application="set" data="api_hangup_hook=uuid_aws_transcribe ${uuid} stop"/>

    <!-- Option B: Deepgram (comment out AWS above, uncomment this) -->
    <!-- <action application="set" data="DEEPGRAM_API_KEY=your-api-key"/> -->
    <!-- <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo 16k"/> -->
    <!-- <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/> -->

    <!-- ============================================ -->
    <!-- STEP 4: Bridge the Call                     -->
    <!-- ============================================ -->
    <action application="bridge" data="sofia/gateway/provider/$1"/>

  </condition>
</extension>
```

### Per-User Configuration

**User Directory File:** `/usr/local/freeswitch/conf/directory/default/1000.xml`

```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <!-- Speaker information for channel mapping -->
      <variable name="effective_caller_id_name" value="John Doe"/>
      <variable name="effective_caller_id_number" value="1000"/>

      <!-- Enable transcription for this user -->
      <variable name="enable_aws_transcribe" value="true"/>
      <!-- OR -->
      <!-- <variable name="enable_deepgram" value="true"/> -->
    </variables>
  </user>
</include>
```

**Conditional Dialplan (checks user flag):**

```xml
<extension name="transcription_conditional" continue="true">
  <condition field="${user_data(${caller_id_number}@${domain_name} var enable_aws_transcribe)}" expression="^true$">
    <condition field="destination_number" expression="^(.+)$">

      <!-- Get speaker names from user directory -->
      <action application="set" data="caller_id_name=${user_data(${caller_id_number}@${domain_name} var effective_caller_id_name)}"/>
      <action application="set" data="callee_id_name=${user_data($1@${domain_name} var effective_caller_id_name)}"/>

      <!-- Enable AWS channel identification -->
      <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>

      <!-- Start on answer -->
      <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo 16k"/>
      <action application="set" data="api_hangup_hook=uuid_aws_transcribe ${uuid} stop"/>

    </condition>
  </condition>
</extension>
```

---

## Testing & Verification

### 1. Verify Channel Variables

```bash
# In fs_cli, during a call
freeswitch@localhost> uuid_dump <uuid> | grep caller_id_name
freeswitch@localhost> uuid_dump <uuid> | grep callee_id_name
freeswitch@localhost> uuid_dump <uuid> | grep destination_number
```

**Expected Output:**
```
variable_caller_id_name: John Doe
variable_caller_id_number: 1000
variable_callee_id_name: Jane Smith
variable_destination_number: 1001
```

### 2. Verify Transcription Events

Enable event socket and listen for transcription events:

```bash
# In fs_cli
freeswitch@localhost> /event CUSTOM transcribe::transcription

# Make a test call, you'll see events like:
Event-Subclass: transcribe::transcription
transcription-vendor: aws
Body: [{"is_final":true,"channel_id":"ch_0",...}]
```

### 3. Verify Pusher Events

Check Pusher dashboard for events on channel `call-<sip_call_id>`:

**Events should appear as:**
```
Channel: call-abc123xyz789
Event: transcription-interim
Data: {"type":"interim","speaker_id":"John Doe(1000)","text":"Hello","timestamp":"..."}

Channel: call-abc123xyz789
Event: transcription-final
Data: {"type":"final","speaker_id":"Jane Smith(1001)","text":"Hello, how are you?","timestamp":"..."}
```

### 4. Test Inbound vs Outbound

**Inbound Call (external → FreeSWITCH):**
- Channel 0 (READ) = External caller
- Channel 1 (WRITE) = FreeSWITCH user

**Outbound Call (FreeSWITCH → external):**
- Channel 0 (READ) = FreeSWITCH user
- Channel 1 (WRITE) = External callee

**Expected Behavior:**
Both scenarios should correctly map speakers because the code uses channel variables, not call direction.

### 5. Debug Logging

Enable debug logging to see speaker mapping:

```bash
# In fs_cli
freeswitch@localhost> console loglevel debug

# Or in dialplan
<action application="set" data="log_level=DEBUG"/>
```

**Look for log lines like:**
```
[DEBUG] mod_aws_transcribe.c:132 Mapped channel 0 to speaker: John Doe(1000)
[DEBUG] mod_aws_transcribe.c:138 Mapped channel 1 to speaker: Jane Smith(1001)
```

---

## Summary Table

| Aspect | mod_aws_transcribe | mod_deepgram_transcribe |
|--------|-------------------|------------------------|
| **Channel Extraction** | `channel_id` field (e.g., "ch_1") | `channel_index` array (e.g., [1]) |
| **Speaker Label Handling** | ❌ **Ignores** `items[].speaker_label` | ❌ **Ignores** `words[].speaker` |
| **Mapping Logic** | ✅ Channel 0→caller, 1→callee | ✅ Channel 0→caller, 1→callee |
| **Channel Variables Used** | ✅ caller_id_name, caller_id_number, callee_id_name, destination_number | ✅ caller_id_name, caller_id_number, callee_id_name, destination_number |
| **Pusher Integration** | ✅ Full (session + interim + final) | ✅ Full (session + interim + final) |
| **Pusher JSON Format** | ✅ Identical | ✅ Identical |
| **Inbound Call Support** | ✅ Yes | ✅ Yes |
| **Outbound Call Support** | ✅ Yes | ✅ Yes |
| **Implementation** | ✅ **Identical to Deepgram** | ✅ **Identical to AWS** |

---

## Best Practices

1. ✅ **Always set speaker information** in dialplan or user directory
2. ✅ **Use stereo mode** for accurate channel separation
3. ✅ **Enable channel identification** (AWS) or multichannel (Deepgram)
4. ✅ **Verify channel variables** are set before transcription starts
5. ✅ **Test both inbound and outbound** call scenarios
6. ✅ **Monitor Pusher events** to confirm correct speaker attribution
7. ❌ **Never rely on API speaker labels** - always use channel mapping

---

## References

- [Audio Module Technical Reference](./AUDIO_MODULE_TECHNICAL_REFERENCE.md)
- [Stereo Channel Assignment Guide](./STEREO_CHANNEL_ASSIGNMENT.md)
- [Real-Time Transcription Delivery](./REALTIME_TRANSCRIPTION_DELIVERY.md)
- [mod_aws_transcribe README](../modules/mod_aws_transcribe/README.md)
- [mod_deepgram_transcribe README](../modules/mod_deepgram_transcribe/README.md)
