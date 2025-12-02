# mod_google_transcribe Alignment - TODO Summary

## Quick Reference

This is a high-level summary of tasks to align `mod_google_transcribe` with AWS/Deepgram patterns.
**Full details:** See `docs/mod_google_transcribe-alignment-plan.md`

---

## 🎯 Goals

1. ✅ Unified command: `uuid_google_transcribe <uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]`
2. ✅ Default 16kHz sample rate, support 8kHz
3. ✅ Interim results enabled by keyword
4. ✅ Stereo with automatic channel detection
5. ✅ Pusher integration (direct API)
6. ✅ Call metadata (caller, callee, SIP Call-ID)
7. ✅ Environment variable configuration
8. ✅ Features off by default

---

## 📋 Task Checklist

### Phase 1: Command Structure (Priority 1) ⚙️ ✅ COMPLETE
- [x] **1.1** Unify command syntax to match AWS/Deepgram
- [x] **1.2** Add sample rate parsing (8k/16k, default 16k)
- [x] **1.3** Add channel mode support (mono/mixed/stereo)

**Files:** `modules/mod_google_transcribe/mod_google_transcribe.c` (lines 441-544)
**Commit:** `4e31884` - feat(mod_google_transcribe): Unify command syntax with AWS/Deepgram pattern (Task 1.1)

---

### Phase 2: Metadata & Sessions (Priority 2) 📋
- [ ] **2.1** Extract call metadata (caller, callee, sip_call_id)
- [ ] **2.2** Add session start/stop events

**Files:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Reference:** `mod_aws_transcribe.c:583-647` (build_session_metadata)

---

### Phase 3: Pusher Integration (Priority 3) 📡
- [ ] **3.1** Add Pusher credential reading (env vars)
- [ ] **3.2** Implement Pusher HTTP API (HMAC SHA256 signing)
- [ ] **3.3** Integrate with response handler

**Files:** `modules/mod_google_transcribe/mod_google_transcribe.c`
**Reference:** `mod_aws_transcribe.c:21-292` (complete Pusher code - can copy directly)

---

### Phase 4: Environment Variables (Priority 4) 🔧
- [ ] **4.1** Add env var support for all Google Speech features
- [ ] **4.2** Set intelligent defaults for stereo (channel detection ON)

**Files:**
- `modules/mod_google_transcribe/google_glue_v1.cpp`
- `modules/mod_google_transcribe/google_glue_v2.cpp`

---

### Phase 5: Testing (Priority 5) ✅
- [ ] **5.1** Unit tests - command parsing
- [ ] **5.2** Integration tests - audio streaming (mono/mixed/stereo)
- [ ] **5.3** Integration tests - Pusher
- [ ] **5.4** Integration tests - environment variables
- [ ] **5.5** Regression tests - existing functionality

---

### Phase 6: Documentation (Priority 6) 📚
- [ ] **6.1** Update README with new command syntax
- [ ] **6.2** Update module documentation
- [ ] **6.3** Code cleanup and comments

**Files:**
- `modules/mod_google_transcribe/README.md`
- `docs/mod_google_transcribe-deps.md`

---

## 🏃 Sprint Plan (Implement One-by-One)

### Sprint 1: Command Structure (2-3 days)
**Tasks:** 1.1, 1.2, 1.3
**Test:** Verify command parsing with all parameter combinations

### Sprint 2: Metadata (1-2 days)
**Tasks:** 2.1, 2.2
**Test:** Verify metadata extraction and session events

### Sprint 3: Pusher (2-3 days)
**Tasks:** 3.1, 3.2, 3.3
**Test:** Verify Pusher events for interim/final results

### Sprint 4: Environment Variables (1-2 days)
**Tasks:** 4.1, 4.2
**Test:** Verify all env vars work, precedence correct

### Sprint 5: Testing (2-3 days)
**Tasks:** 5.1, 5.2, 5.3, 5.4, 5.5
**Test:** All test cases pass

### Sprint 6: Documentation (1 day)
**Tasks:** 6.1, 6.2, 6.3
**Deliverable:** Production-ready module

