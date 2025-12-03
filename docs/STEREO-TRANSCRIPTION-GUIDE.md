# Stereo Transcription with Separate Recognition per Channel

This guide explains how to use stereo audio capture with separate recognition for caller and callee channels in mod_google_transcribe.

## Overview

**Stereo mode** captures both channels of a call (caller and callee) and can either:
- **Mixed mode**: Combine both channels into one transcription stream
- **Separate recognition**: Transcribe each channel separately (caller and callee distinguished)

## Audio Capture Modes

| Mode | Flag | Description | Use Case |
|------|------|-------------|----------|
| `mono` | `SMBF_READ_STREAM` | Only inbound audio (caller) | Single participant transcription |
| `mixed` | `SMBF_READ_STREAM + SMBF_WRITE_STREAM` | Both channels mixed together | Conference calls, combined transcript |
| `stereo` | `SMBF_READ_STREAM + SMBF_WRITE_STREAM + SMBF_STEREO` | Both channels separate | Identify who said what (caller vs callee) |

## Enabling Separate Recognition per Channel

### Method 1: Using Channel Variable (Recommended)

Set the channel variable **before** starting transcription:

```bash
# In fs_cli or dialplan
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true
```

### Method 2: In Dialplan (XML)

```xml
<extension name="stereo_transcribe">
  <condition field="destination_number" expression="^9003$">
    <action application="answer"/>

    <!-- Enable separate recognition per channel -->
    <action application="set" data="GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL=true"/>

    <!-- Optional: Use phone_call model for better telephony recognition -->
    <action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>

    <!-- Start v2 API with stereo mode -->
    <action application="uuid_google_transcribe2" data="${uuid} start en-US interim stereo 8k"/>

    <!-- Your call logic here -->
    <action application="bridge" data="sofia/internal/destination@gateway"/>

    <!-- Stop transcription -->
    <action application="uuid_google_transcribe2" data="${uuid} stop"/>
  </condition>
</extension>
```

## Complete Command Syntax

### For v1 API (uuid_google_transcribe):

```bash
uuid_google_transcribe <UUID> start <lang> [interim] [mono|mixed|stereo] [8k|16k] [metadata]
```

### For v2 API (uuid_google_transcribe2):

```bash
uuid_google_transcribe2 <UUID> start <lang> [interim] [mono|mixed|stereo] [8k|16k] [metadata]
```

## Stereo with Separate Recognition Examples

### v1 API (uuid_google_transcribe)

```bash
# 1. Set separate recognition
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true

# 2. Optional: Set phone_call model for telephony
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call

# 3. Start stereo transcription with 8kHz
uuid_google_transcribe <UUID> start en-US interim stereo 8k

# 4. Stop when done
uuid_google_transcribe <UUID> stop
```

### v2 API (uuid_google_transcribe2) - Recommended

```bash
# 1. Set separate recognition
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true

# 2. Optional: Set phone_call model for telephony
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call

# 3. Start stereo transcription with 8kHz
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k

# 4. Stop when done
uuid_google_transcribe2 <UUID> stop
```

### With 16kHz HD Audio

```bash
# Set separate recognition
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true

# Start with 16kHz stereo
uuid_google_transcribe2 <UUID> start en-US interim stereo 16k
```

## All Available Channel Variables

Configure these **before** starting transcription:

### Core Settings

| Variable | Type | Description | Default | Example |
|----------|------|-------------|---------|---------|
| `GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL` | boolean | Separate transcription per channel | `false` | `true` |
| `GOOGLE_SPEECH_MODEL` | string | Recognition model | auto | `phone_call` |
| `GOOGLE_SPEECH_SAMPLE_RATE` | integer | Audio sample rate (Hz) | `8000` | `16000` |
| `GOOGLE_SPEECH_SINGLE_UTTERANCE` | boolean | Stop after first utterance | `false` | `true` |

### Enhanced Features

| Variable | Type | Description | Default | Example |
|----------|------|-------------|---------|---------|
| `GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS` | boolean | Include word timestamps | `false` | `true` |
| `GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION` | boolean | Add punctuation | `false` | `true` |
| `GOOGLE_SPEECH_PROFANITY_FILTER` | boolean | Filter profanity | `false` | `true` |
| `GOOGLE_SPEECH_MAX_ALTERNATIVES` | integer | Number of alternatives | `0` | `3` |
| `GOOGLE_SPEECH_HINTS` | string | Speech hints | none | `hello,world` |

## Complete Example: Stereo Conference Call

```xml
<extension name="conference_with_transcription">
  <condition field="destination_number" expression="^8888$">
    <action application="answer"/>
    <action application="sleep" data="1000"/>

    <!-- Configure transcription -->
    <action application="set" data="GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL=true"/>
    <action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>
    <action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
    <action application="set" data="GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS=true"/>

    <!-- Start stereo transcription (8kHz for telephony) -->
    <action application="uuid_google_transcribe2" data="${uuid} start en-US interim stereo 8k"/>

    <!-- Join conference -->
    <action application="conference" data="myroom@default"/>

    <!-- Stop transcription on exit -->
    <action application="uuid_google_transcribe2" data="${uuid} stop"/>
  </condition>
</extension>
```

