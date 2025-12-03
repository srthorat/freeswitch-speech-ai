# Google Speech-to-Text Model Selection Guide

## Overview

Google Cloud Speech-to-Text v1 and v2 APIs use **different model names**. This guide explains the model differences and how mod_google_transcribe handles automatic model selection.

## Model Names by API Version

### V1 API Models

| Model Name | Use Case | Description |
|------------|----------|-------------|
| `phone_call` | **Telephony (Recommended)** | Optimized for phone call audio (8-16kHz) |
| `video` | Video/Meetings | Optimized for video conferencing audio |
| `command_and_search` | Voice Commands | Optimized for short voice commands |
| `default` | General Purpose | Google's default model selection |

### V2 API Models

| Model Name | Use Case | Description |
|------------|----------|-------------|
| `long` | **Telephony/General (Recommended)** | Long-form audio, works well for telephony |
| `short` | Voice Commands | Short-form audio, voice commands |
| `chirp` | Latest Model | Google's Chirp model (latest) |
| `chirp_2` | Chirp 2 | Second generation Chirp model |
| `telephony` | Telephony 8kHz | Specifically for 8kHz telephony |
| `telephony_short` | Short Telephony | Short telephony audio |

## Automatic Model Selection

### Default Behavior (No Model Specified)

When you don't set `GOOGLE_SPEECH_MODEL`, the module **automatically selects the best model for telephony**:

```bash
# No model specified - auto-selects optimal telephony model
uuid_google_transcribe <UUID> start en-US interim stereo 8k
# → V1 uses: phone_call

uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
# → V2 uses: long
```

**Log output:**
```
V1 API: Auto-selected model 'phone_call' for telephony
V2 API: Auto-selected model 'long' for telephony
```

### Cross-Version Compatibility

You can use **V1 model names with V2** - they will be automatically mapped:

```bash
# Set V1 model name
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call

# Use with V2 API - automatically mapped to 'long'
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
```

**Mapping Table:**

| V1 Model Name | → | V2 Model Name | Use Case |
|---------------|---|---------------|----------|
| `phone_call` | → | `long` | Telephony/General |
| `video` | → | `long` | Video/Meetings |
| `command_and_search` | → | `short` | Voice Commands |
| `default` | → | `long` | General Purpose |

**Log output:**
```
V2 API: Mapped 'phone_call' → 'long'
V2 API: Using model 'long'
```

### V2-Specific Models

You can also use V2-specific model names directly:

```bash
# Use V2-specific model
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL chirp_2

# Works with V2 API
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
```

**Log output:**
```
V2 API: Using model 'chirp_2'
```

## Configuration Examples

### Example 1: No Configuration (Auto-Select)

```bash
# V1 API - auto-selects 'phone_call'
uuid_google_transcribe <UUID> start en-US interim stereo 8k

# V2 API - auto-selects 'long'
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
```

**Result:**
- ✅ V1 uses `phone_call` model
- ✅ V2 uses `long` model
- ✅ Both optimized for telephony

### Example 2: Set Once, Works for Both APIs

```bash
# Set model once
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call

# Test V1 - uses 'phone_call'
uuid_google_transcribe <UUID> start en-US interim stereo 8k

# Test V2 - automatically maps to 'long'
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
```

**Result:**
- ✅ Single configuration works for both APIs
- ✅ Automatic translation for v2
- ✅ No need to change config when switching APIs

### Example 3: V2-Specific Model

```bash
# Use V2-specific model
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL chirp_2

# Only use with V2 API
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
```

**Result:**
- ✅ Uses latest Chirp 2 model with V2
- ⚠️  Don't use with V1 (chirp_2 doesn't exist in V1)

### Example 4: Dialplan Configuration

```xml
<extension name="transcribe_both_apis">
  <condition field="destination_number" expression="^9001$">
    <action application="answer"/>

    <!-- Set model once - works for both v1 and v2 -->
    <action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>
    <action application="set" data="GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL=true"/>

    <!-- Test V1 API -->
    <action application="uuid_google_transcribe" data="${uuid} start en-US interim stereo 8k"/>
    <action application="sleep" data="10000"/>
    <action application="uuid_google_transcribe" data="${uuid} stop"/>

    <!-- Test V2 API -->
    <action application="uuid_google_transcribe2" data="${uuid} start en-US interim stereo 8k"/>
    <action application="sleep" data="10000"/>
    <action application="uuid_google_transcribe2" data="${uuid} stop"/>

    <action application="hangup"/>
  </condition>
</extension>
```