---

## 🔧 Environment Variables to Support

### Pusher Configuration
```bash
PUSHER_APP_ID=your-app-id
PUSHER_KEY=your-key
PUSHER_SECRET=your-secret
PUSHER_CLUSTER=ap2                          # Default cluster
PUSHER_CHANNEL_PREFIX=call-                 # Default prefix
PUSHER_EVENT_FINAL=transcription-final      # Event names
PUSHER_EVENT_INTERIM=transcription-interim
PUSHER_EVENT_SESSION_START=session-start
PUSHER_EVENT_SESSION_STOP=session-stop
```

### Google Speech Features
```bash
GOOGLE_SPEECH_SAMPLE_RATE=16000             # Override sample rate
GOOGLE_SPEECH_SINGLE_UTTERANCE=false        # Default: continuous
GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL=true  # For stereo
GOOGLE_SPEECH_MAX_ALTERNATIVES=1            # Number of alternatives
GOOGLE_SPEECH_PROFANITY_FILTER=false        # Default: off
GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS=false
GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=false
GOOGLE_SPEECH_MODEL=default                 # Model selection
GOOGLE_SPEECH_USE_ENHANCED=false
GOOGLE_SPEECH_HINTS=""                      # Comma-separated phrases
GOOGLE_SPEECH_SPEAKER_DIARIZATION=false     # Speaker detection
GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT=2
GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT=6
```

---

## 📊 Success Metrics

After implementation, verify:

1. ✅ Command works like AWS/Deepgram
2. ✅ Default 16kHz sample rate
3. ✅ Stereo mode enables channel detection automatically
4. ✅ Pusher receives all transcription events
5. ✅ Call metadata included in all events
6. ✅ Environment variables override defaults
7. ✅ No regressions in existing features
8. ✅ Documentation complete

---

## 📖 Key References

### Analysis Documents
- **Full Plan:** `docs/mod_google_transcribe-alignment-plan.md`
- **Dependencies:** `docs/mod_google_transcribe-deps.md`

### Reference Implementations
- **AWS Module:** `modules/mod_aws_transcribe/mod_aws_transcribe.c`
- **Deepgram Module:** `modules/mod_deepgram_transcribe/mod_deepgram_transcribe.c`

### Code Patterns to Copy
1. **Command parsing:** `mod_aws_transcribe.c:720-820`
2. **Metadata extraction:** `mod_aws_transcribe.c:583-647`
3. **Pusher integration:** `mod_aws_transcribe.c:21-292` (copy verbatim)
4. **Response handler:** `mod_aws_transcribe.c:457-519`

---

## 🚀 Getting Started

1. **Review full plan:** Read `docs/mod_google_transcribe-alignment-plan.md`
2. **Start Sprint 1:** Begin with Task 1.1 (command structure)
3. **Test after each task:** Don't proceed until current task works
4. **Update this checklist:** Mark tasks complete as you go

**Estimated total time:** 10-14 days for complete implementation

---

## ⚠️ Important Notes

- **Don't break existing functionality:** Keep uuid_google_transcribe2 as deprecated alias
- **Copy Pusher code:** AWS and Deepgram have identical implementations - copy directly
- **Test incrementally:** Build → Test → Commit after each task
- **Environment variable precedence:** Channel vars → Env vars → Defaults
- **Stereo defaults:** Automatically enable channel detection when stereo specified

---

## 📞 Example Commands (After Implementation)

```bash
# Basic (mono, 16kHz, no interim)
uuid_google_transcribe <uuid> start en-US

# With interim results
uuid_google_transcribe <uuid> start en-US interim

# Stereo with channel detection (auto-enabled)
uuid_google_transcribe <uuid> start en-US interim stereo

# 8kHz audio
uuid_google_transcribe <uuid> start en-US interim mono 8k

# With custom metadata
uuid_google_transcribe <uuid> start en-US interim stereo 16k '{"campaign":"sales"}'

# Stop transcription
uuid_google_transcribe <uuid> stop
```

---

**Ready to start? Begin with Sprint 1, Task 1.1! 🚀**
