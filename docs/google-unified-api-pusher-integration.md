# Google Transcribe: Unified API Pattern & Pusher Integration

**Date:** 2025-11-25
**Module:** mod_google_transcribe
**Task:** Match unified API pattern with metadata and Pusher integration

---

## 🎯 Objectives

Add to `mod_google_transcribe`:
1. ✅ Metadata extraction (caller/callee, speaker info) - NOT sent to Google API
2. ✅ Pusher integration for real-time transcript streaming
3. ✅ Speaker tagging based on channel information
4. ✅ Session start events
5. ✅ Unified JSON format matching AWS/Deepgram modules

---

## 📋 Implementation Plan

### 1. **Pusher Helper Functions** (mod_google_transcribe.c)

Add these functions before `responseHandler`:

```c
// HMAC-SHA256 for Pusher auth
static void hmac_sha256_hex(const char* key, const char* data, char* output);

// MD5 hash for body integrity
static void md5_hex(const char* data, char* output);

// Curl write callback to capture Pusher response
static size_t pusher_curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp);
```

### 2. **Pusher Transcript Sending** (mod_google_transcribe.c)

```c
static void send_to_pusher(
    switch_core_session_t* session,
    const char* callId,
    const char* transcript,
    int is_final,
    const char* speaker_id,  // NEW: From channel_tag
    const char* timestamp
);
```

**Features:**
- Send interim and final transcripts to Pusher
- Include speaker_id from Google's channel_tag
- Map channels to caller/callee
- Use environment variables for Pusher credentials

### 3. **Session Start Event** (mod_google_transcribe.c)

```c
static void send_session_start_to_pusher(
    switch_core_session_t* session,
    const char* callId
);
```

**Metadata Included:**
- `caller_id`: "Name(Number)"
- `callee_id`: "Name(Number)"
- `timestamp`: ISO 8601 format
- Event type: "session_start"

### 4. **Response Handler Integration** (mod_google_transcribe.c)

Update `responseHandler` to:
1. Extract call ID (sip_call_id → uuid → call_uuid fallback)
2. Extract speaker information from Google V2 response
3. Map channel_tag to speaker_id (caller/callee)
4. Call `send_to_pusher` for transcripts
5. Call `send_session_start_to_pusher` on start

### 5. **Metadata Extraction** (google_glue.cpp)

Extract metadata from FreeSWITCH channel variables:
- `caller_id_name`
- `caller_id_number`
- `callee_id_name` / `effective_callee_id_name`
- `destination_number` / `callee_id_number`
- `sip_call_id`

**Note:** Metadata NOT sent to Google API (V2 doesn't support RecognitionMetadata)

### 6. **Speaker Tagging Logic**

Google V2 API provides `channel_tag` in responses:
```
channel_tag = 0 → Channel 0 (Left)  → Typically caller
channel_tag = 1 → Channel 1 (Right) → Typically callee
```

Map to speaker_id:
```c
const char* speaker_id = (channel_tag == 0) ? "caller" : "callee";
```

---

## 🔧 Environment Variables

### Pusher Configuration (Required)

```bash
export PUSHER_APP_ID="your-app-id"
export PUSHER_KEY="your-key"
export PUSHER_SECRET="your-secret"
export PUSHER_CLUSTER="ap2"  # Optional, defaults to ap2
```

### Pusher Customization (Optional)

```bash
export PUSHER_CHANNEL_PREFIX="call-"  # Default: "call-"
export PUSHER_EVENT_SESSION_START="session-start"  # Default
export PUSHER_EVENT_INTERIM="transcript-interim"  # Default
export PUSHER_EVENT_FINAL="transcript-final"  # Default
```

---

## 📊 JSON Format (Pusher Events)

### Session Start Event

```json
{
  "type": "session_start",
  "caller_id": "John Doe(1001)",
  "callee_id": "Support(+18005551234)",
  "timestamp": "2025-11-25T10:30:00Z"
}
```

### Transcript Event (Interim/Final)

```json
{
  "type": "interim",  // or "final"
  "speaker_id": "caller",  // or "callee"
  "text": "Hello, how are you?",
  "timestamp": "2025-11-25T10:30:05Z",
  "channel_id": "ch_0",  // Google channel_tag
  "language_code": "en-US",
  "is_final": false,
  "confidence": 0.95
}
```

---

## 🔄 Call Flow

### 1. **Call Starts**
```
dialplan → uuid_google_transcribe start
          ↓
    session_init()
          ↓
    send_session_start_to_pusher()
          ↓
    Pusher: session-start event
```

### 2. **During Call**
```
Audio frames → Google V2 API
                    ↓
         StreamingRecognizeResponse
                    ↓
              responseHandler()
                    ↓
         Extract channel_tag, transcript
                    ↓
           send_to_pusher()
                    ↓
    Pusher: transcript-interim/final events
```

### 3. **Call Ends**
```
hangup → media_bug_close
              ↓
       session_cleanup()
```

---

## 📝 Code Additions

### Required Headers (mod_google_transcribe.c)

```c
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/evp.h>
#include <curl/curl.h>
#include <time.h>
```

### Pusher Response Buffer

```c
struct pusher_response {
    char* data;
    size_t size;
};
```

---

## 🎯 Key Differences from AWS/Deepgram

| Aspect | AWS/Deepgram | Google V2 |
|--------|--------------|-----------|
| **Channel ID** | `channel_id` field | `channel_tag` field |
| **Speaker Detection** | Optional diarization | Optional diarization + channel_tag |
| **Metadata to API** | Supported | ❌ NOT supported in V2 |
| **Local Metadata** | Extracted, used for Pusher | Extracted, used for Pusher |
| **Speaker Mapping** | channel_id → speaker | channel_tag → speaker |

---

## ✅ Testing Checklist

- [ ] Pusher credentials validation
- [ ] Session start event sent
- [ ] Interim transcripts sent to Pusher
- [ ] Final transcripts sent to Pusher
- [ ] Speaker_id correctly mapped (caller/callee)
- [ ] Call ID fallback logic works (sip_call_id → uuid)
- [ ] Metadata extraction (caller/callee names)
- [ ] HTTP error handling (401, 403, 400)
- [ ] Logging at appropriate levels
- [ ] No crashes with missing Pusher credentials

---

## 📚 Implementation Files

1. **`modules/mod_google_transcribe/mod_google_transcribe.c`**
   - Add Pusher functions
   - Update responseHandler
   - Add metadata extraction

2. **`examples/freeswitch-config/dialplan/default.xml`**
   - Add caller/callee variable examples
   - Document Pusher environment variables

3. **`docs/google-unified-api-pusher-integration.md`** (this file)
   - Complete documentation

---

## 🚀 Benefits

1. ✅ **Unified API** - Consistent with AWS/Deepgram modules
2. ✅ **Real-time Streaming** - Pusher integration for live transcripts
3. ✅ **Speaker Identification** - Accurate caller/callee tagging
4. ✅ **Metadata Rich** - Caller/callee information in events
5. ✅ **Production Ready** - Error handling and logging
6. ✅ **Future Proof** - Ready for downstream processing/AI

---

**Implementation Status:** Planning Complete - Ready for Code Implementation