## Model Recommendations by Use Case

### Telephony (Phone Calls)

**Recommended:**
- V1: `phone_call`
- V2: `long` or `telephony`

```bash
# Option 1: Auto-select (recommended)
# No model specified - automatically uses phone_call/long

# Option 2: Explicit
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call
```

### Video Conferencing

**Recommended:**
- V1: `video`
- V2: `long`

```bash
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL video
```

### Voice Commands

**Recommended:**
- V1: `command_and_search`
- V2: `short`

```bash
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL command_and_search
```

### Latest/Best Quality

**Recommended:**
- V2: `chirp` or `chirp_2`

```bash
# Only use with V2 API
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL chirp_2
uuid_google_transcribe2 <UUID> start en-US interim stereo 16k
```

## Verification

### Check Logs to Verify Model Selection

```bash
# Watch logs
sudo tail -f /var/log/freeswitch/freeswitch.log | grep -E "V1 API|V2 API"
```

### Expected Log Output

**Auto-Selection (No Model Set):**
```
V1 API: Auto-selected model 'phone_call' for telephony
V1 API: Using model 'phone_call'

V2 API: Auto-selected model 'long' for telephony
V2 API: Using model 'long'
```

**V1 Model with V2 API (Auto-Mapping):**
```
V2 API: Mapped 'phone_call' → 'long'
V2 API: Using model 'long'
```

**V2-Specific Model:**
```
V2 API: Using model 'chirp_2'
```

## Migration Guide

### If You Were Using V1 with `phone_call` Model

**Before (V1 only):**
```bash
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call
uuid_google_transcribe <UUID> start en-US interim stereo 8k
```

**After (Works with Both V1 and V2):**
```bash
# Same configuration works for both!
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call

# V1 uses phone_call
uuid_google_transcribe <UUID> start en-US interim stereo 8k

# V2 auto-maps to long
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k
```

### If You Want to Use V2-Specific Models

**Use V2-specific model names directly:**
```bash
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL long
# or
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL chirp_2
# or
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL telephony
```

## Benefits of Auto-Selection

1. ✅ **Works Out of Box** - No model configuration needed for telephony
2. ✅ **Cross-Version Compatible** - V1 model names work with V2
3. ✅ **Easy Migration** - Switch between v1/v2 without config changes
4. ✅ **Clear Logging** - See exactly which model is being used
5. ✅ **Best Practices** - Auto-selects optimal models for telephony

## Troubleshooting

### Issue: Model Not Found Error

**Symptom:**
```
Error: Model 'phone_call' not found in v2
```

**Solution:**
- Check logs - mod_google_transcribe should auto-map v1 names to v2
- If using old version, rebuild module
- Verify logs show: "V2 API: Mapped 'phone_call' → 'long'"

### Issue: Want to Use Specific V2 Model

**Solution:**
```bash
# Use V2-specific model names directly
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL long
# or chirp, chirp_2, telephony, telephony_short, short
```

### Issue: Unsure Which Model is Being Used

**Solution:**
```bash
# Check logs - model selection is logged at INFO level
sudo tail -f /var/log/freeswitch/freeswitch.log | grep "Using model"

# Output shows:
# V1 API: Using model 'phone_call'
# V2 API: Using model 'long'
```

## Summary

- **No model set?** Auto-selects best telephony model (`phone_call` for v1, `long` for v2)
- **Set v1 model name?** Works with both v1 and v2 (auto-mapped for v2)
- **Want v2-specific model?** Use v2 model names directly (`chirp`, `chirp_2`, etc.)
- **Check logs** to verify which model is actually being used

## Related Documentation

- [Stereo Transcription Guide](STEREO-TRANSCRIPTION-GUIDE.md)
- [Environment Variables Guide](mod_google_transcribe-environment-variables.md)
- [Google Cloud Speech-to-Text v1 Models](https://cloud.google.com/speech-to-text/docs/speech-to-text-supported-languages)
- [Google Cloud Speech-to-Text v2 Models](https://cloud.google.com/speech-to-text/v2/docs/speech-to-text-supported-languages)