## Testing via FreeSWITCH CLI

### Step-by-Step Test

```bash
# 1. Connect to FreeSWITCH CLI
fs_cli

# 2. Make or receive a call, get UUID
show channels

# 3. Enable separate recognition
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call

# 4. Start stereo transcription (v2 API recommended)
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k

# 5. Have a conversation...

# 6. Stop transcription
uuid_google_transcribe2 <UUID> stop
```

## Monitoring Stereo Transcription

### Watch Events

```bash
# In fs_cli
/event CUSTOM google_transcribe::transcription
/event CUSTOM google_transcribe::session_start
/event CUSTOM google_transcribe::session_stop
```

### Expected Event Output (Separate Recognition Enabled)

With separate recognition, you'll receive transcription events with **channel information** indicating which participant spoke:

```
Event-Name: CUSTOM
Event-Subclass: google_transcribe::transcription
transcription-vendor: google
channel: 0  ← Caller channel
transcript: Hello, how can I help you?
confidence: 0.95
```

```
Event-Name: CUSTOM
Event-Subclass: google_transcribe::transcription
transcription-vendor: google
channel: 1  ← Callee channel
transcript: I need assistance with my account
confidence: 0.92
```

### Log Monitoring

```bash
# Terminal 1: Watch transcription logs
sudo tail -f /var/log/freeswitch/freeswitch.log | grep -E "google|transcribe|stereo"

# Look for:
# - "start transcribing lang=en-US interim=yes mix=stereo rate=8000"
# - "separate_recognition enabled"
```

## Sample Rates for Stereo

| Sample Rate | Audio Quality | Use Case | Bandwidth |
|-------------|---------------|----------|-----------|
| 8kHz | Standard telephony | G.711, traditional phone lines | Low |
| 16kHz | HD Voice | Modern VoIP, better quality | Medium |
| 24kHz+ | High fidelity | Studio recordings | High |

**Recommendation:** Use 8kHz for standard telephony, 16kHz for HD VoIP.

## Troubleshooting

### Issue: Not getting separate channel results

**Check:**
1. Variable is set: `uuid_getvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL`
2. Using `stereo` mode in command (not `mono` or `mixed`)
3. Call actually has bidirectional audio (not just inbound)
4. For v2 API: Make sure Google Cloud Speech-to-Text API v2 is enabled

### Issue: Poor recognition quality

**Solutions:**
1. Use `phone_call` model: `uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call`
2. Enable punctuation: `uuid_setvar <UUID> GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION true`
3. Match sample rate to codec (8kHz for G.711, 16kHz for Opus/G.722)
4. Ensure good audio quality (low latency, no packet loss)

### Issue: High API costs

**Optimization:**
- Use 8kHz instead of 16kHz (lower data sent)
- Only transcribe when needed (don't start on answer, start when needed)
- Use v2 API which has better pricing for telephony with `phone_call` model

## API Version Differences (v1 vs v2)

### v1 API (uuid_google_transcribe):
- ✅ Separate recognition: Supported
- ✅ Stereo: Supported
- ✅ phone_call model: Supported
- ⚠️  Older API, may have lower accuracy

### v2 API (uuid_google_transcribe2):
- ✅ Separate recognition: Supported
- ✅ Stereo: Supported
- ✅ phone_call model: Supported (optimized for telephony)
- ✅ Better accuracy, especially for noisy environments
- ✅ **Recommended** for new deployments

## Performance Considerations

**Stereo with separate recognition:**
- Doubles the audio data sent to Google
- Creates two recognition streams (one per channel)
- Higher API costs (2x transcription)
- Better attribution (know who said what)

**Best for:**
- Call center quality monitoring
- Compliance recording with speaker identification
- Analytics requiring speaker attribution
- Customer service training

**Not needed for:**
- Simple call transcription (use mono)
- Single participant calls
- Cost-sensitive deployments

## Quick Reference Card

```bash
# Standard Stereo Setup (8kHz telephony)
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k

# HD Voice Stereo Setup (16kHz)
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call
uuid_google_transcribe2 <UUID> start en-US interim stereo 16k

# With All Features Enabled
uuid_setvar <UUID> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true
uuid_setvar <UUID> GOOGLE_SPEECH_MODEL phone_call
uuid_setvar <UUID> GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION true
uuid_setvar <UUID> GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS true
uuid_google_transcribe2 <UUID> start en-US interim stereo 8k

# Stop
uuid_google_transcribe2 <UUID> stop
```

## Related Documentation

- [Environment Variables Guide](mod_google_transcribe-environment-variables.md)
- [Sprint 2 Implementation Plan](mod_google_transcribe-sprint2-plan.md)
- [Alignment Plan](mod_google_transcribe-alignment-plan.md)

---

**Next Steps:**
1. Upload credentials file to `/home/sthorat/st-stt-v2.json`
2. Restart FreeSWITCH: `sudo systemctl restart freeswitch`
3. Test with examples above
4. Monitor logs and events for results
