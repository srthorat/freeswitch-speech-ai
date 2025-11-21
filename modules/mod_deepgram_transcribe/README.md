# mod_deepgram_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using Deepgram's streaming transcription API.

## Features

- Real-time streaming transcription via Deepgram API
- Speaker diarization to identify different speakers
- Keyword boosting for improved recognition of specific terms
- Named Entity Recognition (NER) for detecting entities like names, dates, amounts
- Profanity filtering
- Automatic punctuation and capitalization
- Redaction of sensitive information (PCI, SSN, etc.)
- Search for specific keywords in transcripts
- Custom word replacement
- Voice Activity Detection (VAD) with configurable endpointing
- Multiple model options (general, phonecall, meeting, voicemail, etc.)
- Numerals formatting (automatic conversion of spoken numbers)
- Interim and final transcription results

## Dependencies

- **libwebsockets** - Required for WebSocket connectivity to Deepgram API
- FreeSWITCH 1.8 or later

## Building

See the main [repository README](../../README.md) for complete build instructions. This module requires FreeSWITCH to be built with libwebsockets support.

The [ansible-role-fsmrf](https://github.com/drachtio/ansible-role-fsmrf) provides automated builds with all dependencies, or you can install libwebsockets manually:

```bash
apt-get install -y libwebsockets-dev
```

## API

### Commands

The freeswitch module exposes the following API commands:

```
uuid_deepgram_transcribe <uuid> start <lang-code> [interim] [stereo|mono]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid language code supported by Deepgram (e.g., en-US, en-GB, es, fr, de)
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned
- `stereo|mono` - Audio channel mode (optional, defaults to mono):
  - `mono` - Single channel with caller audio only (default)
  - `stereo` - Two channels with caller and callee audio on separate channels

```
uuid_deepgram_transcribe <uuid> stop
```
Stop transcription on the channel.

### Audio Channel Modes

**Mono Mode (Default)** - Captures caller audio only:
```
uuid_deepgram_transcribe <uuid> start en-US interim
uuid_deepgram_transcribe <uuid> start en-US interim mono
```

**Stereo Mode** - Captures both caller and callee on separate channels:
```
uuid_deepgram_transcribe <uuid> start en-US interim stereo
```

In stereo mode:
- Channel 0: Caller (inbound/read stream)
- Channel 1: Callee (outbound/write stream)
- Deepgram receives `multichannel=true&channels=2` parameter
- Useful for call centers, quality monitoring, and compliance recording
- Can be combined with speaker diarization for enhanced speaker separation

**Mixed Mode**: Not supported. For mixed audio, use mono mode with speaker diarization as an alternative.

### Channel Variables

The following channel variables can be set to configure the Deepgram transcription service. The module automatically reads these variables and constructs the Deepgram WebSocket API URL.

| Variable | Deepgram API Parameter | Values/Format | Default | Description |
| --- | --- | --- | --- | --- |
| **Authentication** |
| DEEPGRAM_API_KEY | Authorization header | API key string | none | Your Deepgram API key (required) |
| **Model Selection** |
| DEEPGRAM_SPEECH_MODEL | `model` | general, meeting, phonecall, voicemail, finance, conversationalai, video, medical | Auto-selected | Model optimized for use case |
| DEEPGRAM_SPEECH_TIER | `tier` | base, enhanced, nova, nova-2 | Auto-selected | Model quality tier |
| DEEPGRAM_SPEECH_MODEL_VERSION | `version` | Version string | latest | Specific model version |
| DEEPGRAM_SPEECH_CUSTOM_MODEL | `model` | Custom model ID | none | Your custom trained model |
| **Speaker & Entity Detection** |
| DEEPGRAM_SPEECH_DIARIZE | `diarize` | true/false | false | Identify different speakers |
| DEEPGRAM_SPEECH_DIARIZE_VERSION | `diarize_version` | Version string | latest | Diarization algorithm version |
| DEEPGRAM_SPEECH_NER | `ner` | true/false | false | Named Entity Recognition (names, dates, etc.) |
| **Text Formatting** |
| DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION | `punctuate` | true/false | false | Add punctuation automatically |
| DEEPGRAM_SPEECH_NUMERALS | `numerals` | true/false | false | Convert spoken numbers to digits |
| DEEPGRAM_SPEECH_ENABLE_SMART_FORMAT | `smart_format`, `no_delay` | true/false | false | Smart formatting with reduced latency |
| **Keywords & Search** |
| DEEPGRAM_SPEECH_KEYWORDS | `keywords` | word:intensity,... | none | Boost keywords (intensity 1-10) |
| DEEPGRAM_SPEECH_SEARCH | `search` | word1,word2,... | none | Search for specific keywords |
| **Privacy & Compliance** |
| DEEPGRAM_SPEECH_REDACT | `redact` | pci, ssn, numbers (comma-separated) | none | Redact sensitive information |
| DEEPGRAM_SPEECH_PROFANITY_FILTER | `profanity_filter` | true/false | false | Filter profanity from transcripts |
| DEEPGRAM_SPEECH_REPLACE | `replace` | find:replace,... | none | Find and replace terms |
| **Quality & Alternatives** |
| DEEPGRAM_SPEECH_ALTERNATIVES | `alternatives` | 1-10 | 1 | Number of alternative transcription hypotheses |
| **Voice Activity Detection** |
| DEEPGRAM_SPEECH_ENDPOINTING | `endpointing` | milliseconds | none | Silence duration to detect end of speech |
| DEEPGRAM_SPEECH_UTTERANCE_END_MS | `utterance_end_ms` | milliseconds | none | Utterance end detection threshold |
| DEEPGRAM_SPEECH_VAD_TURNOFF | `vad_turnoff` | milliseconds | none | Delay before turning off VAD |
| **Organization** |
| DEEPGRAM_SPEECH_TAG | `tag` | string | none | Custom tag for tracking/organization |

**Fixed Parameters** (automatically set by module):
- `encoding=linear16` - Audio encoding format (hardcoded)
- `sample_rate=8000` - Sample rate in Hz (hardcoded)
- `language=<lang-code>` - From command line parameter
- `multichannel=true&channels=2` - When `stereo` mode is used
- `interim_results=true` - When `interim` is specified

**Module Code Reference:** All variable handling is in `dg_transcribe_glue.cpp` lines 141-267.

## Model Options

Deepgram offers several pre-trained models optimized for different use cases:

| Model | Best For |
| --- | --- |
| **general** | General purpose transcription, versatile across domains |
| **meeting** | Meetings, conferences, multiple speakers |
| **phonecall** | Phone calls, optimized for telephony audio quality |
| **voicemail** | Voicemail messages, single speaker |
| **finance** | Financial services conversations |
| **conversationalai** | Chatbots, virtual assistants, conversational AI |
| **video** | Video content, media, entertainment |
| **medical** | Medical conversations, healthcare terminology |
| **custom** | Your custom trained model (use with DEEPGRAM_SPEECH_CUSTOM_MODEL) |

### Model Tiers

- **base** - Standard accuracy, fast processing
- **enhanced** - Improved accuracy over base
- **nova** - Deepgram's Nova model for highest accuracy
- **nova-2** - Latest version of Nova with further improvements

### Automatic Model/Tier Selection

If you don't specify `DEEPGRAM_SPEECH_MODEL` and `DEEPGRAM_SPEECH_TIER`, the module automatically selects optimal defaults based on the language code. Here are the automatic selections:

| Language | Code | Auto Tier | Auto Model | Notes |
|----------|------|-----------|------------|-------|
| **English (US)** | en, en-US | nova | phonecall | Best quality for phone calls |
| **English (Other)** | en-AU, en-GB, en-IN, en-NZ | nova | general | Best quality, general purpose |
| **Spanish** | es, es-419 | nova | general | High quality for Spanish |
| **Chinese** | zh, zh-CN, zh-TW | base | general | Standard quality |
| **Danish** | da | enhanced | general | Enhanced quality |
| **Dutch** | nl | enhanced | general | Enhanced quality |
| **French** | fr | enhanced | general | Enhanced quality |
| **French (Canada)** | fr-CA | base | general | Standard quality |
| **German** | de | enhanced | general | Enhanced quality |
| **Hindi** | hi | enhanced | general | Enhanced quality |
| **Hindi (Latin)** | hi-Latn | base | general | Standard quality |
| **Indonesian** | id | base | general | Standard quality |
| **Japanese** | ja | enhanced | general | Enhanced quality |
| **Korean** | ko | enhanced | general | Enhanced quality |
| **Norwegian** | no | enhanced | general | Enhanced quality |
| **Polish** | pl | enhanced | general | Enhanced quality |
| **Portuguese** | pt, pt-BR, pt-PT | enhanced | general | Enhanced quality |
| **Russian** | ru | base | general | Standard quality |
| **Swedish** | sv | enhanced | general | Enhanced quality |
| **Tamil** | ta | enhanced | general | Enhanced quality |
| **Turkish** | tr | base | general | Standard quality |
| **Ukrainian** | uk | base | general | Standard quality |

**How it works:**

```bash
# Without specifying model/tier - uses automatic defaults
uuid_deepgram_transcribe <uuid> start en-US interim
# → Automatically uses: tier=nova, model=phonecall

# Override with your own preferences
uuid_setvar <uuid> DEEPGRAM_SPEECH_MODEL meeting
uuid_setvar <uuid> DEEPGRAM_SPEECH_TIER nova-2
uuid_deepgram_transcribe <uuid> start en-US interim
# → Uses: tier=nova-2, model=meeting

# For unsupported languages, defaults to base/general
uuid_deepgram_transcribe <uuid> start ar interim
# → Falls back to: tier=base, model=general
```

**Code reference:** The automatic selection logic is in `dg_transcribe_glue.cpp` lines 41-73, 154-161.

## Authentication

Set your Deepgram API key either as a channel variable or environment variable:

| Variable | Description |
| --- | ----------- |
| DEEPGRAM_API_KEY | Your Deepgram API key |

Get an API key by signing up at [Deepgram Console](https://console.deepgram.com/).

## Events

### deepgram_transcribe::transcription

Returns an interim or final transcription. The event contains a JSON body describing the transcription result.

#### Basic Transcription

```json
{
  "channel_index": [0, 1],
  "duration": 4.59,
  "start": 0.0,
  "is_final": true,
  "speech_final": true,
  "channel": {
    "alternatives": [{
      "transcript": "Hello hello hello.",
      "confidence": 0.98583984,
      "words": [{
        "word": "hello",
        "start": 3.0865219,
        "end": 3.206,
        "confidence": 0.99902344
      }, {
        "word": "hello",
        "start": 3.5644348,
        "end": 3.644087,
        "confidence": 0.9741211
      }, {
        "word": "hello",
        "start": 4.042348,
        "end": 4.3609567,
        "confidence": 0.98583984
      }]
    }]
  },
  "metadata": {
    "request_id": "37835678-5d3b-4c77-910e-f8914c882cec",
    "model_info": {
      "name": "general",
      "version": "2024-01-18.29447",
      "tier": "base"
    },
    "model_uuid": "6b28e919-8427-4f32-9847-492e2efd7daf"
  }
}
```

#### With Speaker Diarization

When `DEEPGRAM_SPEECH_DIARIZE` is set to "true":

```json
{
  "channel_index": [0],
  "duration": 8.34,
  "start": 0.0,
  "is_final": true,
  "speech_final": true,
  "channel": {
    "alternatives": [{
      "transcript": "Hi, how are you? I'm doing great, thanks for asking.",
      "confidence": 0.9765,
      "words": [
        {
          "word": "hi",
          "start": 0.4,
          "end": 0.72,
          "confidence": 0.9921,
          "speaker": 0
        },
        {
          "word": "how",
          "start": 0.88,
          "end": 1.04,
          "confidence": 0.9856,
          "speaker": 0
        },
        {
          "word": "are",
          "start": 1.04,
          "end": 1.2,
          "confidence": 0.9892,
          "speaker": 0
        },
        {
          "word": "you",
          "start": 1.2,
          "end": 1.52,
          "confidence": 0.9934,
          "speaker": 0
        },
        {
          "word": "i'm",
          "start": 2.1,
          "end": 2.34,
          "confidence": 0.9678,
          "speaker": 1
        },
        {
          "word": "doing",
          "start": 2.34,
          "end": 2.58,
          "confidence": 0.9734,
          "speaker": 1
        },
        {
          "word": "great",
          "start": 2.58,
          "end": 2.98,
          "confidence": 0.9812,
          "speaker": 1
        },
        {
          "word": "thanks",
          "start": 3.22,
          "end": 3.54,
          "confidence": 0.9701,
          "speaker": 1
        },
        {
          "word": "for",
          "start": 3.54,
          "end": 3.7,
          "confidence": 0.9889,
          "speaker": 1
        },
        {
          "word": "asking",
          "start": 3.7,
          "end": 4.26,
          "confidence": 0.9765,
          "speaker": 1
        }
      ]
    }]
  },
  "metadata": {
    "request_id": "f9c8e7d6-a5b4-3c2d-1e0f-123456789abc",
    "model_info": {
      "name": "general",
      "version": "2024-01-18.29447",
      "tier": "nova"
    }
  }
}
```

#### With Named Entity Recognition (NER)

When `DEEPGRAM_SPEECH_NER` is set to "true":

```json
{
  "channel": {
    "alternatives": [{
      "transcript": "My name is John Smith and my number is 555-1234.",
      "confidence": 0.96,
      "words": [...],
      "entities": [
        {
          "label": "PERSON",
          "value": "John Smith",
          "start_word": 3,
          "end_word": 4
        },
        {
          "label": "PHONE_NUMBER",
          "value": "555-1234",
          "start_word": 8,
          "end_word": 8
        }
      ]
    }]
  }
}
```

### deepgram_transcribe::connect

Fired when the connection to Deepgram is successfully established.

### deepgram_transcribe::error

Fired when an error occurs during transcription. Contains error details in the event body.

## Usage

**Recommended Approach for Production:** Use per-user flag-based configuration with centralized settings in dialplan.

📖 **See:** [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

This approach provides:
- Clean user files (flags only)
- Centralized API key management in dialplan
- Easy per-user service control
- Works seamlessly with Audio Fork and Azure transcription

**Quick Start:**
- [Complete dialplan example](../../examples/freeswitch-config/dialplan/default.xml.complete-example)
- [User 1001 example (Deepgram enabled)](../../examples/freeswitch-config/directory/1001.xml.complete)

---

### Method 1: User Directory Configuration (Alternative)

Configure Deepgram settings per user by adding variables to user XML files. This is ideal when you want all calls from specific users to automatically have transcription capabilities.

**Edit user directory file** (e.g., `/usr/local/freeswitch/conf/directory/default/1000.xml`):

```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
      <param name="vm-password" value="1000"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="accountcode" value="1000"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1000"/>
      <variable name="effective_caller_id_number" value="1000"/>

      <!-- Deepgram Transcription Variables -->
      <variable name="DEEPGRAM_API_KEY" value="your-deepgram-api-key"/>
      <variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
      <variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
      <variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
      <variable name="DEEPGRAM_SPEECH_DIARIZE_VERSION" value="latest"/>
      <variable name="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION" value="true"/>
    </variables>
  </user>
</include>
```

**Reload directory configuration:**
```bash
fs_cli -x 'reloadxml'
```

**Start transcription on active call:**
```bash
# Get call UUID
fs_cli -x 'show calls'

# Start transcription (mono - caller only)
fs_cli -x 'uuid_deepgram_transcribe <uuid> start en-US interim'

# Or start transcription (stereo - both parties)
fs_cli -x 'uuid_deepgram_transcribe <uuid> start en-US interim stereo'
```

**Benefits:**
- Variables automatically inherited by all calls from that user
- No need to set variables per-call or in dialplan
- Centralized configuration per extension
- Easy to manage different settings for different users/departments

### Method 2: Using drachtio-fsmrf

When using [drachtio-fsmrf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.

```javascript
// Basic transcription (mono - caller only)
await ep.set({
  DEEPGRAM_API_KEY: 'your-api-key',
  DEEPGRAM_SPEECH_MODEL: 'phonecall'
});
ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim`);

// Stereo mode (both caller and callee on separate channels)
await ep.set({
  DEEPGRAM_API_KEY: 'your-api-key',
  DEEPGRAM_SPEECH_MODEL: 'phonecall',
  DEEPGRAM_SPEECH_TIER: 'nova'
});
ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo`);

