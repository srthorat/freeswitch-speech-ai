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

The following channel variables can be set to configure the Deepgram transcription service:

| Variable | Description | Default |
| --- | ----------- | --- |
| DEEPGRAM_API_KEY | Deepgram API key for authentication (required) | none |
| DEEPGRAM_SPEECH_MODEL | Model to use: general, meeting, phonecall, voicemail, finance, conversationalai, video, medical, or custom | general |
| DEEPGRAM_SPEECH_MODEL_VERSION | Specific model version to use | latest |
| DEEPGRAM_SPEECH_TIER | Model tier: base, enhanced, nova, nova-2 | base |
| DEEPGRAM_SPEECH_CUSTOM_MODEL | Custom model ID (if using a custom trained model) | none |
| DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION | Enable automatic punctuation: true/false | true |
| DEEPGRAM_SPEECH_PROFANITY_FILTER | Filter profanity from transcripts: true/false | false |
| DEEPGRAM_SPEECH_REDACT | Redact sensitive info: pci, ssn, numbers, or combination (comma-separated) | none |
| DEEPGRAM_SPEECH_DIARIZE | Enable speaker diarization: true/false | false |
| DEEPGRAM_SPEECH_DIARIZE_VERSION | Diarization version to use | latest |
| DEEPGRAM_SPEECH_NER | Enable Named Entity Recognition: true/false | false |
| DEEPGRAM_SPEECH_ALTERNATIVES | Number of alternative transcription hypotheses to return (1-10) | 1 |
| DEEPGRAM_SPEECH_NUMERALS | Convert spoken numbers to numerals: true/false | false |
| DEEPGRAM_SPEECH_SEARCH | Keywords to search for in transcript (comma-separated) | none |
| DEEPGRAM_SPEECH_KEYWORDS | Keywords to boost with optional intensity: word:intensity,word2:intensity | none |
| DEEPGRAM_SPEECH_REPLACE | Find and replace terms: find1:replace1,find2:replace2 | none |
| DEEPGRAM_SPEECH_TAG | Custom tags for the request (for organization/tracking) | none |
| DEEPGRAM_SPEECH_ENDPOINTING | Time in milliseconds of silence to detect end of speech | none |
| DEEPGRAM_SPEECH_VAD_TURNOFF | Time in milliseconds to wait before turning off VAD | none |

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

### Method 1: User Directory Configuration

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

**Mono mode (caller only):**
```xml
<extension name="deepgram_transcribe_mono">
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
<extension name="deepgram_transcribe_stereo">
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

This stereo example will transcribe both parties during a bridged call, with caller on channel 0 and callee on channel 1.

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

## Examples

See the examples directory for sample applications demonstrating Deepgram transcription with FreeSWITCH.
