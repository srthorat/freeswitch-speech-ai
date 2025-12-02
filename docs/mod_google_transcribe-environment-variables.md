# mod_google_transcribe - Environment Variables Reference

Complete guide to configuring Google Speech features using FreeSWITCH channel variables.

## Table of Contents

- [Overview](#overview)
- [Command Syntax](#command-syntax)
- [Available Variables](#available-variables)
- [Usage Methods](#usage-methods)
- [Common Use Cases](#common-use-cases)
- [Best Practices](#best-practices)

---

## Overview

After the command syntax unification (Sprint 1), both `uuid_google_transcribe` and `uuid_google_transcribe2` use the same simplified AWS/Deepgram-style syntax. Advanced Google Speech features are now configured through **channel variables** (environment variables).

### Command Syntax

```bash
uuid_google_transcribe <uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]
uuid_google_transcribe2 <uuid> [start|stop] lang-code [interim] [mono|mixed|stereo] [8k|16k] [metadata]
```

**Differences:**
- `uuid_google_transcribe`: Defaults to Google Speech API **v1**
- `uuid_google_transcribe2`: Defaults to Google Speech API **v2**

---

## Available Variables

### Core Configuration

| Variable | Type | Description | Default | Example |
|----------|------|-------------|---------|---------|
| `GOOGLE_SPEECH_CLOUD_SERVICES_VERSION` | string | API version (`v1` or `v2`) | `v1` / `v2` | `v2` |
| `GOOGLE_SPEECH_SAMPLE_RATE` | integer | Audio sample rate (Hz) | `16000` | `8000`, `16000` |

### Boolean Features (set to "true" to enable)

| Variable | Description | Default | When to Use |
|----------|-------------|---------|-------------|
| `GOOGLE_SPEECH_SINGLE_UTTERANCE` | Stop after first utterance detected | `false` | Voice commands, IVR |
| `GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL` | Transcribe each channel separately (stereo) | `false` | Call center, agent/customer |
| `GOOGLE_SPEECH_PROFANITY_FILTER` | Filter profanity in results | `false` | Family-friendly content |
| `GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS` | Include word timestamps | `false` | Analytics, word alignment |
| `GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION` | Add punctuation automatically | `false` | Readability, transcripts |
| `GOOGLE_SPEECH_USE_ENHANCED` | Use enhanced model (premium) | `false` | High-accuracy requirements |

### Value-Based Features

| Variable | Type | Description | Default | Valid Values |
|----------|------|-------------|---------|--------------|
| `GOOGLE_SPEECH_MAX_ALTERNATIVES` | integer | Max alternative transcriptions | `1` | `1-30` |
| `GOOGLE_SPEECH_MODEL` | string | Speech model to use | `default` | See [Models](#speech-models) |
| `GOOGLE_SPEECH_HINTS` | string | Phrase hints (comma-separated) | none | `technical,jargon,keywords` |

### Speech Models

| Model | Description | Best For |
|-------|-------------|----------|
| `default` | General-purpose model | Generic transcription |
| `phone_call` | Optimized for telephony audio (8kHz-16kHz) | Contact centers, IVR |
| `video` | Optimized for video/meeting audio | Conferences, recordings |
| `command_and_search` | Short utterances, voice commands | IVR, voice assistants |

---

## Usage Methods

### Method 1: FreeSWITCH Dialplan (XML)

Set channel variables **before** starting transcription:

```xml
<extension name="transcribe_with_features">
  <condition field="destination_number" expression="^1234$">
    <!-- Configure Google Speech features -->
    <action application="set" data="GOOGLE_SPEECH_CLOUD_SERVICES_VERSION=v2"/>
    <action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>
    <action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
    <action application="set" data="GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS=true"/>
    <action application="set" data="GOOGLE_SPEECH_PROFANITY_FILTER=true"/>
    <action application="set" data="GOOGLE_SPEECH_MAX_ALTERNATIVES=3"/>
    <action application="set" data="GOOGLE_SPEECH_HINTS=customer,support,technical"/>

    <!-- Answer call -->
    <action application="answer"/>

    <!-- Start transcription with unified syntax -->
    <action application="uuid_google_transcribe" data="${uuid} start en-US interim stereo 16k"/>

    <!-- Your call flow -->
    <action application="park"/>
  </condition>
</extension>
```

### Method 2: ESL/API Commands

```bash
# Set channel variables first
uuid_setvar <uuid> GOOGLE_SPEECH_CLOUD_SERVICES_VERSION v2
uuid_setvar <uuid> GOOGLE_SPEECH_MODEL phone_call
uuid_setvar <uuid> GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION true
uuid_setvar <uuid> GOOGLE_SPEECH_PROFANITY_FILTER true
uuid_setvar <uuid> GOOGLE_SPEECH_MAX_ALTERNATIVES 3
uuid_setvar <uuid> GOOGLE_SPEECH_HINTS customer,support,technical

# Then start transcription
uuid_google_transcribe <uuid> start en-US interim stereo 16k
```

### Method 3: JavaScript (mod_v8)

```javascript
// Set channel variables
session.setVariable("GOOGLE_SPEECH_CLOUD_SERVICES_VERSION", "v2");
session.setVariable("GOOGLE_SPEECH_MODEL", "phone_call");
session.setVariable("GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION", "true");
session.setVariable("GOOGLE_SPEECH_PROFANITY_FILTER", "true");
session.setVariable("GOOGLE_SPEECH_MAX_ALTERNATIVES", "3");
session.setVariable("GOOGLE_SPEECH_HINTS", "customer,support,technical");

// Start transcription
session.execute("uuid_google_transcribe",
  session.uuid + " start en-US interim stereo 16k");
```

### Method 4: Python (mod_python)

```python
# Set channel variables
session.setVariable("GOOGLE_SPEECH_CLOUD_SERVICES_VERSION", "v2")
session.setVariable("GOOGLE_SPEECH_MODEL", "phone_call")
session.setVariable("GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION", "true")
session.setVariable("GOOGLE_SPEECH_PROFANITY_FILTER", "true")
session.setVariable("GOOGLE_SPEECH_MAX_ALTERNATIVES", "3")
session.setVariable("GOOGLE_SPEECH_HINTS", "customer,support,technical")

# Start transcription
session.execute("uuid_google_transcribe",
  session.uuid + " start en-US interim stereo 16k")
```

### Method 5: Lua (mod_lua)

```lua
-- Set channel variables
session:setVariable("GOOGLE_SPEECH_CLOUD_SERVICES_VERSION", "v2")
session:setVariable("GOOGLE_SPEECH_MODEL", "phone_call")
session:setVariable("GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION", "true")
session:setVariable("GOOGLE_SPEECH_PROFANITY_FILTER", "true")
session:setVariable("GOOGLE_SPEECH_MAX_ALTERNATIVES", "3")
session:setVariable("GOOGLE_SPEECH_HINTS", "customer,support,technical")

-- Start transcription
session:execute("uuid_google_transcribe",
  session:getVariable("uuid") .. " start en-US interim stereo 16k")
```

---

## Common Use Cases

### Use Case 1: Contact Center (High Quality)

**Scenario:** Call center with agent/customer on separate channels, need high accuracy.

```xml
<action application="set" data="GOOGLE_SPEECH_CLOUD_SERVICES_VERSION=v2"/>
<action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>
<action application="set" data="GOOGLE_SPEECH_USE_ENHANCED=true"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS=true"/>
<action application="set" data="GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL=true"/>
<action application="uuid_google_transcribe2" data="${uuid} start en-US interim stereo 16k"/>
```

**Why:**
- `phone_call` model: Optimized for telephony audio
- `enhanced=true`: Premium model for best accuracy (costs more)
- `punctuation=true`: Makes transcripts readable
- `word_time_offsets=true`: Enables analytics/sentiment analysis
- `separate_recognition=true`: Agent and customer transcribed separately
- `stereo`: Separate channels for agent/customer

---

### Use Case 2: Voice Commands / IVR

**Scenario:** Voice assistant, expecting short commands.

```xml
<action application="set" data="GOOGLE_SPEECH_MODEL=command_and_search"/>
<action application="set" data="GOOGLE_SPEECH_SINGLE_UTTERANCE=true"/>
<action application="set" data="GOOGLE_SPEECH_MAX_ALTERNATIVES=5"/>
<action application="set" data="GOOGLE_SPEECH_HINTS=play,pause,stop,volume,next,previous,help"/>
<action application="uuid_google_transcribe" data="${uuid} start en-US"/>
```

**Why:**
- `command_and_search` model: Optimized for short utterances
- `single_utterance=true`: Stops after first command
- `max_alternatives=5`: Get multiple options for matching
- `hints`: Domain-specific vocabulary (voice commands)

---

### Use Case 3: Video/Meeting Transcription

**Scenario:** Recording a video conference or meeting.

```xml
<action application="set" data="GOOGLE_SPEECH_CLOUD_SERVICES_VERSION=v2"/>
<action application="set" data="GOOGLE_SPEECH_MODEL=video"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
<action application="set" data="GOOGLE_SPEECH_MAX_ALTERNATIVES=2"/>
<action application="uuid_google_transcribe2" data="${uuid} start en-US interim mixed 16k"/>
```

**Why:**
- `video` model: Optimized for meeting/video audio
- `punctuation=true`: Readable meeting minutes
- `mixed`: All participants on one channel
- `max_alternatives=2`: Backup options for ambiguous speech

---

### Use Case 4: Family-Friendly Content

**Scenario:** Public service, need to filter inappropriate language.

```xml
<action application="set" data="GOOGLE_SPEECH_PROFANITY_FILTER=true"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
<action application="uuid_google_transcribe" data="${uuid} start en-US interim"/>
```

**Why:**
- `profanity_filter=true`: Masks inappropriate words
- `punctuation=true`: Professional appearance

---

### Use Case 5: Medical/Legal Transcription

**Scenario:** High accuracy required for specialized vocabulary.

```xml
<action application="set" data="GOOGLE_SPEECH_CLOUD_SERVICES_VERSION=v2"/>
<action application="set" data="GOOGLE_SPEECH_USE_ENHANCED=true"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS=true"/>
<action application="set" data="GOOGLE_SPEECH_MAX_ALTERNATIVES=3"/>
<action application="set" data="GOOGLE_SPEECH_HINTS=diagnosis,prescription,medical,legal,contract,plaintiff"/>
<action application="uuid_google_transcribe2" data="${uuid} start en-US interim stereo 16k"/>
```

**Why:**
- `enhanced=true`: Highest accuracy (worth the premium)
- `hints`: Industry-specific terminology
- `max_alternatives=3`: Options for review
- `word_time_offsets=true`: Audit trail

---

### Use Case 6: Budget-Conscious Basic Transcription

**Scenario:** Cost-effective transcription for general use.

```xml
<!-- Use defaults, no enhanced model -->
<action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>
<action application="uuid_google_transcribe" data="${uuid} start en-US interim"/>
```

**Why:**
- Minimal configuration
- Standard model (no premium costs)
- Basic phone_call optimization

---

## Best Practices

### 1. Always Set Variables Before Starting Transcription

❌ **Wrong:**
```bash
uuid_google_transcribe <uuid> start en-US interim
uuid_setvar <uuid> GOOGLE_SPEECH_MODEL phone_call  # Too late!
```

✅ **Correct:**
```bash
uuid_setvar <uuid> GOOGLE_SPEECH_MODEL phone_call
uuid_google_transcribe <uuid> start en-US interim
```

### 2. Use Appropriate Models for Audio Type

| Audio Type | Recommended Model |
|------------|-------------------|
| Phone calls (8kHz-16kHz) | `phone_call` |
| Video conferences | `video` |
| Voice commands | `command_and_search` |
| General audio | `default` |

### 3. Balance Cost vs. Quality

| Feature | Impact | Cost |
|---------|--------|------|
| Standard model | Good accuracy | Standard pricing |
| Enhanced model (`USE_ENHANCED=true`) | Best accuracy | 40% more expensive |
| Word time offsets | Timestamps | No extra cost |
| Punctuation | Readability | No extra cost |
| Max alternatives | More options | Slight increase (more data) |

### 4. Use Hints for Domain-Specific Terms

Improves accuracy for:
- Technical jargon
- Product names
- Industry terms
- Proper nouns

```xml
<!-- E-commerce -->
<action application="set" data="GOOGLE_SPEECH_HINTS=checkout,cart,product,order,shipping"/>

<!-- Healthcare -->
<action application="set" data="GOOGLE_SPEECH_HINTS=appointment,doctor,prescription,medication"/>

<!-- Finance -->
<action application="set" data="GOOGLE_SPEECH_HINTS=account,balance,transfer,payment,deposit"/>
```

### 5. Enable Punctuation for Readability

**Without punctuation:**
```
hello i would like to order a product from your website can you help me with that
```

**With punctuation:**
```
Hello, I would like to order a product from your website. Can you help me with that?
```

### 6. Use Stereo Mode for Call Centers

```bash
# Stereo with separate recognition per channel
uuid_setvar <uuid> GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true
uuid_google_transcribe <uuid> start en-US interim stereo 16k
```

Benefits:
- Agent transcribed separately from customer
- Better speaker attribution
- Improved analytics (sentiment per speaker)

### 7. Sample Rate Guidelines

| Audio Quality | Sample Rate | When to Use |
|---------------|-------------|-------------|
| Telephony | 8kHz | Traditional phone lines |
| HD Voice | 16kHz | Modern VoIP, default |
| High Quality | 24kHz+ | Studio recordings (rarely needed) |

**Default is 16kHz** - works well for most use cases.

---

## Variable Precedence

Configuration is applied in this order (first match wins):

1. **Command parameters** (e.g., `stereo`, `16k`)
2. **Channel variables** (`GOOGLE_SPEECH_*`)
3. **Default values** (hardcoded)

Example:
```bash
# Sample rate from command takes precedence
uuid_setvar <uuid> GOOGLE_SPEECH_SAMPLE_RATE 8000  # Ignored
uuid_google_transcribe <uuid> start en-US interim mono 16k  # 16kHz used
```

---

## Troubleshooting

### Issue: Variables Not Taking Effect

**Cause:** Variables set after transcription started.

**Solution:** Set variables BEFORE calling `uuid_google_transcribe`.

### Issue: Model Not Recognized

**Cause:** Invalid model name.

**Solution:** Use valid model names: `default`, `phone_call`, `video`, `command_and_search`.

### Issue: Enhanced Model Errors

**Cause:** Enhanced model not available for your region/language.

**Solution:** Check Google Cloud Speech API documentation for language support.

### Issue: Hints Not Improving Accuracy

**Cause:** Too many hints or wrong format.

**Solution:**
- Limit to 20-30 most important phrases
- Use comma-separated format (no spaces)
- Match expected vocabulary

---

## References

- [mod_google_transcribe README](../modules/mod_google_transcribe/README.md)
- [Alignment Plan](./mod_google_transcribe-alignment-plan.md)
- [TODO Checklist](./mod_google_transcribe-TODO.md)
- [Google Cloud Speech API Documentation](https://cloud.google.com/speech-to-text/docs)

---

**Last Updated:** Sprint 1 Complete
**Module Version:** Unified syntax (both commands)
**API Support:** Google Speech API v1 and v2
