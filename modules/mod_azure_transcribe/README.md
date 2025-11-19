# mod_azure_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using the Microsoft Azure Cognitive Services Speech-to-Text streaming API.

## Features

- Real-time streaming transcription via Azure Speech Services
- Profanity filtering with multiple modes (masked, removed, raw)
- Detailed output format with N-best alternatives and confidence scores
- Signal-to-noise ratio (SNR) reporting
- Speech hints for improved recognition of specific phrases
- Configurable timeout settings
- Support for multiple languages and dialects
- Interim and final transcription results

## Dependencies

- **libwebsockets** - Required for WebSocket connectivity to Azure Speech Services
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
uuid_azure_transcribe <uuid> start <lang-code> [interim] [stereo|mono] [bugname]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid Azure [language code](https://docs.microsoft.com/en-us/azure/cognitive-services/speech-service/language-support) that is supported for streaming transcription (e.g., en-US, es-ES, fr-FR)
- `interim` - (optional) If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned
- `stereo|mono` - (optional) Capture mode: 'stereo' captures both caller (channel 0) and callee (channel 1) separately; 'mono' captures mixed audio (default: mono)
- `bugname` - (optional) Custom name for the media bug (default: azure_transcribe)

```
uuid_azure_transcribe <uuid> stop [bugname]
```
Stop transcription on the channel.

**Stereo Mode and Channel Identification:**

When using `stereo` mode, the module automatically uses Azure's **ConversationTranscriber** API instead of SpeechRecognizer. This provides proper channel identification in transcription results:

- **Mono mode** (default): Uses `SpeechRecognizer` - all transcriptions show `"Channel":0`
- **Stereo mode**: Uses `ConversationTranscriber` - transcriptions include correct channel number:
  - `"Channel":0` = Caller (the person who initiated the call)
  - `"Channel":1` = Callee (the person who received the call)

**Important stereo mode limitations:**
- `AZURE_SPEECH_HINTS` is not supported with ConversationTranscriber (stereo mode only)
- `AZURE_SPEECH_ALTERNATIVE_LANGUAGE_CODES` is not supported in stereo mode
- Speech start/end detection events are not available in stereo mode

**Example stereo transcription result:**
```json
{
  "Id":"552502b2ed704e48940207fbe64ff3fe",
  "RecognitionStatus":"Success",
  "DisplayText":"Hello.",
  "Offset":241100000,
  "Duration":4400000,
  "Channel":1
}
```

### Advanced Features

**Speaker Diarization**

Azure Speech Services provides speaker diarization using `ConversationTranscriber`, which separates speakers in audio (e.g., "Guest-1", "Guest-2") using AI-based speaker recognition.

**Important Notes:**
- **Not Channel-Based**: Azure's streaming SDK doesn't split recognition by audio channels. Instead, it uses speaker diarization to identify different speakers in the conversation, even from mono or mixed audio.
- **Preview Feature**: Speaker diarization is a preview feature. For production use or access to advanced diarization capabilities, you may need to request access by emailing `diarizationrequest@microsoft.com`.
- **ConversationTranscriber API**: Our implementation uses `ConversationTranscriber` (stereo mode) which provides speaker identification in the transcription results.
- **Alternative**: For true channel-based separation, Azure's [Batch Transcription API](https://docs.microsoft.com/azure/cognitive-services/speech-service/batch-transcription) supports multi-channel audio, but not in real-time streaming.

**Configuration:**

```xml
<action application="set" data="AZURE_DIARIZE_INTERIM_RESULTS=true"/>
<action application="set" data="AZURE_DIARIZATION_SPEAKER_COUNT=2"/>
<action application="set" data="AZURE_DIARIZATION_MIN_SPEAKER_COUNT=1"/>
<action application="set" data="AZURE_DIARIZATION_MAX_SPEAKER_COUNT=2"/>
```

**Example Output:**
```json
{
  "Type": "ConversationTranscription",
  "SpeakerId": "Guest-1",
  "Channel": 0,
  "DisplayText": "Hello, how are you?"
}
```

These settings help Azure better identify different speakers in the conversation. The speaker identification (e.g., "Guest-1", "Guest-2") appears in transcription results.

**References:**
- [GitHub Issue #1485](https://github.com/Azure-Samples/cognitive-services-speech-sdk/issues/1485) - Streaming SDK doesn't split by channels
- [GitHub Issue #1748](https://github.com/Azure-Samples/cognitive-services-speech-sdk/issues/1748) - Speaker diarization preview feature

**Word-Level Timestamps**

Enable detailed timing information for each word in the transcription:

```xml
<action application="set" data="AZURE_WORD_LEVEL_TIMESTAMPS=true"/>
```

When enabled with detailed output format, you'll get offset and duration for each word.

**Sentiment Analysis**

Enable sentiment analysis to understand the emotional tone of transcribed speech:

```xml
<action application="set" data="AZURE_SENTIMENT_ANALYSIS=true"/>
```

Sentiment scores will be included in transcription results when available.

**Dictation Mode**

Enable dictation mode for better punctuation and formatting:

```xml
<action application="set" data="AZURE_DICTATION_MODE=true"/>
```

This mode is optimized for dictation scenarios with improved punctuation insertion.

### Channel Variables

The following channel variables can be set to configure the Azure Speech-to-Text service:

| Variable | Description | Default |
| --- | ----------- | --- |
| AZURE_SUBSCRIPTION_KEY | Azure Speech Services subscription key (required for authentication) | none |
| AZURE_REGION | Azure region where the Speech service is deployed (e.g., eastus, westus2) | none |
| AZURE_PROFANITY_OPTION | Profanity filtering mode: "masked", "removed", or "raw" | raw |
| AZURE_REQUEST_SNR | If set to "1" or "true", enables signal-to-noise ratio reporting | off |
| AZURE_INITIAL_SPEECH_TIMEOUT_MS | Initial time to wait for speech before returning no match (milliseconds) | none |
| AZURE_SPEECH_HINTS | Comma-separated list of phrases or words to expect for improved recognition (mono mode only) | none |
| AZURE_USE_OUTPUT_FORMAT_DETAILED | If set to "true" or "1", provides N-best alternatives and confidence levels | off |
| AZURE_DIARIZE_INTERIM_RESULTS | If set to "true" or "1", enables speaker identification in interim results (stereo mode only) | true |
| AZURE_DIARIZATION_SPEAKER_COUNT | Exact number of speakers expected in the conversation (stereo mode only) | 2 |
| AZURE_DIARIZATION_MIN_SPEAKER_COUNT | Minimum number of speakers in the conversation (stereo mode only) | 1 |
| AZURE_DIARIZATION_MAX_SPEAKER_COUNT | Maximum number of speakers in the conversation (stereo mode only) | 2 |
| AZURE_WORD_LEVEL_TIMESTAMPS | If set to "true" or "1", provides word-level timing information | off |
| AZURE_SENTIMENT_ANALYSIS | If set to "true" or "1", enables sentiment analysis for transcribed text | off |
| AZURE_DICTATION_MODE | If set to "true" or "1", enables dictation mode for better punctuation and formatting | off |

## Authentication

The plugin will first look for channel variables, then environment variables.

The names of the channel variables and environment variables for authentication are:

| Variable | Description |
| --- | ----------- |
| AZURE_SUBSCRIPTION_KEY | The Azure subscription key for Speech Services |
| AZURE_REGION | The Azure region (e.g., eastus, westus2, westeurope) |

### Setting up Azure Authentication

1. Create an Azure Cognitive Services Speech resource in the [Azure Portal](https://portal.azure.com)
2. Navigate to your Speech resource and copy the subscription key and region
3. Set the credentials either as:
   - Channel variables in your FreeSWITCH dialplan
   - Environment variables on your FreeSWITCH server

## Events

### azure_transcribe::transcription

Returns an interim or final transcription. The event contains a JSON body describing the transcription result.

#### Simple Output Format (default)

When `AZURE_USE_OUTPUT_FORMAT_DETAILED` is not set, the event contains basic transcription:

```json
{
  "Id": "1708f0bffc2d4d66b8347280447e9dde",
  "RecognitionStatus": "Success",
  "DisplayText": "This is a test.",
  "Offset": 14400000,
  "Duration": 12200000
}
```

The `RecognitionStatus` field indicates the result:
- `Success` - Final transcript with recognized speech
- `NoMatch` - No speech could be recognized
- `InitialSilenceTimeout` - No speech detected within the timeout period
- `BabbleTimeout` - Only noise detected, no clear speech
- `Error` - An error occurred during recognition

If the body contains `"RecognitionStatus": "Success"`, it is a final transcript; otherwise it is an interim transcript or error.

#### Detailed Output Format

When `AZURE_USE_OUTPUT_FORMAT_DETAILED` is set to "true", the event includes N-best alternatives with confidence scores and word-level details:

```json
{
  "Id": "2f45a3c8-9e6d-4a12-b345-789abc012def",
  "RecognitionStatus": "Success",
  "Offset": 14400000,
  "Duration": 18500000,
  "SNR": 38.4,
  "DisplayText": "What's the weather like today?",
  "NBest": [
    {
      "Confidence": 0.9521,
      "Lexical": "whats the weather like today",
      "ITN": "what's the weather like today",
      "MaskedITN": "what's the weather like today",
      "Display": "What's the weather like today?",
      "Words": [
        {
          "Word": "what's",
          "Offset": 14400000,
          "Duration": 3200000
        },
        {
          "Word": "the",
          "Offset": 17600000,
          "Duration": 800000
        },
        {
          "Word": "weather",
          "Offset": 18400000,
          "Duration": 4000000
        },
        {
          "Word": "like",
          "Offset": 22400000,
          "Duration": 2400000
        },
        {
          "Word": "today",
          "Offset": 24800000,
          "Duration": 8100000
        }
      ]
    },
    {
      "Confidence": 0.8934,
      "Lexical": "whats the weather light today",
      "ITN": "what's the weather light today",
      "MaskedITN": "what's the weather light today",
      "Display": "What's the weather light today?"
    }
  ]
}
```

When `AZURE_REQUEST_SNR` is enabled, the SNR (Signal-to-Noise Ratio) field will be included.

### azure_transcribe::connect

Fired when the connection to Azure Speech Services is successfully established.

### azure_transcribe::error

Fired when an error occurs during transcription. Contains error details in the event body.

## Usage

**Recommended Approach for Production:** Use per-user flag-based configuration with centralized settings in dialplan.

📖 **See:** [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

This approach provides:
- Clean user files (flags only)
- Centralized Azure credentials management in dialplan
- Easy per-user service control
- Works seamlessly with Audio Fork and Deepgram transcription

**Quick Start:**
- [Complete dialplan example](../../examples/freeswitch-config/dialplan/default.xml)
- [User 1002 example (Azure enabled)](../../examples/freeswitch-config/directory/1002.xml)

**Docker Users:** The Azure Docker image (`freeswitch-mod-azure-transcribe:latest`) includes these configuration files pre-installed and ready to use. See [Docker documentation](../../dockerfiles/README.md#mod_azure_transcribe) for details.

---

### Using drachtio-fsmrf

When using [drachtio-fsmrf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.

```javascript
// Basic transcription with stereo audio
await ep.set({
  AZURE_SUBSCRIPTION_KEY: 'your-subscription-key',
  AZURE_REGION: 'eastus'
});
ep.api('uuid_azure_transcribe', `${ep.uuid} start en-US interim stereo`);

// With detailed output and profanity filtering
await ep.set({
  AZURE_SUBSCRIPTION_KEY: 'your-subscription-key',
  AZURE_REGION: 'eastus',
  AZURE_USE_OUTPUT_FORMAT_DETAILED: 'true',
  AZURE_PROFANITY_OPTION: 'masked',
  AZURE_SPEECH_HINTS: 'weather,forecast,temperature'
});
ep.api('uuid_azure_transcribe', `${ep.uuid} start en-US interim stereo`);

// Stop transcription
ep.api('uuid_azure_transcribe', `${ep.uuid} stop`);
```

### Using FreeSWITCH Dialplan

**Recommended: Per-User Flag-Based Approach**

See: [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

```xml
<!-- In dialplan: Check flag and start transcription AFTER answer -->
<extension name="azure_conditional" continue="true">
  <condition field="${user_data(${caller_id_number}@${domain_name} var enable_azure)}" expression="^true$">
    <condition field="destination_number" expression="^(.+)$">
      <action application="log" data="INFO [AZURE] Authorized User ${caller_id_number} calling ${destination_number} -> Starting Azure"/>

      <!-- Set Azure configuration (centralized) -->
      <action application="set" data="AZURE_SUBSCRIPTION_KEY=your-azure-key"/>
      <action application="set" data="AZURE_REGION=eastus"/>

      <!-- Start transcription AFTER call is answered (api_on_answer for API command) -->
      <action application="set" data="api_on_answer=uuid_azure_transcribe ${uuid} start en-US interim stereo"/>
      <action application="set" data="api_hangup_hook=uuid_azure_transcribe ${uuid} stop"/>
    </condition>
  </condition>
</extension>
```

**Benefits:**
- Uses `user_data()` function for reliable flag checking (production-proven)
- Starts transcription AFTER call is answered (not during routing)
- Stereo mode properly identifies caller (Channel 0) vs callee (Channel 1) using ConversationTranscriber
- User files contain only flags (`enable_azure=true`)
- Azure credentials centralized in dialplan
- Works with Audio Fork and Deepgram transcription

---

**Legacy: Direct Application Usage**

```xml
<extension name="azure_transcribe_test">
  <condition field="destination_number" expression="^transcribe$">
    <action application="answer"/>
    <action application="set" data="AZURE_SUBSCRIPTION_KEY=your-subscription-key"/>
    <action application="set" data="AZURE_REGION=eastus"/>
    <action application="set" data="AZURE_USE_OUTPUT_FORMAT_DETAILED=true"/>
    <action application="set" data="AZURE_PROFANITY_OPTION=masked"/>
    <action application="azure_transcribe" data="start en-US interim"/>
    <action application="park"/>
  </condition>
</extension>
```

## Supported Languages

Azure Speech Services supports a wide range of languages. Some common ones include:

- **English**: en-US, en-GB, en-AU, en-CA, en-IN, en-NZ
- **Spanish**: es-ES, es-MX, es-AR, es-CO, es-US
- **French**: fr-FR, fr-CA, fr-BE, fr-CH
- **German**: de-DE, de-AT, de-CH
- **Italian**: it-IT
- **Portuguese**: pt-BR, pt-PT
- **Chinese**: zh-CN, zh-HK, zh-TW
- **Japanese**: ja-JP
- **Korean**: ko-KR
- **Arabic**: ar-EG, ar-SA, ar-AE
- **Hindi**: hi-IN
- **Russian**: ru-RU
- **Dutch**: nl-NL, nl-BE
- **Swedish**: sv-SE
- **Norwegian**: nb-NO
- **Danish**: da-DK
- **Finnish**: fi-FI
- **Polish**: pl-PL
- **Turkish**: tr-TR
- **Thai**: th-TH
- **Vietnamese**: vi-VN
- **Indonesian**: id-ID

For the complete and up-to-date list, see [Azure Language Support](https://docs.microsoft.com/en-us/azure/cognitive-services/speech-service/language-support).

## Troubleshooting

### Authentication Issues

If you see authentication errors:
1. Verify your Azure subscription key is correct
2. Check that the AZURE_REGION matches where your Speech resource is deployed
3. Ensure your Azure Speech resource is active and not expired
4. Verify your subscription has available quota

### No Transcription Results

If you're not receiving transcription events:
1. Check FreeSWITCH logs for connection errors
2. Verify the language code is supported
3. Ensure audio is being captured (check media bug attachment)
4. Verify network connectivity to Azure Speech Services endpoints
5. Check if `AZURE_INITIAL_SPEECH_TIMEOUT_MS` is too short

### Poor Transcription Quality

To improve transcription accuracy:
1. Use `AZURE_SPEECH_HINTS` to provide expected phrases or domain-specific vocabulary
2. Enable detailed output with `AZURE_USE_OUTPUT_FORMAT_DETAILED` to see confidence scores
3. Check SNR values (enable with `AZURE_REQUEST_SNR`) - low SNR indicates noisy audio
4. Ensure proper audio quality (clear speech, minimal background noise)

### Profanity Filtering

Configure profanity filtering based on your needs:
- `raw` - No filtering (default)
- `masked` - Replace profanity with asterisks
- `removed` - Remove profanity entirely from transcript

Set via: `AZURE_PROFANITY_OPTION`

### Connection Issues

If experiencing connection problems:
1. Verify libwebsockets is properly installed
2. Check firewall rules allow outbound WebSocket connections
3. Ensure DNS can resolve Azure Speech Services endpoints
4. Verify network latency to Azure region (choose closest region for best performance)

## Examples

See the examples directory for sample applications demonstrating Azure transcription with FreeSWITCH.