// With speaker diarization and keyword boosting
await ep.set({
  DEEPGRAM_API_KEY: 'your-api-key',
  DEEPGRAM_SPEECH_MODEL: 'meeting',
  DEEPGRAM_SPEECH_TIER: 'nova',
  DEEPGRAM_SPEECH_DIARIZE: 'true',
  DEEPGRAM_SPEECH_KEYWORDS: 'pricing:3,discount:2,payment:2',
  DEEPGRAM_SPEECH_NER: 'true'
});
ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en interim`);

// Stop transcription
ep.api('uuid_deepgram_transcribe', `${ep.uuid} stop`);
```

### Method 3: Using FreeSWITCH Dialplan

**Recommended: Per-User Flag-Based Approach (Starts AFTER Answer)**

See: [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

```xml
<!-- In dialplan: Check flag and start transcription AFTER answer -->
<extension name="deepgram_conditional" continue="true">
  <condition field="${user_data(${caller_id_number}@${domain_name} var enable_deepgram)}" expression="^true$">
    <condition field="destination_number" expression="^(.+)$">
      <action application="log" data="INFO [DEEPGRAM] Authorized User ${caller_id_number} calling ${destination_number} -> Starting Deepgram"/>

      <!-- Set Deepgram configuration (centralized) -->
      <action application="set" data="DEEPGRAM_API_KEY=your-deepgram-api-key"/>
      <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
      <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>

      <!-- Start transcription AFTER call is answered (api_on_answer for API command) -->
      <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
      <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/>
    </condition>
  </condition>
</extension>
```

**Benefits:**
- Uses `user_data()` function for reliable flag checking (production-proven)
- Starts transcription AFTER call is answered (not during routing)
- User files contain only flags (`enable_deepgram=true`)
- API keys centralized in dialplan
- Works with Audio Fork and Azure transcription

---

**Legacy: Direct Application Usage**

**Mono mode (caller only):**
```xml
<extension name="deepgram_transcribe_mono_test">
  <condition field="destination_number" expression="^transcribe$">
    <action application="answer"/>
    <action application="set" data="DEEPGRAM_API_KEY=your-api-key"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
    <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
    <action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>
    <action application="set" data="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
    <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
    <action application="park"/>
  </condition>
</extension>
```

**Stereo mode (both caller and callee on separate channels):**
```xml
<extension name="deepgram_transcribe_stereo_test">
  <condition field="destination_number" expression="^(1\d{3})$">
    <action application="answer"/>
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
    <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
    <action application="uuid_deepgram_transcribe" data="start en-US interim stereo"/>
    <action application="bridge" data="user/${destination_number}"/>
  </condition>
</extension>
```

**Note:** Legacy examples start transcription during call setup. For production, use the recommended flag-based approach which starts AFTER answer.

---

## How the Module Constructs API Requests

Understanding how channel variables map to Deepgram API parameters helps you configure features correctly.

### URL Construction Process

When you run `uuid_deepgram_transcribe <uuid> start en-US interim stereo`, the module:

1. **Reads all channel variables** set for that call
2. **Builds the WebSocket URL** with query parameters
3. **Connects to Deepgram** with the complete feature set

### Example 1: Basic Transcription

**Configuration:**
```xml
<variable name="DEEPGRAM_API_KEY" value="abc123..."/>
```

**Command:**
```bash
uuid_deepgram_transcribe <uuid> start en-US interim
```

**Generated URL:**
```
wss://api.deepgram.com/v1/listen?tier=nova&model=phonecall&language=en-US&punctuate=true&interim_results=true&encoding=linear16&sample_rate=8000
```

**Headers:**
```
Authorization: Token abc123...
```

**Explanation:**
- `tier=nova&model=phonecall` - Auto-selected for en-US
- `language=en-US` - From command line
- `punctuate=true` - Default behavior
- `interim_results=true` - From `interim` parameter
- `encoding=linear16&sample_rate=8000` - Always set by module

### Example 2: Stereo with Diarization

**Configuration:**
```xml
<variable name="DEEPGRAM_API_KEY" value="abc123..."/>
<variable name="DEEPGRAM_SPEECH_MODEL" value="meeting"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="nova-2"/>
<variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
<variable name="DEEPGRAM_SPEECH_DIARIZE_VERSION" value="2024-01-01"/>
```

**Command:**
```bash
uuid_deepgram_transcribe <uuid> start en-US interim stereo
```

**Generated URL:**
```
wss://api.deepgram.com/v1/listen?tier=nova-2&model=meeting&language=en-US&multichannel=true&channels=2&punctuate=true&diarize=true&diarize_version=2024-01-01&interim_results=true&encoding=linear16&sample_rate=8000
```

**Explanation:**
- `tier=nova-2&model=meeting` - From channel variables (overrides auto-selection)
- `multichannel=true&channels=2` - From `stereo` parameter
- `diarize=true&diarize_version=2024-01-01` - From channel variables

### Example 3: Full Feature Set

**Configuration:**
```xml
<variable name="DEEPGRAM_API_KEY" value="abc123..."/>
<variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
<variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
<variable name="DEEPGRAM_SPEECH_NER" value="true"/>
<variable name="DEEPGRAM_SPEECH_KEYWORDS" value="pricing:5,discount:3"/>
<variable name="DEEPGRAM_SPEECH_SEARCH" value="refund,cancel"/>
<variable name="DEEPGRAM_SPEECH_REDACT" value="pci,ssn"/>
<variable name="DEEPGRAM_SPEECH_ALTERNATIVES" value="3"/>
<variable name="DEEPGRAM_SPEECH_NUMERALS" value="true"/>
<variable name="DEEPGRAM_SPEECH_TAG" value="sales-call-001"/>
```

**Command:**
```bash
uuid_deepgram_transcribe <uuid> start en-US interim stereo
```

**Generated URL:**
```
wss://api.deepgram.com/v1/listen?tier=nova&model=phonecall&language=en-US&multichannel=true&channels=2&punctuate=true&redact=pci%2Cssn&diarize=true&ner=true&alternatives=3&numerals=true&search=refund&search=cancel&keywords=pricing%3A5&keywords=discount%3A3&tag=sales-call-001&interim_results=true&encoding=linear16&sample_rate=8000
```

**Explanation:**
- All channel variables are converted to API parameters
- Comma-separated lists (keywords, search, redact) become multiple parameters
- Special characters are URL-encoded (`:` → `%3A`, `,` → `%2C`)
- The module handles all encoding automatically

### Example 4: PCI Compliance

**Configuration:**
```xml
<variable name="DEEPGRAM_API_KEY" value="abc123..."/>
<variable name="DEEPGRAM_SPEECH_MODEL" value="finance"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="enhanced"/>
<variable name="DEEPGRAM_SPEECH_REDACT" value="pci,ssn,numbers"/>
<variable name="DEEPGRAM_SPEECH_PROFANITY_FILTER" value="true"/>
<variable name="DEEPGRAM_SPEECH_REPLACE" value="card:payment,account:identifier"/>
```

**Command:**
```bash
uuid_deepgram_transcribe <uuid> start en-US interim
```

**Generated URL:**
```
wss://api.deepgram.com/v1/listen?tier=enhanced&model=finance&language=en-US&punctuate=true&profanity_filter=true&redact=pci%2Cssn%2Cnumbers&replace=card%3Apayment&replace=account%3Aidentifier&interim_results=true&encoding=linear16&sample_rate=8000
```

**Result:**
- Credit card numbers redacted: "My card is [REDACTED]"
- SSN redacted: "My SSN is [REDACTED]"
- Words replaced: "card" → "payment", "account" → "identifier"

### Variable Processing Details

**Boolean Variables:**
- Module uses `switch_true()` to evaluate: "true", "yes", "1", "on" → true
- Any other value → false

**Comma-Separated Lists:**
- `DEEPGRAM_SPEECH_KEYWORDS`: Each item becomes `&keywords=item`
- `DEEPGRAM_SPEECH_SEARCH`: Each item becomes `&search=item`
- `DEEPGRAM_SPEECH_REPLACE`: Each item becomes `&replace=item`
- Supports up to 500 items per variable

**URL Encoding:**
- Automatically encodes special characters
- Preserves: `!'()*-.0-9A-Za-z_~:`
- Encodes everything else as `%HH`

**Code Implementation:** See `dg_transcribe_glue.cpp`:
- URL construction: Lines 141-267
- URI encoding: Lines 121-139
- Boolean parsing: Lines 192-215

---

## Supported Languages

Deepgram supports many languages with varying model availability:

- **English**: en, en-US, en-GB, en-AU, en-NZ, en-IN
- **Spanish**: es, es-419 (Latin America)
- **French**: fr, fr-CA
- **German**: de
- **Portuguese**: pt, pt-BR
- **Italian**: it
- **Dutch**: nl
- **Japanese**: ja
- **Korean**: ko
- **Swedish**: sv
- **Turkish**: tr
- **Russian**: ru
- **Ukrainian**: uk
- **Hindi**: hi
- **Indonesian**: id
- **Chinese**: zh, zh-CN, zh-TW

For the complete and up-to-date list of supported languages and features per language, see [Deepgram Language Support](https://developers.deepgram.com/documentation/features/language/).

## Troubleshooting

### Authentication Issues

If you see authentication errors:
1. Verify your Deepgram API key is correct
2. Check that your API key has sufficient credits/quota
3. Ensure the API key hasn't expired

### No Transcription Results

If you're not receiving transcription events:
1. Check FreeSWITCH logs for connection errors
2. Verify the language code is supported
3. Ensure audio is being captured (check media bug attachment)
4. Verify network connectivity to Deepgram endpoints
5. Check your Deepgram account has available credits

### Speaker Diarization Not Working

If speaker labels are not appearing:
1. Ensure `DEEPGRAM_SPEECH_DIARIZE` is set to "true"
2. Speaker diarization requires sufficient audio with multiple speakers
3. Works best with clear audio and distinct speakers
4. Some languages may have limited diarization support

### Poor Transcription Quality

To improve transcription accuracy:
1. Use the appropriate model for your use case (phonecall, meeting, etc.)
2. Consider upgrading to enhanced or nova tier
3. Use keyword boosting for domain-specific terms: `DEEPGRAM_SPEECH_KEYWORDS: 'term1:3,term2:2'`
4. Ensure good audio quality (8kHz minimum, 16kHz recommended)
5. Enable automatic punctuation for better readability

### Keyword Boosting

Boost recognition of specific terms using intensifiers:
```javascript
DEEPGRAM_SPEECH_KEYWORDS: 'Freeswitch:3,VoIP:2,transcription:2'
```
Intensifiers range from -10 to 10, where higher values increase likelihood of detection.

### Connection Issues

If experiencing connection problems:
1. Verify libwebsockets is properly installed
2. Check firewall rules allow outbound WebSocket connections
3. Ensure DNS can resolve Deepgram endpoints
4. Check network latency - Deepgram has global endpoints

---

## Pusher Integration (Real-time Transcription Delivery)

mod_deepgram_transcribe includes built-in Pusher integration for delivering real-time transcriptions to your frontend applications via WebSocket.

### How It Works

When Pusher is configured, the module automatically:
1. Receives transcription from Deepgram API
2. Transforms the data to a standardized format
3. Maps speaker identity using caller/callee metadata
4. Sends the transformed data to Pusher in real-time
5. Your frontend receives immediate transcription updates

### Pusher Configuration

Set these environment variables when running FreeSWITCH:

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `PUSHER_APP_ID` | Yes | - | Your Pusher application ID |
| `PUSHER_KEY` | Yes | - | Your Pusher API key (public) |
| `PUSHER_SECRET` | Yes | - | Your Pusher secret key (for HMAC signing) |
| `PUSHER_CLUSTER` | No | `ap2` | Pusher cluster (us2, us3, eu, ap1, ap2, ap3, ap4) |
| `PUSHER_CHANNEL_PREFIX` | No | `call-` | Prefix for channel names |
| `PUSHER_EVENT_SESSION_START` | No | `session-start` | Event name for session start notification |
| `PUSHER_EVENT_FINAL` | No | `transcription-final` | Event name for final transcriptions |
| `PUSHER_EVENT_INTERIM` | No | `transcription-interim` | Event name for interim transcriptions |

### Docker Run with Pusher

**Deepgram + Pusher:**
```bash
docker run -d --name freeswitch \
  -p 5060:5060/udp -p 8021:8021 \
  -e DEEPGRAM_API_KEY=your-deepgram-api-key \
  -e PUSHER_APP_ID=123456 \
  -e PUSHER_KEY=your-pusher-key \
  -e PUSHER_SECRET=your-pusher-secret \
  -e PUSHER_CLUSTER=ap2 \
  freeswitch-speech-ai:latest
```

**All optional Pusher settings:**
```bash
docker run -d --name freeswitch \
  -p 5060:5060/udp -p 8021:8021 \
  -e DEEPGRAM_API_KEY=your-deepgram-api-key \
  -e PUSHER_APP_ID=123456 \
  -e PUSHER_KEY=your-pusher-key \
  -e PUSHER_SECRET=your-pusher-secret \
  -e PUSHER_CLUSTER=ap2 \
  -e PUSHER_CHANNEL_PREFIX=transcription- \
  -e PUSHER_EVENT_SESSION_START=session-start \
  -e PUSHER_EVENT_FINAL=final \
  -e PUSHER_EVENT_INTERIM=interim \
  freeswitch-speech-ai:latest
```

### Pusher Data Format

The module sends two types of messages to Pusher:

#### 1. Session Start Message

Sent when the call is answered and transcription session begins:

```json
{
  "type": "session_start",
  "caller_id": "John Doe(1000)",
  "callee_id": "Jane Smith(1002)",
  "timestamp": "2025-11-21T21:30:40Z"
}
```

**Fields:**
- `type`: Always `"session_start"`
- `caller_id`: `"{name}({number})"` - caller identity from channel variables
- `callee_id`: `"{name}({number})"` - callee identity from channel variables
- `timestamp`: ISO 8601 UTC timestamp when session started

#### 2. Transcription Messages

Sent for each transcription result (interim and final):

```json
{
  "type": "final",
  "speaker_id": "John Doe(1000)",
  "text": "Hello, how can I help you today?",
  "timestamp": "2025-11-21T21:30:45Z"
}
```

**Fields:**
- `type`: `"final"` or `"interim"` - transcription finality
- `speaker_id`: `"{name}({number})"` - speaker identity mapped from metadata
- `text`: The transcribed text (never empty - validated before sending)
- `timestamp`: ISO 8601 UTC timestamp

### Channel Naming

Pusher channels are automatically created based on the SIP Call-ID:
- Format: `{PUSHER_CHANNEL_PREFIX}{sip_call_id}`
- Example: `call-abc123-def456@domain.com`
- Default prefix: `call-`

### Frontend Integration

**JavaScript/TypeScript Example:**

```javascript
import Pusher from 'pusher-js';

// Initialize Pusher client
const pusher = new Pusher('your-pusher-key', {
  cluster: 'ap2'
});

// Subscribe to call channel (use SIP Call-ID from your call setup)
const callId = 'abc123-def456@domain.com';
const channel = pusher.subscribe(`call-${callId}`);

// Listen for session start (when call is answered)
channel.bind('session-start', (data) => {
  console.log(`Call session started: ${data.caller_id} -> ${data.callee_id}`);
  // Initialize UI, show participants, etc.
  initializeCallSession(data.caller_id, data.callee_id);
});

// Listen for final transcriptions
channel.bind('transcription-final', (data) => {
  console.log(`[${data.speaker_id}] ${data.text}`);
  // Display in UI - this is the final, accurate transcription
  addToTranscript(data.speaker_id, data.text, 'final');
});

// Listen for interim transcriptions (real-time updates)
channel.bind('transcription-interim', (data) => {
  console.log(`[${data.speaker_id}] (interim) ${data.text}`);
  // Update live preview - this may change
  updateLivePreview(data.speaker_id, data.text);
});

// Cleanup when call ends
function endCall() {
  channel.unbind_all();
  pusher.unsubscribe(`call-${callId}`);
}
```

**React Example:**

```jsx
import { useEffect, useState } from 'react';
import Pusher from 'pusher-js';

function TranscriptionDisplay({ callId }) {
  const [transcripts, setTranscripts] = useState([]);
  const [liveText, setLiveText] = useState('');
  const [sessionInfo, setSessionInfo] = useState(null);

  useEffect(() => {
    const pusher = new Pusher(process.env.REACT_APP_PUSHER_KEY, {
      cluster: 'ap2'
    });

    const channel = pusher.subscribe(`call-${callId}`);

    // Session start - show call participants
    channel.bind('session-start', (data) => {
      setSessionInfo({
        caller: data.caller_id,
        callee: data.callee_id,
        startTime: data.timestamp
      });
    });

    // Final transcriptions - add to permanent list
    channel.bind('transcription-final', (data) => {
      setTranscripts(prev => [...prev, {
        speaker: data.speaker_id,
        text: data.text,
        timestamp: data.timestamp,
        type: data.type
      }]);
      setLiveText(''); // Clear interim
    });

    // Interim transcriptions - show as live preview
    channel.bind('transcription-interim', (data) => {
      setLiveText(`${data.speaker_id}: ${data.text}`);
    });

    return () => {
      channel.unbind_all();
      pusher.unsubscribe(`call-${callId}`);
    };
  }, [callId]);

  return (
    <div>
      <h2>Transcription</h2>
      {/* Session info */}
      {sessionInfo && (
        <div className="session-info">
          <p><strong>Caller:</strong> {sessionInfo.caller}</p>
          <p><strong>Callee:</strong> {sessionInfo.callee}</p>
        </div>
      )}
      {/* Final transcripts */}
      <div className="transcripts">
        {transcripts.map((t, i) => (
          <div key={i} className="transcript-line">
            <strong>{t.speaker}</strong>: {t.text}
          </div>
        ))}
      </div>
      {/* Live interim preview */}
      {liveText && (
        <div className="interim-preview" style={{ opacity: 0.6 }}>
          {liveText}
        </div>
      )}
    </div>
  );
}
```

### Security Considerations

**Environment Variables (Recommended):**
- Store `PUSHER_SECRET` as environment variable only
- Never commit secrets to version control
- Use different Pusher apps for dev/staging/production

**Channel Permissions:**
- Pusher channels are public by default
- For private channels, implement server-side authorization
- Use Pusher's auth endpoint feature for sensitive data

**Best Practices:**
- Rotate Pusher secrets periodically
- Use Pusher's encrypted channels for sensitive transcriptions
- Implement call ID validation in your frontend
- Clean up subscriptions when calls end

### Troubleshooting Pusher Integration

**Issue: Transcriptions not appearing in frontend**

1. Check Pusher credentials are set:
   ```bash
   docker exec freeswitch env | grep PUSHER
   ```

2. Verify channel name format (check SIP Call-ID):
   ```bash
   docker logs freeswitch | grep "sip_call_id"
   ```

3. Enable Pusher debug logging in frontend:
   ```javascript
   Pusher.logToConsole = true;
   const pusher = new Pusher('key', { cluster: 'ap2' });
   ```

4. Check Pusher dashboard for event delivery stats

**Issue: Wrong speaker identification**

- Verify caller/callee metadata is set in dialplan
- Check that `sip_call_id` channel variable exists
- Ensure stereo mode is used for accurate speaker separation

**Issue: Delayed transcriptions**

- Check network latency to Pusher cluster
- Consider using a geographically closer cluster
- Verify FreeSWITCH server has good network connectivity

### Disabling Pusher

Pusher integration is **optional**. If Pusher credentials are not configured, the module:
- Continues to work normally
- Only sends transcriptions as FreeSWITCH events
- Does not attempt Pusher API calls
- Logs no errors about missing Pusher config

To disable, simply don't set `PUSHER_APP_ID`, `PUSHER_KEY`, or `PUSHER_SECRET`.

---

## Outbound Call Handling

When making outbound calls from FreeSWITCH (originating calls to external numbers), the channel mapping for speaker identification remains consistent with inbound calls:

### Channel Mapping - Based on Caller/Callee Roles

**IMPORTANT**: Channel assignment is based on **caller/callee roles**, NOT on which side is FreeSWITCH.

**Stereo Mode Channel Assignment:**
- **Channel 0** = Caller (whoever initiated/originated the call)
- **Channel 1** = Callee (whoever received/answered the call)

**This works automatically for both directions:**

**Inbound calls TO FreeSWITCH:**
- Channel 0 = External customer (the caller)
- Channel 1 = FreeSWITCH extension/agent (the callee)

**Outbound calls FROM FreeSWITCH:**
- Channel 0 = FreeSWITCH extension/agent (the caller)
- Channel 1 = External customer (the callee)

### Why No Swap Is Needed

FreeSWITCH automatically sets channel variables based on signaling roles:
- `caller_id_number` / `caller_id_name` = Always the caller (Channel 0)
- `destination_number` / `callee_id_name` = Always the callee (Channel 1)

The media bug stereo streams (READ/WRITE) naturally align with these same roles because the media bug is attached to the A-leg, where:
- **READ stream** = Audio from A-leg = Caller
- **WRITE stream** = Audio to A-leg (from B-leg) = Callee

**No manual swapping or special logic is required** - the module automatically extracts the correct speaker identities from FreeSWITCH channel variables.

### Dialplan Configuration for Outbound Calls

**Using `api_on_answer` for outbound calls:**

```xml
<extension name="outbound_call_with_transcription">
  <condition field="destination_number" expression="^9(\d{10})$">
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
    <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>

    <!-- Start transcription AFTER call is answered -->
    <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
    <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/>

    <!-- Bridge to external number -->
    <action application="bridge" data="sofia/external/1$1@sip-provider.com"/>
  </condition>
</extension>
```

**Using JavaScript/Lua for outbound calls:**

```javascript
// Using drachtio-fsmrf for outbound calls
const ms = await mrf.connect({...});
const ep = await ms.createEndpoint();

// Set transcription config before making call
await ep.set({
  DEEPGRAM_API_KEY: process.env.DEEPGRAM_API_KEY,
  DEEPGRAM_SPEECH_MODEL: 'phonecall',
  DEEPGRAM_SPEECH_TIER: 'nova'
});

// Make outbound call
await ep.execute('bridge', 'sofia/external/15551234567@provider.com');

// Start transcription after answer
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo`);
```

### Metadata and Speaker Identification for Outbound Calls

The module automatically extracts caller/callee information from FreeSWITCH channel variables:

**For Outbound Calls:**
- `caller_id_name` / `caller_id_number` → Channel 0 (FreeSWITCH/Originator)
- `destination_number` / `callee_id_name` → Channel 1 (External party/Destination)

**Example metadata for outbound call to +15551234567:**

```json
{
  "callerName": "Extension 1000",
  "callerNumber": "1000",
  "calleeName": "Unknown",
  "calleeNumber": "15551234567",
  "call-Id": "abc123-def456@domain.com"
}
```

### Pusher Integration for Outbound Calls

When using Pusher for real-time transcription delivery, outbound calls work identically to inbound calls:

**Transformed format sent to Pusher:**

```json
{
  "type": "final",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, this is John calling about...",
  "timestamp": "2025-11-21T10:30:45Z"
}
```

```json
{
  "type": "interim",
  "speaker_id": "Unknown(15551234567)",
  "text": "Hi John, how can I help you?",
  "timestamp": "2025-11-21T10:30:52Z"
}
```

**Key Points:**
- Speaker mapping uses `channel_index` ONLY (not AI speaker detection)
- Channel 0 always maps to caller (originator)
- Channel 1 always maps to callee (destination)
- Metadata is extracted automatically from channel variables
- Works with both inbound and outbound call scenarios

## Examples

See the examples directory for sample applications demonstrating Deepgram transcription with FreeSWITCH.
