# mod_aws_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using AWS streaming transcription API.

## Features

- Real-time streaming transcription via AWS Transcribe Streaming API
- Speaker diarization to identify different speakers in the audio
- Support for multiple languages and language identification
- Interim and final transcription results
- Custom vocabulary support for domain-specific terminology
- Vocabulary filtering for profanity or sensitive words
- Medical and custom language models
- Channel identification for stereo audio

## Dependencies

- **AWS C++ SDK** - Required for AWS Transcribe API communication
  - Specifically: `aws-cpp-sdk-core` and `aws-cpp-sdk-transcribestreaming`
- FreeSWITCH 1.8 or later

## Building

See the main [repository README](../../README.md) for complete build instructions.

**Recommended:** Use the [ansible-role-fsmrf](https://github.com/drachtio/ansible-role-fsmrf) which handles building the AWS C++ SDK with all dependencies.

**Manual build:** You will need to build the AWS C++ SDK from source. Key steps:

1. Install AWS SDK dependencies:
```bash
apt-get install -y libcurl4-openssl-dev libssl-dev uuid-dev zlib1g-dev libpulse-dev
```

2. Build AWS C++ SDK with only required components:
```bash
git clone --recurse-submodules https://github.com/aws/aws-sdk-cpp
cd aws-sdk-cpp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_ONLY="transcribestreaming" \
  -DENABLE_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
sudo make install
```

For AWS SDK 1.11.200 compatibility (which this module is verified to work with), ensure you're using a compatible version.

## API

### Commands

The freeswitch module exposes the following API commands:

```
aws_transcribe <uuid> start <lang-code> [interim]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid AWS [language code](https://docs.aws.amazon.com/transcribe/latest/dg/supported-languages.html) that is supported for streaming transcription (e.g., en-US, es-US, fr-FR)
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned

```
aws_transcribe <uuid> stop
```
Stop transcription on the channel.

### Channel Variables

The following channel variables can be set to configure the AWS transcription service:

| Variable | Description | Default |
| --- | ----------- | --- |
| AWS_ACCESS_KEY_ID | AWS access key ID for authentication | (from environment or AWS credentials) |
| AWS_SECRET_ACCESS_KEY | AWS secret access key for authentication | (from environment or AWS credentials) |
| AWS_REGION | AWS region for Transcribe service (e.g., us-east-1, us-west-2) | us-east-1 |
| AWS_VOCABULARY_NAME | Name of custom vocabulary to use | none |
| AWS_VOCABULARY_FILTER_NAME | Name of vocabulary filter to apply | none |
| AWS_VOCABULARY_FILTER_METHOD | How to filter: "remove", "mask", "tag" | none |
| AWS_SESSION_ID | Custom session identifier for the transcription | auto-generated |
| AWS_METADATA | Custom metadata to attach to the session | none |
| AWS_SHOW_SPEAKER_LABEL | Enable speaker diarization (set to "true") | false |
| AWS_SPEAKER_LABEL | Deprecated - use AWS_SHOW_SPEAKER_LABEL | false |
| AWS_ENABLE_CHANNEL_IDENTIFICATION | Enable channel identification for stereo audio | false |
| AWS_NUMBER_OF_CHANNELS | Number of audio channels (1 or 2) | 1 |

## Authentication

The plugin will first look for channel variables, then environment variables. If neither are found, then the default AWS credentials chain will be used (EC2 instance role, ~/.aws/credentials, etc.).

The names of the channel variables and environment variables for authentication are:

| Variable | Description |
| --- | ----------- |
| AWS_ACCESS_KEY_ID | The AWS access key ID |
| AWS_SECRET_ACCESS_KEY | The AWS secret access key |
| AWS_REGION | The AWS region (e.g., us-east-1) |

## Events

### aws_transcribe::transcription

Returns an interim or final transcription. The event contains a JSON body describing the transcription result.

#### Without Speaker Diarization

```json
[
  {
    "is_final": true,
    "alternatives": [{
      "transcript": "Hello. Can you hear me?"
    }]
  }
]
```

#### With Speaker Diarization

When `AWS_SHOW_SPEAKER_LABEL` is set to "true", the transcription includes speaker labels and individual word timings:

```json
[
  {
    "is_final": true,
    "alternatives": [{
      "transcript": "Hello. How are you today?",
      "items": [
        {
          "content": "Hello",
          "end_time": 0.38,
          "speaker_label": "spk_0",
          "start_time": 0.0,
          "type": "pronunciation"
        },
        {
          "content": ".",
          "type": "punctuation"
        },
        {
          "content": "How",
          "end_time": 1.01,
          "speaker_label": "spk_1",
          "start_time": 0.82,
          "type": "pronunciation"
        },
        {
          "content": "are",
          "end_time": 1.14,
          "speaker_label": "spk_1",
          "start_time": 1.02,
          "type": "pronunciation"
        },
        {
          "content": "you",
          "end_time": 1.32,
          "speaker_label": "spk_1",
          "start_time": 1.15,
          "type": "pronunciation"
        },
        {
          "content": "today",
          "end_time": 1.79,
          "speaker_label": "spk_1",
          "start_time": 1.33,
          "type": "pronunciation"
        },
        {
          "content": "?",
          "type": "punctuation"
        }
      ]
    }],
    "speakers": [
      {
        "speaker": "spk_0",
        "transcript": "Hello."
      },
      {
        "speaker": "spk_1",
        "transcript": "How are you today?"
      }
    ]
  }
]
```

The `speakers` array is a convenience feature that groups the transcript by speaker, making it easier to display conversation-style output.

### aws_transcribe::connect

Fired when the connection to AWS Transcribe is successfully established.

### aws_transcribe::error

Fired when an error occurs during transcription. Contains error details in the event body.

## Usage

### Using drachtio-fsmrf

When using [drachtio-fsmrf](https://www.npmjs.com/package/drachtio-fsmrf), you can access this API command via the api method on the 'endpoint' object.

```javascript
// Basic transcription
ep.api('aws_transcribe', `${ep.uuid} start en-US interim`);

// With speaker diarization
await ep.set({
  AWS_SHOW_SPEAKER_LABEL: 'true',
  AWS_REGION: 'us-east-1'
});
ep.api('aws_transcribe', `${ep.uuid} start en-US interim`);

// Stop transcription
ep.api('aws_transcribe', `${ep.uuid} stop`);
```

### Using FreeSWITCH Dialplan

```xml
<extension name="aws_transcribe">
  <condition field="destination_number" expression="^transcribe$">
    <action application="answer"/>
    <action application="set" data="AWS_REGION=us-east-1"/>
    <action application="set" data="AWS_SHOW_SPEAKER_LABEL=true"/>
    <action application="aws_transcribe" data="start en-US interim"/>
    <action application="park"/>
  </condition>
</extension>
```

## Supported Languages

AWS Transcribe Streaming supports many languages. Some common ones include:

- **English**: en-US (US), en-GB (UK), en-AU (Australia), en-IN (India)
- **Spanish**: es-US (US), es-ES (Spain)
- **French**: fr-FR (France), fr-CA (Canada)
- **German**: de-DE
- **Italian**: it-IT
- **Portuguese**: pt-BR (Brazil), pt-PT (Portugal)
- **Japanese**: ja-JP
- **Korean**: ko-KR
- **Chinese**: zh-CN (Mandarin)
- **Arabic**: ar-AE, ar-SA
- **Hindi**: hi-IN
- **Russian**: ru-RU
- **Dutch**: nl-NL
- **Turkish**: tr-TR
- **Swedish**: sv-SE
- **Indonesian**: id-ID
- **Malay**: ms-MY
- **Thai**: th-TH
- **Vietnamese**: vi-VN

For the complete and up-to-date list, see [AWS Transcribe Supported Languages](https://docs.aws.amazon.com/transcribe/latest/dg/supported-languages.html).

## Troubleshooting

### Authentication Issues

If you see authentication errors:
1. Verify your AWS credentials are correct
2. Check that the IAM user/role has `transcribe:StartStreamTranscription` permission
3. Ensure the AWS_REGION is set correctly

### No Transcription Results

If you're not receiving transcription events:
1. Check FreeSWITCH logs for connection errors
2. Verify the language code is supported for streaming
3. Ensure audio is being captured (check media bug attachment)
4. Verify network connectivity to AWS Transcribe endpoints

### Speaker Diarization Not Working

If speaker labels are not appearing:
1. Ensure `AWS_SHOW_SPEAKER_LABEL` is set to "true" (not "1" or "yes")
2. Speaker diarization requires sufficient audio with multiple speakers
3. Check that you're using a supported language for speaker diarization

### Build Issues

If you encounter build errors:
1. Ensure AWS C++ SDK version 1.11.200 or compatible version is installed
2. Verify all SDK dependencies are installed (libcurl, openssl, uuid-dev)
3. Check that the transcribestreaming component is built
4. Ensure FreeSWITCH can find the AWS SDK libraries (check LD_LIBRARY_PATH)

## Examples

[aws_transcribe.js](../../examples/aws_transcribe.js) - Complete example showing how to use AWS transcription with drachtio-fsmrf, including speaker diarization support.
