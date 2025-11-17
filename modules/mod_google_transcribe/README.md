# mod_google_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using Google's Speech-to-Text API.

## Features

- Real-time streaming transcription via gRPC
- High accuracy with automatic punctuation
- Speaker diarization support
- Alternative language detection
- Voice activity detection (VAD) for cost optimization
- Word-level timing offsets
- Multiple model options (command and search, phone call, video, default)
- Enhanced models for premium accuracy
- Single utterance mode
- Profanity filtering
- Phrase hints for domain-specific vocabulary

Optionally, the connection to the Google Cloud recognizer can be delayed until voice activity has been detected. This can be useful in cases where it is desired to minimize the costs of streaming audio for transcription. This setting is governed by the channel variables starting with `RECOGNIZER_VAD`, as described below.

## Dependencies

- **gRPC** - Required for communication with Google Cloud Speech API
- **protobuf** (Protocol Buffers) - Required for message serialization
- **Google Cloud Speech API libraries**
- FreeSWITCH 1.8 or later

## Building

See the main [repository README](../../README.md) for complete build instructions. This module requires FreeSWITCH to be built with gRPC support.

**Recommended:** Use the [ansible-role-fsmrf](https://github.com/drachtio/ansible-role-fsmrf) which handles building gRPC, protobuf, and all dependencies with proper patches.

**Manual build:** Refer to the [gRPC build steps](https://github.com/drachtio/ansible-role-fsmrf/blob/main/tasks/grpc.yml) in the ansible role for detailed instructions on building gRPC and protobuf for FreeSWITCH.

## API

### Commands
The freeswitch module exposes two versions of an API command to transcribe speech:
#### version 1
```bash
uuid_google_transcribe <uuid> start <lang-code> [interim]
```
When using this command, additional speech processing options can be provided through Freeswitch channel variables, described [below](#command-variables).

####version 2
```bash
uuid_google_transcribe2 <uuid> start <lang-code> [interim] (bool) \
[single-utterance](bool) [separate-recognition](bool) [max-alternatives](int) \
[profanity-filter](bool) [word-time](bool) [punctuation](bool) \
[model](string) [enhanced](bool) [hints](word seperated by , and no spaces) \
[play-file] (play file path)
```
This command allows speech processing options to be provided on the command line, and has the ability to optionally play an audio file as a prompt.

Example:
```bash
bgapi uuid_google_transcribe2 312033b6-4b2a-48d8-be0c-5f161aec2b3e start en-US \
true true true 5 true true true command_and_search true \
yes,no,hello https://www2.cs.uic.edu/~i101/SoundFiles/CantinaBand60.wav
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid Google [language code](https://cloud.google.com/speech-to-text/docs/languages) to use for speech recognition
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
uuid_google_transcribe <uuid> stop
```
Stop transcription on the channel.

### Command Variables
Additional google speech options can be set through freeswitch channel variables for `uuid_google_transcribe` (some can alternatively be set in the command line for `uuid_google_transcribe2`).

| variable | Description |
| --- | ----------- |
| GOOGLE_SPEECH_SINGLE_UTTERANCE | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.StreamingRecognitionConfig.FIELDS.bool.google.cloud.speech.v1.StreamingRecognitionConfig.single_utterance) |
| GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.enable_separate_recognition_per_channel) |
| GOOGLE_SPEECH_MAX_ALTERNATIVES | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.int32.google.cloud.speech.v1.RecognitionConfig.max_alternatives) |
| GOOGLE_SPEECH_PROFANITY_FILTER | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.profanity_filter) |
| GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.enable_word_time_offsets) |
| GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.enable_automatic_punctuation) |
| GOOGLE_SPEECH_MODEL | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.string.google.cloud.speech.v1.RecognitionConfig.model) |
| GOOGLE_SPEECH_USE_ENHANCED | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1#google.cloud.speech.v1.RecognitionConfig.FIELDS.bool.google.cloud.speech.v1.RecognitionConfig.use_enhanced) |
| GOOGLE_SPEECH_HINTS | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.PhraseSet) |
| GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES | a comma-separated list of language codes, [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionConfig.FIELDS.repeated.string.google.cloud.speech.v1p1beta1.RecognitionConfig.alternative_language_codes) |
| GOOGLE_SPEECH_SPEAKER_DIARIZATION | set to 1 to enable [speaker diarization](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.SpeakerDiarizationConfig) |
|  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.SpeakerDiarizationConfig) |
|  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.SpeakerDiarizationConfig) |
| GOOGLE_SPEECH_METADATA_INTERACTION_TYPE | set to 'discussion', 'presentation', 'phone_call', 'voicemail', 'professionally_produced', 'voice_search', 'voice_command', or 'dictation' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.InteractionType) |
| GOOGLE_SPEECH_METADATA_INDUSTRY_NAICS_CODE | [read this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata) |
| GOOGLE_SPEECH_METADATA_MICROPHONE_DISTANCE | set to 'nearfield', 'midfield', or 'farfield' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.MicrophoneDistance) |
| GOOGLE_SPEECH_METADATA_ORIGINAL_MEDIA_TYPE | set to 'audio', or 'video' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.OriginalMediaType) |
| GOOGLE_SPEECH_METADATA_RECORDING_DEVICE_TYPE | set to 'smartphone', 'pc', 'phone_line', 'vehicle', 'other_outdoor_device', or 'other_indoor_device' [per this](https://cloud.google.com/speech-to-text/docs/reference/rpc/google.cloud.speech.v1p1beta1#google.cloud.speech.v1p1beta1.RecognitionMetadata.RecordingDeviceType)|
| START_RECOGNIZING_ON_VAD | if set to 1 or true, do not begin streaming audio to google cloud until voice activity is detected.|
| RECOGNIZER_VAD_MODE | An integer value 0-3 from less to more aggressive vad detection (default: 2).|
| RECOGNIZER_VAD_VOICE_MS | The number of milliseconds of voice activity that is required to trigger the connection to google cloud, when START_RECOGNIZING_ON_VAD is set (default: 250).|
| RECOGNIZER_VAD_DEBUG | if >0 vad debug logs will be generated (default: 0).|


### Events
**google_transcribe::transcription** - returns an interim or final transcription.  The event contains a JSON body describing the transcription result:
```js
{
	"stability": 0,
	"is_final": true,
	"alternatives": [{
		"confidence": 0.96471,
		"transcript": "Donny was a good bowler, and a good man"
	}]
}
```

**google_transcribe::end_of_utterance** - returns an indication that an utterance has been detected.  This may be returned prior to a final transcription.  This event is only returned when GOOGLE_SPEECH_SINGLE_UTTERANCE is set to true.

**google_transcribe::end_of_transcript** - returned when a transcription operation has completed. If a final transcription has not been returned by now, it won't be. This event is only returned when GOOGLE_SPEECH_SINGLE_UTTERANCE is set to true.

**google_transcribe::no_audio_detected** - returned when google has returned an error indicating that no audio was received for a lengthy period of time.

**google_transcribe::max_duration_exceeded** - returned when google has returned an an indication that a long-running transcription has been stopped due to a max duration limit (305 seconds) on their side.  It is the applications responsibility to respond by starting a new transcription session, if desired.

**google_transcribe::no_audio_detected** - returned when google has not received any audio for some reason.

## Authentication

Google Cloud Speech-to-Text requires authentication via a service account key. Set up authentication by:

1. Create a Google Cloud project and enable the Speech-to-Text API
2. Create a service account and download the JSON key file
3. Set the `GOOGLE_APPLICATION_CREDENTIALS` environment variable to point to the key file:
```bash
export GOOGLE_APPLICATION_CREDENTIALS="/path/to/service-account-key.json"
```

Alternatively, you can use Application Default Credentials (ADC) if running on Google Cloud Platform (GCE, GKE, Cloud Run, etc.).

## Usage

### Using drachtio-fsmrf

When using [drachtio-fsmrf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.

```javascript
// Basic transcription
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US`);

// With speaker diarization
await ep.set({
  GOOGLE_SPEECH_SPEAKER_DIARIZATION: '1',
  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT: '2',
  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT: '4'
});
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US interim`);

// With enhanced model and phrase hints
await ep.set({
  GOOGLE_SPEECH_MODEL: 'phone_call',
  GOOGLE_SPEECH_USE_ENHANCED: 'true',
  GOOGLE_SPEECH_HINTS: 'customer service,technical support,account balance',
  GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION: 'true'
});
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US interim`);

