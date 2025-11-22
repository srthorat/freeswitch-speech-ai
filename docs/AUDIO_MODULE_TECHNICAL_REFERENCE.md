# Audio Module Technical Reference Guide
## Complete Technical Comparison: mod_audio_fork, mod_deepgram_transcribe, mod_aws_transcribe

**Document Version:** 1.0
**Last Updated:** 2025-11-22
**Branch:** `featurefInal-mod-audio-fork-deepgram-aws`

---

## Table of Contents

1. [Module Overview](#module-overview)
2. [Mix-Type Support](#mix-type-support)
3. [Sampling Rate Support](#sampling-rate-support)
4. [Metadata Support](#metadata-support)
5. [Audio Processing Pipeline](#audio-processing-pipeline)
6. [Buffer Implementation](#buffer-implementation)
7. [Speaker Mapping](#speaker-mapping)
8. [Pusher Integration](#pusher-integration)
9. [Complete Examples](#complete-examples)
10. [API Reference](#api-reference)

---

## Module Overview

### mod_audio_fork
- **Purpose:** Generic WebSocket audio streaming
- **Protocol:** WebSocket (libwebsockets)
- **Use Case:** Stream audio to any custom backend
- **Authentication:** Basic Auth (optional)

### mod_deepgram_transcribe
- **Purpose:** Deepgram speech-to-text transcription
- **Protocol:** WebSocket to Deepgram API
- **Use Case:** Real-time transcription with Pusher integration
- **Authentication:** Token-based (API key)

### mod_aws_transcribe
- **Purpose:** AWS Transcribe speech-to-text
- **Protocol:** AWS SDK (HTTP/2 event stream)
- **Use Case:** Real-time transcription with Pusher integration
- **Authentication:** AWS credentials (IAM, access keys, STS)

---

## Mix-Type Support

All three modules support **identical** mix-type configurations:

### Supported Mix Types

| Mix Type | Channels | Audio Content | Use Case | Flag Configuration |
|----------|----------|---------------|----------|-------------------|
| **mono** | 1 | Caller only (A-leg/inbound) | Single speaker recording | `SMBF_READ_STREAM` |
| **mixed** | 1 | Caller + Callee mixed together | Combined audio, single track | `SMBF_READ_STREAM \| SMBF_WRITE_STREAM` |
| **stereo** | 2 | Ch0=Caller, Ch1=Callee | Speaker separation, call centers | `SMBF_READ_STREAM \| SMBF_WRITE_STREAM \| SMBF_STEREO` |

### Channel Assignment in Stereo Mode

```
Channel 0 (LEFT)  = Caller  (A-leg / Inbound / Read stream)
Channel 1 (RIGHT) = Callee  (B-leg / Outbound / Write stream)
```

### Implementation Details

#### mod_audio_fork
```c
// mod_audio_fork.c:244-255
if (0 == strcmp(argv[3], "mixed")) {
  flags |= SMBF_WRITE_STREAM;
}
else if (0 == strcmp(argv[3], "stereo")) {
  flags |= SMBF_WRITE_STREAM;
  flags |= SMBF_STEREO;
}
else if(0 != strcmp(argv[3], "mono")) {
  // Error: invalid mix type
}
```

**API Syntax:**
```
uuid_audio_fork <uuid> start <ws-url> [mono|mixed|stereo] [8k|16k|...] [bugname] [metadata]
```

**Example:**
```javascript
await ep.api('uuid_audio_fork', `${ep.uuid} start wss://backend.example.com stereo 16k`);
```

#### mod_deepgram_transcribe
```c
// mod_deepgram_transcribe.c:634-642
if (argc > 4) {
  if (!strcmp(argv[4], "mixed")) {
    flags |= SMBF_WRITE_STREAM;  // Mixed: READ + WRITE (single channel)
  } else if (!strcmp(argv[4], "stereo")) {
    flags |= SMBF_WRITE_STREAM;  // Stereo: READ + WRITE + STEREO
    flags |= SMBF_STEREO;
  }
  // else: mono is default (SMBF_READ_STREAM only)
}
```

**API Syntax:**
```
uuid_deepgram_transcribe <uuid> start <lang> [interim] [mono|mixed|stereo] [8k|16k] [metadata]
```

**Example:**
```javascript
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo 16k`);
```

#### mod_aws_transcribe
```c
// mod_aws_transcribe.c:657-666
if (argc > 4) {
  if (!strcmp(argv[4], "mixed")) {
    flags |= SMBF_WRITE_STREAM;  // Mixed: READ + WRITE (single channel)
  } else if (!strcmp(argv[4], "stereo")) {
    flags |= SMBF_WRITE_STREAM;  // Stereo: READ + WRITE + STEREO
    flags |= SMBF_STEREO;
  }
  // else: mono is default (SMBF_READ_STREAM only)
}
```

**API Syntax:**
```
uuid_aws_transcribe <uuid> start <lang> [interim] [mono|mixed|stereo] [8k|16k] [metadata]
```

**Example:**
```javascript
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo 16k`);
```

### Configuration Requirements for Stereo

**For Deepgram:**
```javascript
// Stereo mode automatically adds to API request:
// multichannel=true&channels=2
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**For AWS:**
```javascript
// Must enable channel identification for stereo separation
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

---

## Sampling Rate Support

### Default Sampling Rates

| Module | Default Rate | Supported Rates | Validation | Code Location |
|--------|--------------|----------------|------------|---------------|
| **mod_audio_fork** | **8000 Hz** | 8k, 16k, 24k, 32k, 48k, 64k (any multiple of 8000) | `sampling % 8000 == 0` | mod_audio_fork.c:233, 268 |
| **mod_deepgram_transcribe** | **16000 Hz** | 8k, 16k (or any numeric multiple of 8000) | `rate % 8000 == 0` | mod_deepgram_transcribe.c:631 |
| **mod_aws_transcribe** | **16000 Hz** | 8k, 16k (AWS API limitation) | `rate % 8000 == 0` | mod_aws_transcribe.c:654 |

### Detailed Implementation

#### mod_audio_fork
```c
// mod_audio_fork.c:233
int sampling = 8000;  // DEFAULT: 8kHz

// mod_audio_fork.c:256-264
if (0 == strcmp(argv[4], "16k")) {
  sampling = 16000;
}
else if (0 == strcmp(argv[4], "8k")) {
  sampling = 8000;
}
else {
  sampling = atoi(argv[4]);  // Accept any numeric value
}

// mod_audio_fork.c:268 - Validation
if (sampling % 8000 != 0) {
  // ERROR: "invalid sample rate"
}
```

**Supported rates:** 8000, 16000, 24000, 32000, 48000, 64000 Hz
**Input format:** `"8k"`, `"16k"`, or numeric (e.g., `24000`)

#### mod_deepgram_transcribe
```c
// mod_deepgram_transcribe.c:631
int sampling = 16000;  // DEFAULT: 16kHz

// mod_deepgram_transcribe.c:645-657
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
```

**Supported rates:** Primarily 8000, 16000 Hz (accepts other multiples of 8000)
**Recommended:** 16kHz for better transcription quality
**Input format:** `"8k"`, `"16k"`, or numeric

#### mod_aws_transcribe
```c
// mod_aws_transcribe.c:654
int sampling = 16000;  // DEFAULT: 16kHz

// mod_aws_transcribe.c:668-680
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

// aws_transcribe_glue.cpp:147
m_request.SetMediaSampleRateHertz(samples_per_second);
```

**Supported rates:** 8000, 16000 Hz (AWS Transcribe API limitation)
**Recommended:** 16kHz (AWS best practice)
**Input format:** `"8k"`, `"16k"`, or numeric

### Codec Sample Rate Handling

All modules handle codec sample rates identically:

```c
// Determine actual samples per second from codec
samples_per_second = !strcasecmp(read_impl.iananame, "g722") ?
  read_impl.actual_samples_per_second :  // G722: use actual (16000)
  read_impl.samples_per_second;          // Others: use normal
```

**Codec Sample Rates:**
| Codec | Sample Rate | Special Handling |
|-------|-------------|------------------|
| G.711 (PCMU/PCMA) | 8000 Hz | Normal |
| G.722 | 16000 Hz | Uses `actual_samples_per_second` |
| Opus | 48000 Hz | Normal |
| G.729 | 8000 Hz | Normal |

### Resampling Logic

**When resampling occurs:**
```c
if (requested_sampling_rate != codec_actual_samples_per_second) {
  // Initialize Speex resampler
  tech_pvt->resampler = speex_resampler_init(
    channels,
    codec_sample_rate,
    requested_sample_rate,
    SWITCH_RESAMPLE_QUALITY,
    &err
  );
}
```

**When NO resampling occurs (optimal):**
- G.711 codec (8kHz) + requesting 8kHz = **no resampling**
- G.722 codec (16kHz) + requesting 16kHz = **no resampling**

**Examples:**

| Codec | Request | Resampling | Direction |
|-------|---------|------------|-----------|
| G.711 (8kHz) | 8kHz | ❌ No | - |
| G.711 (8kHz) | 16kHz | ✅ Yes | Upsampling |
| G.722 (16kHz) | 8kHz | ✅ Yes | Downsampling |
| G.722 (16kHz) | 16kHz | ❌ No | - |
| Opus (48kHz) | 16kHz | ✅ Yes | Downsampling |

---

## Metadata Support

### Metadata Comparison

| Module | Format | When Sent | Enrichment | Used For |
|--------|--------|-----------|------------|----------|
| **mod_audio_fork** | JSON object/array | Immediately after connection | ❌ None (sent as-is) | Backend server |
| **mod_deepgram_transcribe** | JSON object/array | ❌ NOT sent to Deepgram | ✅ Yes (caller/callee info) | FreeSWITCH events only |
| **mod_aws_transcribe** | JSON object/array | ❌ NOT sent to AWS | ✅ Yes (caller/callee info) | FreeSWITCH events only |

### mod_audio_fork - Raw Metadata

**Behavior:** Metadata is sent to the WebSocket server as a text frame immediately after connection.

**Code:**
```cpp
// lws_glue.cpp:171-174
if (strlen(tech_pvt->initialMetadata) > 0) {
  AudioPipe *pAudioPipe = static_cast<AudioPipe *>(tech_pvt->pAudioPipe);
  pAudioPipe->bufferForSending(tech_pvt->initialMetadata);  // Sends as text frame
}
```

**Example:**
```javascript
await ep.api('uuid_audio_fork',
  `${ep.uuid} start wss://backend.example.com stereo 16k my-bug-name {"userId":"12345","callType":"support"}`
);
```

The backend WebSocket server receives:
```json
{
  "userId": "12345",
  "callType": "support"
}
```

### mod_deepgram_transcribe - Enriched Metadata (Local Only)

**Behavior:** Metadata is enriched with caller/callee information but **NOT sent to Deepgram**. Used only for FreeSWITCH events.

**Enrichment Code:**
```c
// mod_deepgram_transcribe.c:463-527
static char* build_session_metadata(switch_core_session_t *session,
                                     switch_memory_pool_t *pool,
                                     char *user_metadata) {
  cJSON *jMetadata = cJSON_CreateObject();

  // Add caller information (Channel 0 / A-leg)
  cJSON_AddStringToObject(jMetadata, "callerName", caller_name);
  cJSON_AddStringToObject(jMetadata, "callerNumber", caller_number);

  // Add callee information (Channel 1 / B-leg)
  cJSON_AddStringToObject(jMetadata, "calleeName", callee_name);
  cJSON_AddStringToObject(jMetadata, "calleeNumber", callee_number);

  // Add SIP Call-ID
  cJSON_AddStringToObject(jMetadata, "call-Id", sip_call_id);

  return metadata_str;
}
```

**Enriched Metadata Structure:**
```json
{
  "callerName": "Extension 1000",
  "callerNumber": "1000",
  "calleeName": "Extension 1001",
  "calleeNumber": "1001",
  "call-Id": "3848276298220188511@atlanta.example.com",
  // ...user-provided metadata fields merged here
}
```

**Important:** This metadata is **NOT sent to Deepgram API**. It's only included in FreeSWITCH events for local tracking.

### mod_aws_transcribe - Enriched Metadata (Local Only)

**Behavior:** Identical to Deepgram - enriched but NOT sent to AWS.

**Code:** Same as Deepgram (`mod_aws_transcribe.c:486-550`)

**Enriched Metadata Structure:** Same as Deepgram

**Important:** This metadata is **NOT sent to AWS Transcribe API**. It's only included in FreeSWITCH events for local tracking.

---

## Audio Processing Pipeline

### mod_audio_fork

```
┌──────────────────────┐
│ FreeSWITCH Media Bug │
│ (capture_callback)   │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ fork_frame()         │
│ - Read from bug      │
│ - Direct buffer copy │
└──────────┬───────────┘
           │
           ↓ (if needed)
┌──────────────────────┐
│ Speex Resampler      │
│ - Convert rate       │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ AudioPipe Buffer     │
│ - 2-5 sec circular   │
│ - Overrun detection  │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ WebSocket Binary     │
│ - libwebsockets      │
│ - Binary frames      │
└──────────────────────┘
```

**Key Features:**
- Direct buffer writing (lws_glue.cpp:547-575)
- Buffer overrun detection with event notification
- Bi-directional (can receive JSON and play audio)

### mod_deepgram_transcribe

```
┌──────────────────────┐
│ FreeSWITCH Media Bug │
│ (capture_callback)   │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ dg_transcribe_frame()│
│ - Read from bug      │
└──────────┬───────────┘
           │
           ↓ (if needed)
┌──────────────────────┐
│ Speex Resampler      │
│ - Convert rate       │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ AudioPipe Buffer     │
│ - Pre-connect deque  │
│ - 2-5 sec buffer     │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ WebSocket Binary     │
│ wss://api.deepgram   │
│ - Token auth         │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ JSON Response        │
│ - Transcription      │
│ - channel_index      │
└──────────────────────┘
```

**Key Features:**
- Pre-connection buffering with deque
- Token-based authentication
- Rich API query parameters (diarization, punctuation, smart_format)
- Receives JSON transcription results

### mod_aws_transcribe

```
┌──────────────────────┐
│ FreeSWITCH Media Bug │
│ (capture_callback)   │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ aws_transcribe_frame │
│ - Read from bug      │
└──────────┬───────────┘
           │
           ↓ (if needed)
┌──────────────────────┐
│ Speex Resampler      │
│ - Convert rate       │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ Pre-Connect Buffer   │
│ - 32KB deque (1 sec) │
│ - Circular (drop old)│
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ AWS SDK Streaming    │
│ - HTTP/2 event stream│
│ - Signature V4 auth  │
└──────────┬───────────┘
           │
           ↓
┌──────────────────────┐
│ TranscriptEvent      │
│ - Transcription      │
│ - channel_id         │
└──────────────────────┘
```

**Key Features:**
- Pre-connection circular buffer (drops oldest when full)
- Accepts ANY size audio chunks (no chunking needed)
- AWS SDK C++ (not WebSocket)
- Flexible authentication (IAM, access keys, STS)

---

## Buffer Implementation

### mod_audio_fork

**Buffer Configuration:**
```cpp
// lws_glue.cpp:25-26
static int nAudioBufferSecs = std::max(1, std::min(
    requestedBufferSecs ? ::atoi(requestedBufferSecs) : 2, 5));
```
- **Default:** 2 seconds
- **Range:** 1-5 seconds
- **Environment Variable:** `MOD_AUDIO_FORK_BUFFER_SECS`

**Buffer Calculation:**
```cpp
// lws_glue.cpp:240
size_t buflen = LWS_PRE +
    (FRAME_SIZE_8000 * desiredSampling / 8000 * channels * 1000 /
     RTP_PACKETIZATION_PERIOD * nAudioBufferSecs);
```

**Example (16kHz stereo, 2 seconds):**
```
buflen = LWS_PRE + (320 * 2 * 2 * 50 * 2)
       = LWS_PRE + 128,000 bytes
```

**Buffer Type:** Circular buffer with write pointer

**Overrun Behavior:**
```cpp
// lws_glue.cpp:554-565
if (available < pAudioPipe->binaryMinSpace()) {
  if (!tech_pvt->buffer_overrun_notified) {
    tech_pvt->buffer_overrun_notified = 1;
    tech_pvt->responseHandler(session, EVENT_BUFFER_OVERRUN, NULL);
  }
  pAudioPipe->binaryWritePtrResetToZero();  // Drop old data
}
```

### mod_deepgram_transcribe

**Buffer Configuration:** Same as mod_audio_fork
- **Default:** 2 seconds
- **Range:** 1-5 seconds
- **Environment Variable:** `MOD_AUDIO_FORK_BUFFER_SECS`

**Buffer Type:** AudioPipe (shared implementation with mod_audio_fork)

### mod_aws_transcribe

**Pre-Connection Buffer:**
```cpp
// aws_transcribe_glue.cpp:25-29
#define PRE_CONNECT_BUFFER_SIZE (32000)  // 1 second at 16kHz
```

**Buffer Type:**
```cpp
std::deque<Aws::Vector<unsigned char>> m_deqPreConnectAudio;
```

**Pre-Connection Logic:**
```cpp
// aws_transcribe_glue.cpp:276-294
if (!m_connected) {
  if (m_preConnectAudioSize + datalen <= PRE_CONNECT_BUFFER_SIZE) {
    m_deqPreConnectAudio.push_back(bits);
    m_preConnectAudioSize += datalen;
  } else {
    // Buffer full - drop oldest (circular behavior)
    if (!m_deqPreConnectAudio.empty()) {
      m_preConnectAudioSize -= m_deqPreConnectAudio.front().size();
      m_deqPreConnectAudio.pop_front();
    }
    m_deqPreConnectAudio.push_back(bits);
    m_preConnectAudioSize += datalen;
  }
}
```

**Post-Connection:**
```cpp
// aws_transcribe_glue.cpp:297-300
// Post-connection: Send immediately
m_deqAudio.push_back(bits);
m_packets++;
m_cond.notify_one();
```

**Characteristics:**
- **Pre-connection:** 32KB (1 second at 16kHz)
- **Post-connection:** Immediate sending (no size limit, relies on AWS SDK flow control)
- **Behavior:** Circular - drops oldest when pre-connection buffer full

---

## Speaker Mapping

### Important Distinction

**Native API Speaker Features vs Pusher `speaker_id`:**

| Feature | AWS Transcribe | Deepgram | Used in Pusher? |
|---------|---------------|----------|----------------|
| **Native Speaker Diarization** | `AWS_SHOW_SPEAKER_LABEL` (spk_0, spk_1) | `DEEPGRAM_SPEECH_DIARIZE=true` (speaker 0, 1) | ❌ **NO** |
| **Channel Identification** | `AWS_ENABLE_CHANNEL_IDENTIFICATION` (ch_0, ch_1) | `multichannel=true` (channel_index [0, 1]) | ✅ **YES** |
| **Pusher `speaker_id`** | Mapped from `channel_id` | Mapped from `channel_index` | ✅ **YES** |

**Key Point:** For Pusher integration, we use **channel-based mapping**, NOT native speaker labels.

### Deepgram Speaker Mapping

**API Configuration:**
```javascript
// For Pusher speaker mapping - Use MULTICHANNEL (automatic in stereo mode)
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo`);

// This automatically adds: multichannel=true&channels=2
```

**Source Data from Deepgram:**
```json
{
  "channel_index": [0],
  "channel": {
    "alternatives": [{
      "transcript": "Hello, how are you?"
    }]
  }
}
```

**Parsing Logic:**
```c
// mod_deepgram_transcribe.c:111-120
cJSON* channel_index = cJSON_GetObjectItem(root, "channel_index");
if (channel_index && cJSON_IsArray(channel_index)) {
  cJSON* channel_item = cJSON_GetArrayItem(channel_index, 0);
  if (channel_item && cJSON_IsNumber(channel_item)) {
    speaker_channel = (int)channel_item->valuedouble;
  }
}
```

**Channel Mapping:**
- `channel_index: [0]` → Channel 0 → Caller
- `channel_index: [1]` → Channel 1 → Callee

### AWS Speaker Mapping

**API Configuration:**
```javascript
// For Pusher speaker mapping - Use CHANNEL identification
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**Source Data from AWS:**
```json
[{
  "channel_id": "ch_0",
  "alternatives": [{
    "transcript": "Hello, how are you?"
  }]
}]
```

**Parsing Logic:**
```c
// mod_aws_transcribe.c:112-118
cJSON* channel_id = cJSON_GetObjectItem(first_result, "channel_id");
if (channel_id && cJSON_IsString(channel_id)) {
  const char* channel_str = cJSON_GetStringValue(channel_id);
  if (channel_str && strstr(channel_str, "ch_")) {
    speaker_channel = atoi(channel_str + 3); // Skip "ch_" prefix
  }
}
```

**Channel Mapping:**
- `"channel_id": "ch_0"` → Channel 0 → Caller
- `"channel_id": "ch_1"` → Channel 1 → Callee

### Speaker ID Construction (Both Modules)

**Code (Identical):**
```c
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
```

**Result Examples:**
- `"Extension 1000(1000)"` - Caller
- `"John Doe(+15551234567)"` - Caller with full name
- `"Support Team(1001)"` - Callee
- `"Unknown(Unknown)"` - When info not available

**Channel Variable Sources:**
```c
// Caller info
caller_name   = switch_channel_get_variable(chan, "caller_id_name");
caller_number = switch_channel_get_variable(chan, "caller_id_number");

// Callee info
callee_name   = switch_channel_get_variable(chan, "callee_id_name")
                || switch_channel_get_variable(chan, "effective_callee_id_name");
callee_number = switch_channel_get_variable(chan, "destination_number")
                || switch_channel_get_variable(chan, "callee_id_number");
```

---

## Pusher Integration

### Pusher Data Format

Both **mod_aws_transcribe** and **mod_deepgram_transcribe** send **IDENTICAL** data formats to Pusher.

### HTTP Request Format

```http
POST https://api-{cluster}.pusher.com/apps/{app_id}/events?auth_key={key}&auth_timestamp={ts}&auth_version=1.0&body_md5={md5}&auth_signature={signature}
Content-Type: application/json

{
  "name": "{event_name}",
  "channels": ["{channel_name}"],
  "data": "{escaped_json_string}"
}
```

### Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `PUSHER_APP_ID` | *(required)* | Pusher application ID |
| `PUSHER_KEY` | *(required)* | Pusher application key |
| `PUSHER_SECRET` | *(required)* | Pusher application secret |
| `PUSHER_CLUSTER` | `"ap2"` | Pusher cluster (e.g., `us2`, `eu`, `ap2`) |
| `PUSHER_CHANNEL_PREFIX` | `"call-"` | Prefix for channel names |
| `PUSHER_EVENT_FINAL` | `"transcription-final"` | Final transcription event name |
| `PUSHER_EVENT_INTERIM` | `"transcription-interim"` | Interim transcription event name |
| `PUSHER_EVENT_SESSION_START` | `"session-start"` | Session start event name |

### Channel Naming

**Default Format:**
```
call-{sip_call_id}
```

**Example:**
```
call-3848276298220188511@atlanta.example.com
```

**Code:**
```c
// mod_deepgram_transcribe.c:70, 73, 79
const char* channel_prefix = getenv("PUSHER_CHANNEL_PREFIX");
if (!channel_prefix) channel_prefix = "call-";

const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
snprintf(channel, sizeof(channel), "%s%s", channel_prefix, sip_call_id);
```

### Event Types

#### 1. Session Start Event

**Event Name:** `session-start` (default)

**Data Structure:**
```json
{
  "type": "session_start",
  "caller_id": "Extension 1000(1000)",
  "callee_id": "Extension 1001(1001)",
  "timestamp": "2025-11-22T10:30:00Z"
}
```

**When Sent:** Immediately after WebSocket connection established (Deepgram) or AWS stream ready (AWS)

**Code:**
```c
// mod_deepgram_transcribe.c:363-367
const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
if (sip_call_id && 0 == strcmp(eventName, TRANSCRIBE_EVENT_CONNECT_SUCCESS)) {
  send_session_start_to_pusher(session, sip_call_id);
}
```

#### 2. Transcription Events (Interim & Final)

**Interim Event Name:** `transcription-interim` (default)
**Final Event Name:** `transcription-final` (default)

**Data Structure:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, how are",
  "timestamp": "2025-11-22T10:30:05Z"
}
```

```json
{
  "type": "final",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, how are you?",
  "timestamp": "2025-11-22T10:30:07Z"
}
```

**Detection Logic:**

**Deepgram:**
```c
// mod_deepgram_transcribe.c:375-382
cJSON* is_final_field = cJSON_GetObjectItem(root, "is_final");
if (is_final_field && cJSON_IsBool(is_final_field)) {
  is_final = cJSON_IsTrue(is_final_field) ? SWITCH_TRUE : SWITCH_FALSE;
}
cJSON* speech_final_field = cJSON_GetObjectItem(root, "speech_final");
if (speech_final_field && cJSON_IsBool(speech_final_field)) {
  is_final = cJSON_IsTrue(speech_final_field) ? SWITCH_TRUE : SWITCH_FALSE;
}
```

**AWS:**
```c
// mod_aws_transcribe.c:398-401
cJSON* is_final_field = cJSON_GetObjectItem(first_result, "is_final");
if (is_final_field && cJSON_IsBool(is_final_field)) {
  is_final = cJSON_IsTrue(is_final_field) ? SWITCH_TRUE : SWITCH_FALSE;
}
```

**Empty Transcript Handling (Both Modules):**
```c
// Don't send to Pusher if transcript is empty
if (!transcript || strlen(transcript) == 0) {
  cJSON_Delete(root);
  return;
}
```

### Authentication

**HMAC SHA256 Signature:**
```c
// mod_deepgram_transcribe.c:41-47
static void hmac_sha256_hex(const char* key, const char* data, char* out) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key, strlen(key), (unsigned char*)data, strlen(data), digest, &len);
  bin_to_hex(digest, len, out);
}
```

**MD5 Body Checksum:**
```c
// mod_deepgram_transcribe.c:49-54
static void md5_hex(const char* data, char* out) {
  unsigned char digest[MD5_DIGEST_LENGTH];
  MD5((unsigned char*)data, strlen(data), digest);
  bin_to_hex(digest, MD5_DIGEST_LENGTH, out);
}
```

**String to Sign:**
```c
// mod_deepgram_transcribe.c:201-204
snprintf(to_sign, sizeof(to_sign),
  "POST\n/apps/%s/events\n%s",
  app_id, query);
```

---

## Complete Examples

### Example 1: Session Start

**Channel:** `call-3848276298220188511@atlanta.example.com`
**Event:** `session-start`

**Pusher Data:**
```json
{
  "type": "session_start",
  "caller_id": "Extension 1000(1000)",
  "callee_id": "Extension 1001(1001)",
  "timestamp": "2025-11-22T10:30:00Z"
}
```

### Example 2: Channel 0 (Caller) - Interim Transcriptions

**Channel:** `call-3848276298220188511@atlanta.example.com`
**Event:** `transcription-interim`

**Interim #1:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello",
  "timestamp": "2025-11-22T10:30:01Z"
}
```

**Interim #2:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, how",
  "timestamp": "2025-11-22T10:30:02Z"
}
```

**Interim #3:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, how are",
  "timestamp": "2025-11-22T10:30:03Z"
}
```

### Example 3: Channel 0 (Caller) - Final Transcription

**Event:** `transcription-final`

```json
{
  "type": "final",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, how are you?",
  "timestamp": "2025-11-22T10:30:04Z"
}
```

### Example 4: Channel 1 (Callee) - Interim & Final

**Event:** `transcription-interim`

**Interim #1:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1001(1001)",
  "text": "I'm",
  "timestamp": "2025-11-22T10:30:05Z"
}
```

**Interim #2:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1001(1001)",
  "text": "I'm doing",
  "timestamp": "2025-11-22T10:30:06Z"
}
```

**Interim #3:**
```json
{
  "type": "interim",
  "speaker_id": "Extension 1001(1001)",
  "text": "I'm doing great",
  "timestamp": "2025-11-22T10:30:07Z"
}
```

**Event:** `transcription-final`

**Final:**
```json
{
  "type": "final",
  "speaker_id": "Extension 1001(1001)",
  "text": "I'm doing great, thanks for asking!",
  "timestamp": "2025-11-22T10:30:09Z"
}
```

### Example 5: Timeline Visualization

```
Time: 10:30:00 - SESSION START
├─ Event: session-start
└─ Data: {"type":"session_start","caller_id":"Extension 1000(1000)","callee_id":"Extension 1001(1001)"}

Time: 10:30:01 - CHANNEL 0 INTERIM #1
├─ Event: transcription-interim
└─ Data: {"type":"interim","speaker_id":"Extension 1000(1000)","text":"Hello"}

Time: 10:30:02 - CHANNEL 0 INTERIM #2
├─ Event: transcription-interim
└─ Data: {"type":"interim","speaker_id":"Extension 1000(1000)","text":"Hello, how"}

Time: 10:30:03 - CHANNEL 0 INTERIM #3
├─ Event: transcription-interim
└─ Data: {"type":"interim","speaker_id":"Extension 1000(1000)","text":"Hello, how are"}

Time: 10:30:04 - CHANNEL 0 FINAL ✅
├─ Event: transcription-final
└─ Data: {"type":"final","speaker_id":"Extension 1000(1000)","text":"Hello, how are you?"}

Time: 10:30:05 - CHANNEL 1 INTERIM #1
├─ Event: transcription-interim
└─ Data: {"type":"interim","speaker_id":"Extension 1001(1001)","text":"I'm"}

Time: 10:30:06 - CHANNEL 1 INTERIM #2
├─ Event: transcription-interim
└─ Data: {"type":"interim","speaker_id":"Extension 1001(1001)","text":"I'm doing"}

Time: 10:30:07 - CHANNEL 1 INTERIM #3
├─ Event: transcription-interim
└─ Data: {"type":"interim","speaker_id":"Extension 1001(1001)","text":"I'm doing great"}

Time: 10:30:09 - CHANNEL 1 FINAL ✅
├─ Event: transcription-final
└─ Data: {"type":"final","speaker_id":"Extension 1001(1001)","text":"I'm doing great, thanks for asking!"}
```

### Example 6: JavaScript Client Implementation

```javascript
import Pusher from 'pusher-js';

const pusher = new Pusher('your-pusher-key', { cluster: 'ap2' });
const callId = '3848276298220188511@atlanta.example.com';
const channel = pusher.subscribe(`call-${callId}`);

// Store interim text by speaker
const interimTranscripts = {
  "Extension 1000(1000)": "",
  "Extension 1001(1001)": ""
};

// Session start
channel.bind('session-start', (data) => {
  console.log('📞 Call Started');
  console.log(`   Caller: ${data.caller_id}`);
  console.log(`   Callee: ${data.callee_id}`);
  console.log(`   Time: ${data.timestamp}`);
});

// Interim transcriptions (updates in real-time)
channel.bind('transcription-interim', (data) => {
  interimTranscripts[data.speaker_id] = data.text;
  console.log(`💬 ${data.speaker_id}: "${data.text}" (interim)`);

  // Update UI in real-time (e.g., live captions)
  updateLiveCaptions(data.speaker_id, data.text, false);
});

// Final transcriptions (confirmed)
channel.bind('transcription-final', (data) => {
  console.log(`✅ ${data.speaker_id}: "${data.text}" (FINAL)`);

  // Clear interim for this speaker
  interimTranscripts[data.speaker_id] = "";

  // Save to transcript history
  saveToTranscript(data.speaker_id, data.text, data.timestamp);

  // Update UI with final version
  updateLiveCaptions(data.speaker_id, data.text, true);
});
```

---

## API Reference

### mod_audio_fork

**Syntax:**
```
uuid_audio_fork <uuid> start <wss-url> [mono|mixed|stereo] [8k|16k|24k|32k|48k|64k] [bugname] [metadata]
uuid_audio_fork <uuid> stop [bugname] [text]
uuid_audio_fork <uuid> send_text [bugname] <text>
uuid_audio_fork <uuid> pause [bugname]
uuid_audio_fork <uuid> resume [bugname]
uuid_audio_fork <uuid> graceful-shutdown [bugname]
```

**Examples:**
```javascript
// Start stereo 16kHz streaming
await ep.api('uuid_audio_fork', `${ep.uuid} start wss://backend.example.com/audio stereo 16k`);

// Start with custom bugname and metadata
await ep.api('uuid_audio_fork', `${ep.uuid} start wss://backend.example.com/audio stereo 16k my-bug {"userId":"123"}`);

// Send text message to backend
await ep.api('uuid_audio_fork', `${ep.uuid} send_text my-bug {"action":"mute"}`);

// Stop streaming
await ep.api('uuid_audio_fork', `${ep.uuid} stop`);
```

### mod_deepgram_transcribe

**Syntax:**
```
uuid_deepgram_transcribe <uuid> start <lang-code> [interim] [mono|mixed|stereo] [8k|16k] [bugname] [metadata]
uuid_deepgram_transcribe <uuid> stop [bugname]
```

**Examples:**
```javascript
// Start stereo 16kHz with interim results
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo 16k`);

// Start with metadata
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo 16k my-bug {"callType":"support"}`);

// Stop transcription
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} stop`);
```

**Environment Variables:**
```bash
DEEPGRAM_API_KEY=your_api_key
PUSHER_APP_ID=123456
PUSHER_KEY=your_pusher_key
PUSHER_SECRET=your_pusher_secret
PUSHER_CLUSTER=ap2
```

**Channel Variables:**
```javascript
await ep.set({
  DEEPGRAM_SPEECH_MODEL: 'phonecall',
  DEEPGRAM_SPEECH_TIER: 'nova-2',
  DEEPGRAM_SPEECH_DIARIZE: 'true',
  DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION: 'true'
});
```

### mod_aws_transcribe

**Syntax:**
```
uuid_aws_transcribe <uuid> start <lang-code> [interim] [mono|mixed|stereo] [8k|16k] [bugname] [metadata]
uuid_aws_transcribe <uuid> stop [bugname]
```

**Examples:**
```javascript
// Start stereo 16kHz with interim results
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo 16k`);

// Start with metadata
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo 16k my-bug {"callType":"support"}`);

// Stop transcription
await ep.api('uuid_aws_transcribe', `${ep.uuid} stop`);
```

**Environment Variables:**
```bash
AWS_ACCESS_KEY_ID=AKIA...
AWS_SECRET_ACCESS_KEY=...
AWS_REGION=us-east-1
PUSHER_APP_ID=123456
PUSHER_KEY=your_pusher_key
PUSHER_SECRET=your_pusher_secret
PUSHER_CLUSTER=ap2
```

**Channel Variables:**
```javascript
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
  AWS_SHOW_SPEAKER_LABEL: 'true',
  AWS_VOCABULARY_NAME: 'my-custom-vocab',
  AWS_REGION: 'us-east-1'
});
```

---

## Summary Tables

### Feature Comparison Matrix

| Feature | mod_audio_fork | mod_deepgram_transcribe | mod_aws_transcribe |
|---------|---------------|------------------------|-------------------|
| **Mix Types** | ✅ mono, mixed, stereo | ✅ mono, mixed, stereo | ✅ mono, mixed, stereo |
| **Default Mix** | ❌ Required parameter | mono | mono |
| **Sampling Rates** | 8k, 16k, 24k, 32k, 48k, 64k | 8k, 16k | 8k, 16k |
| **Default Sampling** | **8000 Hz** | **16000 Hz** | **16000 Hz** |
| **Metadata** | ✅ Sent to backend | ✅ Local tracking only | ✅ Local tracking only |
| **Metadata Enrichment** | ❌ No | ✅ Yes (caller/callee) | ✅ Yes (caller/callee) |
| **Buffer Type** | Circular (2-5 sec) | Circular (2-5 sec) | Deque (1 sec pre, unlimited post) |
| **Pusher Integration** | ❌ No | ✅ Yes | ✅ Yes |
| **Speaker Mapping** | N/A | Channel-based | Channel-based |
| **Interim Results** | N/A | ✅ Yes | ✅ Yes |
| **Empty Transcript Filter** | N/A | ✅ Yes | ✅ Yes |

### Resampling Matrix

| Codec | Default Rate | mod_audio_fork (8kHz) | mod_deepgram (16kHz) | mod_aws (16kHz) |
|-------|-------------|-----------------------|----------------------|-----------------|
| G.711 | 8000 Hz | ✅ No resampling | ⚠️ Upsampling | ⚠️ Upsampling |
| G.722 | 16000 Hz | ⚠️ Downsampling | ✅ No resampling | ✅ No resampling |
| Opus | 48000 Hz | ⚠️ Downsampling | ⚠️ Downsampling | ⚠️ Downsampling |

### Code Location Reference

| Feature | mod_audio_fork | mod_deepgram_transcribe | mod_aws_transcribe |
|---------|----------------|------------------------|-------------------|
| **Mix-type parsing** | mod_audio_fork.c:244-255 | mod_deepgram_transcribe.c:634-642 | mod_aws_transcribe.c:657-666 |
| **Sampling rate** | mod_audio_fork.c:233, 256-264 | mod_deepgram_transcribe.c:631, 645-657 | mod_aws_transcribe.c:654, 668-680 |
| **Metadata handling** | lws_glue.cpp:238 | mod_deepgram_transcribe.c:463-527 | mod_aws_transcribe.c:486-550 |
| **Buffer config** | lws_glue.cpp:25-26, 240 | dg_transcribe_glue.cpp:25-33 | aws_transcribe_glue.cpp:25-29 |
| **Speaker mapping** | N/A | mod_deepgram_transcribe.c:111-140 | mod_aws_transcribe.c:112-142 |
| **Pusher send** | N/A | mod_deepgram_transcribe.c:56-238 | mod_aws_transcribe.c:56-240 |

---

## Additional Resources

- [Module Comparison Guide](./MODULE_COMPARISON.md) - High-level module comparison
- [Stereo Channel Assignment](./STEREO_CHANNEL_ASSIGNMENT.md) - Channel assignment details
- [Real-time Transcription Delivery](./REALTIME_TRANSCRIPTION_DELIVERY.md) - Delivery options
- [mod_audio_fork README](../modules/mod_audio_fork/README.md)
- [mod_deepgram_transcribe README](../modules/mod_deepgram_transcribe/README.md)
- [mod_aws_transcribe README](../modules/mod_aws_transcribe/README.md)

---

## Contributing

Found an issue or have improvements? Please submit a pull request or open an issue on GitHub.

## License

This documentation is part of the freeswitch-speech-ai project and follows the same license terms.
