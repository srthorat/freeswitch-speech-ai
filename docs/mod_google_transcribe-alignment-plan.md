# mod_google_transcribe Alignment Implementation Plan

## Analysis Summary

Based on analysis of `mod_aws_transcribe` and `mod_deepgram_transcribe`, here are the key patterns to align:

### Current State (mod_google_transcribe)
- ✅ Two commands: `uuid_google_transcribe` (v1-style with channel vars) and `uuid_google_transcribe2` (v2-style with CLI params)
- ✅ Supports Google Speech API v1p1beta1 and v2
- ✅ Uses gRPC streaming
- ❌ No unified command syntax with AWS/Deepgram
- ❌ No sample rate parsing (8k/16k)
- ❌ No channel mode support (mono/mixed/stereo)
- ❌ No Pusher integration
- ❌ No call metadata extraction
- ❌ Inconsistent defaults vs AWS/Deepgram

### Target State (Aligned with AWS/Deepgram)
```
uuid_google_transcribe <uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]
```

**Defaults:**
- Sample rate: **16kHz** (matching AWS/Deepgram)
- Interim results: **enabled** when "interim" keyword present
- Channel mode: **mono** (single channel, READ stream only)
- Stereo: **automatic channel detection ON** when stereo specified
- Features: **OFF by default**, enabled via environment variables
- Pusher: **enabled** when credentials provided via env vars

---

## Implementation TODO List

### Phase 1: Command Structure & Parameter Parsing ⚙️

#### Task 1.1: Unify Command Syntax
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Current:** Lines 600-750 (two separate command handlers)
**Goal:** Single command with AWS/Deepgram-style parameter parsing

**Changes:**
- [ ] Keep `uuid_google_transcribe` as primary command
- [ ] Deprecate `uuid_google_transcribe2` (or make it alias)
- [ ] Parse parameters in order: `<uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]`
- [ ] Add parameter validation with helpful error messages

**Expected Behavior:**
```bash
# Mono, 16kHz, no interim (defaults)
uuid_google_transcribe <uuid> start en-US

# With interim results
uuid_google_transcribe <uuid> start en-US interim

# Stereo, 8kHz, interim
uuid_google_transcribe <uuid> start en-US interim stereo 8k

# With JSON metadata
uuid_google_transcribe <uuid> start en-US interim stereo 16k '{"caller":"Alice"}'
```

**Reference:** `mod_aws_transcribe.c:720-820`

---

#### Task 1.2: Add Sample Rate Parsing
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Support `8k`, `16k`, or numeric sample rates

**Changes:**
- [ ] Default sampling = 16000 (16kHz)
- [ ] Parse argv[5]: "8k" → 8000, "16k" → 16000, or atoi() for numeric
- [ ] Validate rate is multiple of 8000
- [ ] Pass to `google_speech_session_init_v1/v2`

**Code Pattern:**
```c
int sampling = 16000;  // Default 16kHz
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

**Reference:** `mod_aws_transcribe.c:773-785`

---

#### Task 1.3: Add Channel Mode Support (mono/mixed/stereo)
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Support mono (default), mixed, stereo channel modes

**Changes:**
- [ ] Default flags = `SMBF_READ_STREAM` (mono, caller audio only)
- [ ] Parse argv[4] for "mono", "mixed", "stereo"
- [ ] Set flags based on mode:
  - `mono`: `SMBF_READ_STREAM`
  - `mixed`: `SMBF_READ_STREAM | SMBF_WRITE_STREAM`
  - `stereo`: `SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO`
- [ ] Calculate channels: `channels = flags & SMBF_STEREO ? 2 : 1`
- [ ] Pass channels to Google API config

**Code Pattern:**
```c
switch_media_bug_flag_t flags = SMBF_READ_STREAM;  // Default: mono
uint32_t channels = 1;