// Stop transcription
ep.api('uuid_google_transcribe', `${ep.uuid} stop`);
```

### Using FreeSWITCH Dialplan

```xml
<extension name="google_transcribe">
  <condition field="destination_number" expression="^transcribe$">
    <action application="answer"/>
    <action application="set" data="GOOGLE_SPEECH_MODEL=phone_call"/>
    <action application="set" data="GOOGLE_SPEECH_USE_ENHANCED=true"/>
    <action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
    <action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION=1"/>
    <action application="uuid_google_transcribe" data="start en-US interim"/>
    <action application="park"/>
  </condition>
</extension>
```

## Supported Languages

Google Cloud Speech-to-Text supports over 125 languages and variants. Some common ones include:

- **English**: en-US, en-GB, en-AU, en-CA, en-IN, en-NZ
- **Spanish**: es-ES, es-US, es-MX, es-AR, es-CO
- **French**: fr-FR, fr-CA, fr-BE, fr-CH
- **German**: de-DE, de-AT, de-CH
- **Italian**: it-IT
- **Portuguese**: pt-BR, pt-PT
- **Japanese**: ja-JP
- **Korean**: ko-KR
- **Chinese**: cmn-Hans-CN (Mandarin Simplified), cmn-Hant-TW (Mandarin Traditional), yue-Hant-HK (Cantonese)
- **Arabic**: ar-SA, ar-AE, ar-EG
- **Hindi**: hi-IN
- **Russian**: ru-RU
- **Dutch**: nl-NL, nl-BE
- **Swedish**: sv-SE
- **Norwegian**: no-NO
- **Danish**: da-DK
- **Finnish**: fi-FI
- **Polish**: pl-PL
- **Turkish**: tr-TR
- **Thai**: th-TH
- **Vietnamese**: vi-VN
- **Indonesian**: id-ID

For the complete and up-to-date list, see [Google Cloud Speech-to-Text Language Support](https://cloud.google.com/speech-to-text/docs/languages).

## Troubleshooting

### Authentication Issues

If you see authentication errors:
1. Verify `GOOGLE_APPLICATION_CREDENTIALS` environment variable is set correctly
2. Check that the service account has the "Cloud Speech-to-Text API User" role
3. Ensure the Speech-to-Text API is enabled in your Google Cloud project
4. Verify the service account key file is valid and accessible

### No Transcription Results

If you're not receiving transcription events:
1. Check FreeSWITCH logs for gRPC connection errors
2. Verify the language code is supported
3. Ensure audio is being captured (check media bug attachment)
4. Verify network connectivity to Google Cloud endpoints
5. Check if VAD settings are too aggressive (if using `START_RECOGNIZING_ON_VAD`)

### Speaker Diarization Not Working

If speaker labels are not appearing:
1. Ensure `GOOGLE_SPEECH_SPEAKER_DIARIZATION` is set to "1"
2. Set appropriate min/max speaker counts with `GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT` and `GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT`
3. Speaker diarization requires sufficient audio with multiple speakers
4. Not all languages support speaker diarization - check Google's language support documentation

### Poor Transcription Quality

To improve transcription accuracy:
1. Use the appropriate model for your use case:
   - `phone_call` - Optimized for telephony audio (8kHz)
   - `video` - Optimized for video/broadcast content
   - `command_and_search` - Optimized for short queries
   - `default` - General purpose
2. Enable enhanced models with `GOOGLE_SPEECH_USE_ENHANCED: 'true'` for premium accuracy
3. Use phrase hints for domain-specific vocabulary: `GOOGLE_SPEECH_HINTS: 'term1,term2,term3'`
4. Ensure good audio quality (clear speech, minimal background noise)
5. Enable automatic punctuation for better readability

### Max Duration Exceeded

If you receive `google_transcribe::max_duration_exceeded` events:
1. Google has a 305-second (5 minute) limit per streaming session
2. Your application should restart the transcription session when this event is received
3. Implement automatic session restart logic in your application

### Voice Activity Detection (VAD)

To optimize costs by only transcribing when speech is detected:
```javascript
await ep.set({
  START_RECOGNIZING_ON_VAD: '1',
  RECOGNIZER_VAD_MODE: '2',           // 0-3, higher = more aggressive
  RECOGNIZER_VAD_VOICE_MS: '250',     // ms of voice needed to start
  RECOGNIZER_VAD_DEBUG: '1'           // enable debug logging
});
```

### Build Issues

If you encounter build errors:
1. Ensure gRPC and protobuf are built with the correct versions
2. Verify all dependencies are installed (see ansible role for complete list)
3. Check that FreeSWITCH can find the gRPC libraries (check LD_LIBRARY_PATH)
4. The ansible-role-fsmrf includes necessary patches for FreeSWITCH compatibility
5. Manual builds can be complex - strongly consider using the ansible role

## Examples

[google_transcribe.js](../../examples/google_transcribe.js) - Complete example showing how to use Google transcription with drachtio-fsmrf, including speaker diarization and advanced options.