if (argc > 4) {
    if (!strcmp(argv[4], "mixed")) {
        flags |= SMBF_WRITE_STREAM;
    } else if (!strcmp(argv[4], "stereo")) {
        flags |= SMBF_WRITE_STREAM;
        flags |= SMBF_STEREO;
        channels = 2;
    }
}
```

**Reference:** `mod_aws_transcribe.c:735-750`

---

### Phase 2: Metadata & Session Management 📋

#### Task 2.1: Extract Call Metadata
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Extract caller, callee, and SIP Call-ID information

**Changes:**
- [ ] Add `build_session_metadata()` function (copy from AWS/Deepgram)
- [ ] Extract from channel variables:
  - `caller_id_name` + `caller_id_number` → callerName/callerNumber
  - `callee_id_name` or `effective_callee_id_name` + `destination_number` → calleeName/calleeNumber
  - `sip_call_id` → call-Id
- [ ] Merge with user-provided metadata (argv[6])
- [ ] Pass to session init and response handler

**Structure:**
```json
{
  "callerName": "Extension 1000",
  "callerNumber": "1000",
  "calleeName": "Extension 1001",
  "calleeNumber": "1001",
  "call-Id": "abc123@sip.example.com"
}
```

**Reference:** `mod_aws_transcribe.c:583-647`

---

#### Task 2.2: Add Session Start/Stop Events
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Emit session lifecycle events with metadata

**Changes:**
- [ ] Add event subclasses:
  - `TRANSCRIBE_EVENT_SESSION_START` → "google_transcribe::session_start"
  - `TRANSCRIBE_EVENT_SESSION_STOP` → "google_transcribe::session_stop"
- [ ] Emit SESSION_START when gRPC stream connected
- [ ] Include full metadata in event body
- [ ] Emit SESSION_STOP when stream closes
- [ ] Register events in `mod_transcribe_load()`

**Reference:** `mod_aws_transcribe.c:457-519` (responseHandler)

---

### Phase 3: Pusher Integration 📡

#### Task 3.1: Add Pusher Configuration
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Read Pusher credentials from environment/channel variables

**Changes:**
- [ ] Add Pusher credential reading (priority: channel vars → env vars):
  - `PUSHER_APP_ID`
  - `PUSHER_KEY`
  - `PUSHER_SECRET`
  - `PUSHER_CLUSTER` (default: "ap2")
- [ ] Add Pusher event name configuration:
  - `PUSHER_CHANNEL_PREFIX` (default: "call-")
  - `PUSHER_EVENT_FINAL` (default: "transcription-final")
  - `PUSHER_EVENT_INTERIM` (default: "transcription-interim")
  - `PUSHER_EVENT_SESSION_START` (default: "session-start")
  - `PUSHER_EVENT_SESSION_STOP` (default: "session-stop")

**Reference:** `mod_aws_transcribe.c:78-127`

---

#### Task 3.2: Implement Pusher Sending Logic
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Send transcriptions directly to Pusher API

**Changes:**
- [ ] Add `send_to_pusher()` function (copy from AWS/Deepgram):
  - Build channel name: `"call-" + sip_call_id`
  - Transform Google JSON to Pusher event format
  - Calculate body MD5
  - Build HMAC SHA256 signature
  - Send HTTP POST to `api-<cluster>.pusher.com`
- [ ] Add helper functions:
  - `md5_hex()` for body hash
  - `hmac_sha256_hex()` for signature
  - `json_escape()` for escaping JSON strings
- [ ] Add retry logic (curl with timeout)

**Pusher Event Format:**
```json
{
  "name": "transcription-final",
  "channel": "call-abc123@sip.example.com",
  "data": "{\"type\":\"final\",\"speaker_id\":\"Extension 1000\",\"text\":\"hello world\",\"timestamp\":\"2024-12-02T15:30:46Z\"}"
}
```

**Reference:**
- `mod_aws_transcribe.c:21-292` (complete Pusher implementation)
- `mod_deepgram_transcribe.c:21-290` (identical implementation)

---

#### Task 3.3: Integrate Pusher with Response Handler
**File:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Goal:** Send all transcription results to Pusher

**Changes:**
- [ ] Modify `responseHandler()` function:
  - Check if Pusher configured (app_id, key, secret, sip_call_id all present)
  - Determine if result is final or interim
  - Transform Google JSON to Pusher format
  - Call `send_to_pusher()`
- [ ] Handle both v1p1beta1 and v2 API response formats
- [ ] Extract speaker info for stereo/diarization
- [ ] Add timestamps to events

**Reference:** `mod_aws_transcribe.c:347-443` (interim results handling)

---

### Phase 4: Environment Variable Configuration 🔧

#### Task 4.1: Add Environment Variable Support
**Files:**
- `modules/mod_google_transcribe/google_glue_v1.cpp`
- `modules/mod_google_transcribe/google_glue_v2.cpp`

**Goal:** Support environment variable overrides for all Google Speech features

**Changes:**
- [ ] Read from channel variables FIRST, then environment variables
- [ ] Support all Google Speech API parameters:
  - `GOOGLE_SPEECH_SAMPLE_RATE` (override sample rate)
  - `GOOGLE_SPEECH_SINGLE_UTTERANCE` (default: false)
  - `GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL` (default: false for mono/mixed, true for stereo)
  - `GOOGLE_SPEECH_MAX_ALTERNATIVES` (default: 1)
  - `GOOGLE_SPEECH_PROFANITY_FILTER` (default: false)
  - `GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS` (default: false)
  - `GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION` (default: false)
  - `GOOGLE_SPEECH_MODEL` (default: "default", options: "medical", "phone_call", etc.)
  - `GOOGLE_SPEECH_USE_ENHANCED` (default: false)
  - `GOOGLE_SPEECH_HINTS` (comma-separated phrases)
  - `GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES` (comma-separated)
  - `GOOGLE_SPEECH_SPEAKER_DIARIZATION` (default: false)
  - `GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT` (default: 2)
  - `GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT` (default: 6)
  - `GOOGLE_SPEECH_METADATA_INTERACTION_TYPE` (optional)
  - `GOOGLE_SPEECH_METADATA_MICROPHONE_DISTANCE` (optional)

**Pattern:**
```cpp
// Priority: channel variable → environment variable → default
const char* sample_rate_str = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SAMPLE_RATE");
if (!sample_rate_str) sample_rate_str = getenv("GOOGLE_SPEECH_SAMPLE_RATE");
if (sample_rate_str) config->set_sample_rate_hertz(atoi(sample_rate_str));
```

**Reference:** `dg_transcribe_glue.cpp:141-266`

---

#### Task 4.2: Set Intelligent Defaults for Stereo
**Files:**
- `modules/mod_google_transcribe/google_glue_v1.cpp`
- `modules/mod_google_transcribe/google_glue_v2.cpp`

**Goal:** When stereo mode is used, enable channel detection automatically

**Changes:**
- [ ] If `channels == 2` (stereo), set defaults:
  - `SEPARATE_RECOGNITION_PER_CHANNEL` = true (unless explicitly disabled)
  - `audio_channel_count` = 2
  - Enable speaker diarization if `GOOGLE_SPEECH_SPEAKER_DIARIZATION=1`
- [ ] For mono/mixed, keep single channel processing

**Code Pattern:**
```cpp
if (channels > 1) {
    config->set_audio_channel_count(channels);

    // Default: enable separate recognition for stereo
    const char* separate_recognition = switch_channel_get_variable(channel, "GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL");
    if (!separate_recognition) separate_recognition = getenv("GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL");

    // Default to enabled for stereo
    if (!separate_recognition || atoi(separate_recognition) == 1) {
        config->set_enable_separate_recognition_per_channel(true);
    }
}
```

**Reference:** `google_glue_v1.cpp:92-100`

---

### Phase 5: Testing & Validation ✅

#### Task 5.1: Unit Tests - Command Parsing
**Goal:** Test parameter parsing for all combinations

**Test Cases:**
- [ ] Minimal: `uuid_google_transcribe <uuid> start en-US`
- [ ] With interim: `uuid_google_transcribe <uuid> start en-US interim`
- [ ] With channel mode: `uuid_google_transcribe <uuid> start en-US interim stereo`
- [ ] With sample rate: `uuid_google_transcribe <uuid> start en-US interim stereo 8k`
- [ ] With metadata: `uuid_google_transcribe <uuid> start en-US interim stereo 16k '{"test":"data"}'`
- [ ] Invalid parameters (should fail gracefully)
- [ ] Stop command: `uuid_google_transcribe <uuid> stop`

**Validation:**
- Correct sample rate selected
- Correct flags set for media bug
- Correct channel count
- Metadata parsed and merged correctly

---

#### Task 5.2: Integration Tests - Audio Streaming
**Goal:** Test actual transcription with different configurations

**Test Cases:**
- [ ] **Mono 16kHz**: Single caller audio, no interim
- [ ] **Mono 16kHz with interim**: Real-time partial results
- [ ] **Mono 8kHz**: Lower quality audio
- [ ] **Mixed 16kHz**: Caller + callee mixed into single channel
- [ ] **Stereo 16kHz**: Separate channels for caller/callee
- [ ] **Stereo with channel detection**: Verify separate transcriptions
- [ ] **Sample rate conversion**: Test 8kHz → 16kHz resampling (if needed)

**Validation:**
- Transcription accuracy
- Interim results timing
- Channel separation (stereo)
- No audio dropouts
- Proper cleanup on stop

---

#### Task 5.3: Integration Tests - Pusher
**Goal:** Verify Pusher integration works correctly

**Test Cases:**
- [ ] **Session start event**: Verify metadata sent to Pusher on stream connect
- [ ] **Interim results**: Check interim events sent to Pusher
- [ ] **Final results**: Check final events sent to Pusher
- [ ] **Session stop event**: Verify stop event sent on cleanup
- [ ] **No Pusher credentials**: Should work without errors (no-op)
- [ ] **Invalid credentials**: Should log error but continue transcription
- [ ] **Multiple concurrent calls**: Each gets unique channel (`call-<sip_call_id>`)

**Validation:**
- Pusher events received in correct order
- Event format matches AWS/Deepgram
- Speaker identification correct (stereo)
- Timestamps present
- Channel naming correct

---

#### Task 5.4: Integration Tests - Environment Variables
**Goal:** Test all configuration via environment variables

**Test Cases:**
- [ ] Override sample rate via `GOOGLE_SPEECH_SAMPLE_RATE`
- [ ] Enable punctuation via `GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION`
- [ ] Enable speaker diarization via `GOOGLE_SPEECH_SPEAKER_DIARIZATION`
- [ ] Set custom model via `GOOGLE_SPEECH_MODEL`
- [ ] Add speech hints via `GOOGLE_SPEECH_HINTS`
- [ ] Configure Pusher via `PUSHER_*` env vars
- [ ] Test precedence: channel variable > env variable > default

**Validation:**
- Settings applied correctly
- Defaults used when not specified
- Channel variables override env variables

---

#### Task 5.5: Regression Tests - Existing Functionality
**Goal:** Ensure existing features still work

**Test Cases:**
- [ ] **v1p1beta1 API**: All features work with v1 API
- [ ] **v2 API**: All features work with v2 API
- [ ] **API version selection**: `GOOGLE_SPEECH_CLOUD_SERVICES_VERSION` env var works
- [ ] **All existing channel variables**: Backward compatibility maintained
- [ ] **VAD (Voice Activity Detection)**: `START_RECOGNIZING_ON_VAD` still works
- [ ] **All metadata options**: Interaction type, NAICS code, mic distance, etc.

**Validation:**
- No regressions
- All existing features functional
- Documentation updated

---

### Phase 6: Documentation & Cleanup 📚

#### Task 6.1: Update README
**File:** `modules/mod_google_transcribe/README.md`

**Changes:**
- [ ] Update command syntax examples
- [ ] Document new parameter order
- [ ] Add sample rate section (8k vs 16k)
- [ ] Add channel mode section (mono/mixed/stereo)
- [ ] Document Pusher integration
- [ ] Add environment variable reference table
- [ ] Add migration guide from uuid_google_transcribe2

---

#### Task 6.2: Update Module Documentation
**File:** `docs/mod_google_transcribe-deps.md`

**Changes:**
- [ ] Add Pusher dependencies (libcurl, OpenSSL)
- [ ] Document environment variables
- [ ] Add Pusher configuration section
- [ ] Update examples

---

#### Task 6.3: Code Cleanup
**Files:** All module files

**Changes:**
- [ ] Remove or deprecate `uuid_google_transcribe2` (or make alias)
- [ ] Add code comments explaining alignment with AWS/Deepgram
- [ ] Consistent error messages
- [ ] Consistent logging (use same log levels as AWS/Deepgram)
- [ ] Memory leak checks (valgrind)
- [ ] Clean up unused code

---

## Implementation Order (Priority)

### Sprint 1: Core Command & Parsing (Tasks 1.1-1.3)
**Goal:** Get basic aligned command working
**Deliverable:** Command accepts AWS/Deepgram-style parameters

### Sprint 2: Metadata & Sessions (Tasks 2.1-2.2)
**Goal:** Extract and use call metadata
**Deliverable:** Session events with caller/callee info

### Sprint 3: Pusher Integration (Tasks 3.1-3.3)
**Goal:** Send transcriptions to Pusher
**Deliverable:** Real-time transcripts via Pusher

### Sprint 4: Environment Variables (Tasks 4.1-4.2)
**Goal:** Configuration via env vars
**Deliverable:** All features configurable

### Sprint 5: Testing (Tasks 5.1-5.5)
**Goal:** Comprehensive validation
**Deliverable:** All tests passing

### Sprint 6: Documentation (Tasks 6.1-6.3)
**Goal:** Complete documentation
**Deliverable:** Production-ready module

---

## Success Criteria

✅ **Command Alignment**: Single command with AWS/Deepgram-style parameters
✅ **Sample Rate**: Supports 8kHz and 16kHz (default 16kHz)
✅ **Channel Modes**: Supports mono/mixed/stereo
✅ **Stereo Detection**: Automatic channel detection for stereo
✅ **Interim Results**: Enabled by "interim" keyword
✅ **Pusher Integration**: Direct Pusher API integration
✅ **Call Metadata**: Caller, callee, SIP Call-ID extraction
✅ **Environment Variables**: All features configurable via env vars
✅ **Defaults**: Sensible defaults (16kHz, mono, features off)
✅ **Backward Compatibility**: Existing features still work
✅ **Testing**: All test cases pass
✅ **Documentation**: Complete and accurate

---

## Files to Modify

### C/C++ Source Files
- [ ] `modules/mod_google_transcribe/mod_google_transcribe.c` (main command handler)
- [ ] `modules/mod_google_transcribe/google_glue_v1.cpp` (v1 API, env vars)
- [ ] `modules/mod_google_transcribe/google_glue_v2.cpp` (v2 API, env vars)
- [ ] `modules/mod_google_transcribe/mod_google_transcribe.h` (add Pusher structs)

### Documentation Files
- [ ] `modules/mod_google_transcribe/README.md` (usage examples)
- [ ] `docs/mod_google_transcribe-deps.md` (dependencies)

### Build Files (if needed)
- [ ] `scripts/install-all.sh` (already updated, may need libcurl check)

---

## Dependencies

### New Dependencies
- **libcurl**: For Pusher HTTP API calls (already available in FreeSWITCH)
- **OpenSSL**: For HMAC SHA256 signatures (already available)

### Existing Dependencies
- gRPC, protobuf (already configured)
- Google Cloud Speech API proto files (already generated)

---

## Risk Mitigation

### Risk 1: Breaking Existing Deployments
**Mitigation:** Keep uuid_google_transcribe2 as deprecated alias, add migration guide

### Risk 2: Pusher Integration Complexity
**Mitigation:** Copy proven implementation from AWS/Deepgram (identical code)

### Risk 3: Performance Impact
**Mitigation:** Pusher sends are async (fire-and-forget), minimal overhead

### Risk 4: Environment Variable Conflicts
**Mitigation:** Use consistent `GOOGLE_SPEECH_*` prefix, document precedence

---

## Next Steps

1. **Review this plan** and approve/adjust priorities
2. **Start with Sprint 1** (Tasks 1.1-1.3) - command structure alignment
3. **Test each task** before moving to next
4. **Iterate** based on testing results

**Estimated Timeline:**
- Sprint 1: 2-3 days
- Sprint 2: 1-2 days
- Sprint 3: 2-3 days (most complex - Pusher)
- Sprint 4: 1-2 days
- Sprint 5: 2-3 days (thorough testing)
- Sprint 6: 1 day

**Total:** ~10-14 days for complete implementation and testing
