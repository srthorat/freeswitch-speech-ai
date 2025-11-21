# mod_aws_transcribe

A Freeswitch module that generates real-time transcriptions on a Freeswitch channel by using AWS streaming transcription API.

## Features

- Real-time streaming transcription via AWS Transcribe Streaming API
- Speaker diarization to identify different speakers in the audio (AI-based, up to 10 speakers)
- Channel identification for stereo audio (perfect agent/customer separation in telephony)
- Support for multiple languages and language identification
- Interim and final transcription results
- Custom vocabulary support for domain-specific terminology
- Vocabulary filtering for profanity or sensitive words
- Medical and custom language models
- **Voice Activity Detection (VAD)** - Delay AWS connection until speech detected (reduces costs)
- **Automatic audio resampling** - Handles 8kHz, 16kHz, 48kHz codecs automatically
- **Pre-connection buffering** - Buffers audio during AWS connection to avoid missing speech start
- **Multi-session management** - Handle hundreds of concurrent calls efficiently
- **Production-grade threading** - Producer-consumer pattern with proper synchronization

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

**Verified AWS SDK versions:**
- ✅ **1.11.345** - Default version used in Docker builds (tested and stable)
- ✅ **1.11.200+** - All versions from 1.11.200 onwards are compatible
- ✅ **1.11.694** - Latest version as of 2025-01 (upgrade available)

For production use, we recommend AWS SDK 1.11.345 or later.

## Architecture Overview

mod_aws_transcribe is a **production-grade FreeSWITCH module** (938 lines of code) designed specifically for real-time telephony transcription. Unlike simple file-processing demos, this module provides:

### Key Design Features

1. **Real-time Audio Processing**
   - Processes live RTP audio streams from phone calls
   - No artificial delays - handles audio as it arrives
   - Integrates seamlessly with FreeSWITCH media pipeline

2. **Advanced Buffering Strategy**
   - **Pre-connection buffer**: Circular buffer (4800 bytes) stores audio while AWS connection is establishing
   - **Post-connection queue**: `std::deque` for thread-safe audio streaming
   - Ensures no speech is lost during connection setup

3. **Intelligent VAD Integration**
   - Optional Voice Activity Detection delays AWS connection until speech detected
   - Reduces AWS costs by avoiding silence transcription
   - Configurable sensitivity via `START_RECOGNIZING_ON_VAD` channel variable

4. **Automatic Audio Resampling**
   - Uses `speex_resampler` to handle various codec sample rates
   - Automatically converts 8kHz → 16kHz for AWS requirements
   - Supports 8kHz, 16kHz, 48kHz without manual configuration

5. **Production Threading Model**
   - **Media bug callback thread** - Captures audio frames from FreeSWITCH
   - **AWS processing thread** - Handles AWS communication and event processing
   - Producer-consumer pattern with mutex/condition variable synchronization
   - Handles hundreds of concurrent calls efficiently

6. **Flexible Authentication**
   - Per-call credentials via channel variables
   - Global credentials via environment variables
   - Automatic IAM role detection on EC2/ECS
   - Three-tier fallback ensures maximum flexibility

7. **Real-time Event System**
   - Fires FreeSWITCH events immediately (not batched)
   - `aws_transcribe::transcription` - Interim and final results
   - `aws_transcribe::connect` - Connection established
   - `aws_transcribe::error` - Error notifications
   - `aws_transcribe::vad_detected` - Speech detected
   - Events consumed by dialplan, ESL clients, or other modules

### Comparison with Standalone Implementations

mod_aws_transcribe is **far more advanced** than typical AWS Transcribe sample code:

| Feature | Standalone Demo | mod_aws_transcribe |
|---------|----------------|-------------------|
| **Audio source** | Pre-recorded file | Live phone call |
| **Timing** | Simulated (sleep) | Real-time RTP |
| **Buffering** | None | Pre-connection + queue |
| **VAD** | No | Yes (optional) |
| **Resampling** | Manual | Automatic |
| **Threading** | Simple join | Producer-consumer |
| **Sessions** | One at a time | Hundreds concurrent |
| **Events** | File output | Real-time FreeSWITCH events |
| **Configuration** | Static | Dynamic per-call |
| **Lines of code** | ~269 | 938 (3.5x more) |

**Conclusion:** This module is specifically engineered for production telephony environments, not general-purpose file transcription.

## API

### Commands

The freeswitch module exposes the following API commands:

```
uuid_aws_transcribe <uuid> start <lang-code> [interim] [stereo|mono]
```
Attaches media bug to channel and performs streaming recognize request.
- `uuid` - unique identifier of Freeswitch channel
- `lang-code` - a valid AWS [language code](https://docs.aws.amazon.com/transcribe/latest/dg/supported-languages.html) that is supported for streaming transcription (e.g., en-US, es-US, fr-FR)
- `interim` - If the 'interim' keyword is present then both interim and final transcription results will be returned; otherwise only final transcriptions will be returned
- `stereo|mono` - Optional: Specify audio channel mode (default: mono). Use `stereo` for dual-channel audio with channel identification

```
uuid_aws_transcribe <uuid> stop
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
| START_RECOGNIZING_ON_VAD | Enable Voice Activity Detection - delay AWS connection until speech detected (reduces costs) | false |

## Authentication

### Credential Priority (IMPORTANT)

The module uses a **three-tier authentication priority**:

1. **Channel Variables** (HIGHEST PRIORITY) - Set in dialplan via `<action application="set">`
2. **Environment Variables** - Passed to FreeSWITCH process (e.g., Docker `-e` flags)
3. **AWS Credentials Chain** (FALLBACK) - EC2/ECS IAM role, ~/.aws/credentials, ~/.aws/config

⚠️ **Critical**: If you set AWS credentials in the dialplan (even with placeholder values), they will **override** environment variables. To use environment variables, **comment out** or **remove** credential lines from the dialplan.

### Credential Variables

| Variable | Description | Required |
| --- | ----------- | -------- |
| AWS_ACCESS_KEY_ID | The AWS access key ID (AKIA* for permanent, ASIA* for temporary) | Yes |
| AWS_SECRET_ACCESS_KEY | The AWS secret access key | Yes |
| AWS_SESSION_TOKEN | Session token for temporary STS credentials (only needed for ASIA* keys) | For temporary creds |
| AWS_REGION | The AWS region (e.g., us-east-1) | Yes |

### Method 1: Environment Variables (Recommended for Docker)

**For permanent IAM credentials (AKIA*):**
```bash
docker run -e AWS_ACCESS_KEY_ID=AKIA**************** \
           -e AWS_SECRET_ACCESS_KEY=**************************************** \
           -e AWS_REGION=us-east-1 \
           srt2011/freeswitch-mod-aws-transcribe:latest
```

**For temporary STS credentials (ASIA*):**
```bash
docker run -e AWS_ACCESS_KEY_ID=ASIA**************** \
           -e AWS_SECRET_ACCESS_KEY=**************************************** \
           -e AWS_SESSION_TOKEN=IQoJb3JpZ2luX2VjE... \
           -e AWS_REGION=us-east-1 \
           srt2011/freeswitch-mod-aws-transcribe:latest
```

> **Note**: Replace the masked values with your actual AWS credentials. Never commit real credentials to git.

Dialplan should NOT set these variables (comment them out):
```xml
<!-- <action application="set" data="AWS_ACCESS_KEY_ID=..."/> -->
<!-- <action application="set" data="AWS_SECRET_ACCESS_KEY=..."/> -->
<!-- <action application="set" data="AWS_REGION=us-east-1"/> -->
```

### Method 2: Channel Variables (Per-Call Credentials)

Set in dialplan for per-user or per-call credentials:
```xml
<action application="set" data="AWS_ACCESS_KEY_ID=${user_data(${caller_id_number}@${domain_name} var aws_key)}"/>
<action application="set" data="AWS_SECRET_ACCESS_KEY=${user_data(${caller_id_number}@${domain_name} var aws_secret)}"/>
<action application="set" data="AWS_REGION=us-east-1"/>
```

### Method 3: AWS Credentials Chain (IAM Roles)

**Best for production deployments on AWS infrastructure.**

For AWS EC2/ECS/EKS deployments with IAM roles, no explicit credentials are needed:

```bash
# No AWS credentials needed - uses IAM role automatically
docker run -e AWS_REGION=us-east-1 \
           srt2011/freeswitch-mod-aws-transcribe:latest
```

Dialplan should NOT set credentials:
```xml
<!-- Do NOT set AWS_ACCESS_KEY_ID or AWS_SECRET_ACCESS_KEY -->
<!-- Only set region if needed -->
<action application="set" data="AWS_REGION=us-east-1"/>
```

**How it works:**
1. Module detects no explicit credentials
2. AWS SDK automatically tries:
   - EC2 instance metadata (IAM instance profile)
   - ECS task role (IAM task execution role)
   - EKS service account (IRSA)
   - `~/.aws/credentials` file
   - `~/.aws/config` file

**Required IAM Policy:**
```json
{
  "Version": "2012-10-17",
  "Statement": [
    {
      "Effect": "Allow",
      "Action": [
        "transcribe:StartStreamTranscription"
      ],
      "Resource": "*"
    }
  ]
}
```

### Credential Type Detection

The module automatically detects your credential type at startup:

| Access Key Prefix | Type | Session Token Required? | Log Message |
|-------------------|------|-------------------------|-------------|
| `AKIA*` | Permanent IAM User | No | `Permanent (AKIA*)` |
| `ASIA*` | Temporary STS/SSO | **YES** | `Temporary (ASIA* + session token)` |
| (empty) | IAM Role/Credentials Chain | N/A | `AWS default credentials chain` |

**Watch startup logs for authentication status:**
```
=========================================================
mod_aws_transcribe: Checking AWS credentials...
  ✓ Environment credentials found: Temporary (ASIA* + session token)
    AWS_ACCESS_KEY_ID: ASIA***
    AWS_SESSION_TOKEN: present
  ✓ AWS_REGION: us-east-1

Authentication priority:
  1. Channel variables (per-call)
  2. Environment variables (container-level)
  3. AWS credentials chain (IAM role, ~/.aws/credentials)
=========================================================
```

### Troubleshooting Authentication

**Error: "The security token included in the request is invalid"**

This means your ASIA* credentials are missing the session token:

```bash
# ❌ WRONG - ASIA* without session token
export AWS_ACCESS_KEY_ID=ASIA...
export AWS_SECRET_ACCESS_KEY=...
# Missing: AWS_SESSION_TOKEN

# ✅ CORRECT - ASIA* with session token
export AWS_ACCESS_KEY_ID=ASIA...
export AWS_SECRET_ACCESS_KEY=...
export AWS_SESSION_TOKEN=IQoJb3JpZ2luX2VjE...  # Required!
```

**Check logs for warnings:**
```
⚠ WARNING: ASIA* credentials require AWS_SESSION_TOKEN!
⚠ Authentication will likely fail without session token.
```

**Verify credentials in container:**
```bash
# Check what FreeSWITCH sees
docker exec freeswitch env | grep AWS

# Should show (if using env vars):
AWS_ACCESS_KEY_ID=ASIA... or AKIA...
AWS_SECRET_ACCESS_KEY=...
AWS_SESSION_TOKEN=...  (only for ASIA*)
AWS_REGION=us-east-1
```

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

---

## Speaker Identification in Telephony

mod_aws_transcribe supports **two methods** for identifying speakers in phone calls. Choose the method based on your use case and audio setup.

### Method 1: Speaker Diarization (AI-Based)

**What it is:** AWS AI analyzes voice patterns to distinguish different speakers in the audio.

**Best for:**
- Conference calls (3+ participants)
- Calls where you have mono (single channel) audio
- When you need to identify multiple speakers but don't have stereo recording

**Configuration:**
```javascript
// Via drachtio-fsmrf
await ep.set({
  AWS_SHOW_SPEAKER_LABEL: 'true',
  AWS_REGION: 'us-east-1'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

```xml
<!-- Via FreeSWITCH dialplan -->
<action application="set" data="AWS_SHOW_SPEAKER_LABEL=true"/>
<action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
```

**Output:** Speakers labeled as `spk_0`, `spk_1`, `spk_2`, etc. (see [Speaker Diarization Output](#with-speaker-diarization))

**Characteristics:**
- ✅ Works with mono audio
- ✅ Detects up to 10 speakers
- ✅ No special audio setup required
- ❌ Doesn't identify "who is who" (just spk_0, spk_1...)
- ❌ Accuracy: 85-95% (can confuse similar voices)
- ⚠️ Cost: **2x base rate** (~$0.048/minute)

---

### Method 2: Channel Identification (Telephony-Optimized) ⭐ RECOMMENDED

**What it is:** Uses LEFT and RIGHT stereo audio channels to separate speakers physically.

**Best for:**
- **Call centers** (agent + customer)
- **1-on-1 phone calls** (exactly 2 speakers)
- Quality monitoring and compliance recording
- When you control the phone system and can record stereo

**How it works:**
```
┌──────────────────────────────────┐
│  Phone Call                      │
│                                  │
│  Agent    ──→ LEFT channel (ch_0)│
│  Customer ──→ RIGHT channel (ch_1)│
└──────────────────────────────────┘
```

**Configuration:**
```javascript
// Via drachtio-fsmrf
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
  AWS_NUMBER_OF_CHANNELS: '2',
  AWS_REGION: 'us-east-1'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

```xml
<!-- Via FreeSWITCH dialplan -->
<action application="set" data="RECORD_STEREO=true"/>
<action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
<action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
<action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
```

**Output:** Results include `channel_id` field:
```json
[
  {
    "is_final": true,
    "channel_id": "ch_0",  // Left channel = Agent
    "alternatives": [{
      "transcript": "Hello, how can I help you today?"
    }]
  },
  {
    "is_final": true,
    "channel_id": "ch_1",  // Right channel = Customer
    "alternatives": [{
      "transcript": "I need help with my order."
    }]
  }
]
```

**Characteristics:**
- ✅ **100% accurate** separation (physical channels)
- ✅ Know exactly who is agent vs customer
- ✅ Perfect for call center use cases
- ✅ Lower compute cost
- ❌ Requires stereo audio recording
- ❌ Only works for 2 speakers
- ⚠️ Cost: **1.25x base rate** (~$0.030/minute)

---

### Combining Both Methods (Advanced)

For complex scenarios like conference calls with participants on each side:

```javascript
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',  // Separate by channel
  AWS_SHOW_SPEAKER_LABEL: 'true',            // AND detect speakers within each channel
  AWS_NUMBER_OF_CHANNELS: '2'
});
```

This gives you:
- `ch_0` + `spk_0`, `spk_1` = Multiple people on agent side
- `ch_1` + `spk_2`, `spk_3` = Multiple people on customer side

**Cost:** ~$0.054/minute (both features enabled)

---

### Decision Guide: Which Method to Use?

| Use Case | Method | Why |
|----------|--------|-----|
| **Call Center (Agent + Customer)** | Channel Identification ⭐ | 100% accurate, cheaper, perfect separation |
| **Customer Support 1-on-1** | Channel Identification ⭐ | Know exactly who said what |
| **3-way Conference Call** | Speaker Diarization | AI detects all 3 speakers |
| **Group Call (4+ people)** | Speaker Diarization | Only option for multiple speakers |
| **Webinar/Panel** | Speaker Diarization | Many speakers on same audio |
| **Compliance Recording** | Channel Identification ⭐ | Perfect accuracy required |

---

### Cost Comparison

Based on AWS Transcribe pricing (as of 2025):

| Configuration | Cost per Minute | Monthly Cost (1000 min) | Use Case |
|--------------|-----------------|------------------------|----------|
| Basic transcription only | $0.024 | $24.00 | No speaker identification |
| + Channel identification | $0.030 | $30.00 | Call centers (recommended) |
| + Speaker diarization | $0.048 | $48.00 | Conferences, multi-speaker |
| + Both | $0.054 | $54.00 | Complex scenarios |

**Recommendation:** Use channel identification when possible - it's cheaper and more accurate!

---

### FreeSWITCH Stereo Recording Setup

To use channel identification, you need stereo audio recording:

**Option 1: Using mod_audio_fork (Real-time)**
```javascript
// Automatically provides stereo if both sides are captured
await ep.execute('uuid_audio_fork', `${ep.uuid} start`);
```

**Option 2: Using dialplan configuration**
```xml
<extension name="stereo_recording">
  <condition field="destination_number" expression="^(\d+)$">
    <action application="answer"/>
    <action application="set" data="RECORD_STEREO=true"/>
    <action application="set" data="RECORD_MIN_SEC=0"/>
    <action application="record_session" data="/tmp/recording_${uuid}.wav"/>

    <!-- Enable AWS channel identification -->
    <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
    <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>

    <action application="park"/>
  </condition>
</extension>
```

---

### Real-World Examples

#### Example 1: Call Center with Perfect Speaker Separation

```javascript
// Contact center setup
const { Srf } = require('drachtio-srf');
const Mrf = require('drachtio-fsmrf');

srf.invite(async (req, res) => {
  const ms = await mrf.connect(...);
  const ep = await ms.createEndpoint();

  // Configure for agent + customer separation
  await ep.set({
    AWS_ACCESS_KEY_ID: process.env.AWS_ACCESS_KEY_ID,
    AWS_SECRET_ACCESS_KEY: process.env.AWS_SECRET_ACCESS_KEY,
    AWS_REGION: 'us-east-1',
    AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
    AWS_NUMBER_OF_CHANNELS: '2'
  });

  // Start transcription
  await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);

  // Handle transcription events
  ep.on('aws_transcribe::transcription', (evt, result) => {
    const data = JSON.parse(evt.body);
    if (data[0].channel_id === 'ch_0') {
      console.log(`Agent: ${data[0].alternatives[0].transcript}`);
    } else {
      console.log(`Customer: ${data[0].alternatives[0].transcript}`);
    }
  });
});
```

#### Example 2: Conference Call with Multiple Speakers

```javascript
// Conference call setup
await ep.set({
  AWS_SHOW_SPEAKER_LABEL: 'true',
  AWS_REGION: 'us-east-1'
});

await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);

ep.on('aws_transcribe::transcription', (evt, result) => {
  const data = JSON.parse(evt.body);

  // AWS groups by speaker automatically
  if (data[0].speakers) {
    data[0].speakers.forEach(speaker => {
      console.log(`${speaker.speaker}: ${speaker.transcript}`);
      // Output:
      // spk_0: Hello everyone.
      // spk_1: Hi, glad to be here.
      // spk_2: Let's get started.
    });
  }
});
```

---

### Troubleshooting Speaker Identification

#### Channel Identification Issues

**Problem:** Not getting `channel_id` in results

**Solutions:**
1. Verify `AWS_ENABLE_CHANNEL_IDENTIFICATION` is set to `"true"` (string, not boolean)
2. Confirm `AWS_NUMBER_OF_CHANNELS` is set to `2`
3. Ensure audio is actually stereo (check FreeSWITCH recording)
4. Check FreeSWITCH logs for "channel identification" messages

**Problem:** Both speakers appear on same channel

**Solutions:**
1. Check stereo recording configuration (`RECORD_STEREO=true`)
2. Verify media bug is capturing both channels
3. Ensure SIP signaling preserves stereo audio

#### Speaker Diarization Issues

**Problem:** All words have same speaker label

**Solutions:**
1. Verify `AWS_SHOW_SPEAKER_LABEL` is set to `"true"`
2. Ensure there are actually multiple speakers in the audio
3. Check that speakers talk long enough (>5 seconds each)
4. Verify language code supports speaker diarization

**Problem:** Poor speaker separation accuracy

**Solutions:**
1. Use channel identification instead if you have 2 speakers
2. Ensure good audio quality (minimize background noise)
3. Allow speakers to talk for longer periods (AI needs data)
4. Consider using custom vocabulary for better accuracy

---

## Pusher Integration (Real-time Transcription Delivery)

mod_aws_transcribe includes built-in Pusher integration for delivering real-time transcriptions to your frontend applications via WebSocket.

### How It Works

When Pusher is configured, the module automatically:
1. Receives transcription from AWS Transcribe API
2. Transforms the data to a standardized format
3. Maps speaker identity using caller/callee metadata and channel ID
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
| `PUSHER_EVENT_FINAL` | No | `transcription-final` | Event name for final transcriptions |
| `PUSHER_EVENT_INTERIM` | No | `transcription-interim` | Event name for interim transcriptions |

### Docker Run with Pusher

**AWS + Pusher:**
```bash
docker run -d --name freeswitch \
  -p 5060:5060/udp -p 8021:8021 \
  -e AWS_ACCESS_KEY_ID=AKIA**************** \
  -e AWS_SECRET_ACCESS_KEY=**************************************** \
  -e AWS_REGION=us-east-1 \
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
  -e AWS_ACCESS_KEY_ID=AKIA**************** \
  -e AWS_SECRET_ACCESS_KEY=**************************************** \
  -e AWS_REGION=us-east-1 \
  -e PUSHER_APP_ID=123456 \
  -e PUSHER_KEY=your-pusher-key \
  -e PUSHER_SECRET=your-pusher-secret \
  -e PUSHER_CLUSTER=ap2 \
  -e PUSHER_CHANNEL_PREFIX=transcription- \
  -e PUSHER_EVENT_FINAL=final \
  -e PUSHER_EVENT_INTERIM=interim \
  freeswitch-speech-ai:latest
```

### Pusher Data Format

The module sends transformed transcription data in this format:

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
- `text`: The transcribed text
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

  useEffect(() => {
    const pusher = new Pusher(process.env.REACT_APP_PUSHER_KEY, {
      cluster: 'ap2'
    });

    const channel = pusher.subscribe(`call-${callId}`);

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
- Ensure channel identification is enabled for accurate speaker separation
- Use `AWS_ENABLE_CHANNEL_IDENTIFICATION=true` and `AWS_NUMBER_OF_CHANNELS=2`

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
- **Channel 0** (ch_0) = Caller (whoever initiated/originated the call)
- **Channel 1** (ch_1) = Callee (whoever received/answered the call)

**This works automatically for both directions:**

**Inbound calls TO FreeSWITCH:**
- Channel 0 (ch_0) = External customer (the caller)
- Channel 1 (ch_1) = FreeSWITCH extension/agent (the callee)

**Outbound calls FROM FreeSWITCH:**
- Channel 0 (ch_0) = FreeSWITCH extension/agent (the caller)
- Channel 1 (ch_1) = External customer (the callee)

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
    <action application="set" data="AWS_REGION=us-east-1"/>
    <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
    <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>

    <!-- Start transcription AFTER call is answered -->
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
    <action application="set" data="api_hangup_hook=uuid_aws_transcribe ${uuid} stop"/>

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
  AWS_REGION: 'us-east-1',
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
  AWS_NUMBER_OF_CHANNELS: '2'
});

// Make outbound call
await ep.execute('bridge', 'sofia/external/15551234567@provider.com');

// Start transcription after answer
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

### Metadata and Speaker Identification for Outbound Calls

The module automatically extracts caller/callee information from FreeSWITCH channel variables:

**For Outbound Calls:**
- `caller_id_name` / `caller_id_number` → Channel 0 (ch_0) (FreeSWITCH/Originator)
- `destination_number` / `callee_id_name` → Channel 1 (ch_1) (External party/Destination)

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

### AWS Transcription Output for Outbound Calls

When using channel identification, AWS returns results with `channel_id`:

```json
[
  {
    "is_final": true,
    "channel_id": "ch_0",
    "alternatives": [{
      "transcript": "Hello, this is John from ABC Company."
    }]
  },
  {
    "is_final": true,
    "channel_id": "ch_1",
    "alternatives": [{
      "transcript": "Hi John, how can I help you today?"
    }]
  }
]
```

### Pusher Integration for Outbound Calls

When using Pusher for real-time transcription delivery, outbound calls work identically to inbound calls:

**Transformed format sent to Pusher:**

```json
{
  "type": "final",
  "speaker_id": "Extension 1000(1000)",
  "text": "Hello, this is John from ABC Company.",
  "timestamp": "2025-11-21T10:30:45Z"
}
```

```json
{
  "type": "interim",
  "speaker_id": "Unknown(15551234567)",
  "text": "Hi John, how can I help you today?",
  "timestamp": "2025-11-21T10:30:52Z"
}
```

**Key Points:**
- Speaker mapping uses `ChannelId` (ch_0, ch_1) for accurate separation
- Channel 0 (ch_0) always maps to caller (originator)
- Channel 1 (ch_1) always maps to callee (destination)
- Metadata is extracted automatically from channel variables
- Works with both inbound and outbound call scenarios
- 100% accurate speaker separation (physical channel-based, not AI)

---

## Usage

**Recommended Approach for Production:** Use per-user flag-based configuration with centralized settings in dialplan.

📖 **See:** [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

**Note:** The guide shows Azure transcription examples, but the same pattern applies to AWS transcription by:
1. Using `enable_aws_transcribe` flag in user files
2. Setting AWS credentials in dialplan using channel variables
3. Using `api_on_answer` to start transcription after call is answered

---

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
ep.api('uuid_aws_transcribe', `${ep.uuid} stop`);
```

### Using FreeSWITCH Dialplan

```xml
<extension name="aws_transcribe">
  <condition field="destination_number" expression="^transcribe$">
    <action application="answer"/>
    <action application="set" data="AWS_REGION=us-east-1"/>
    <action application="set" data="AWS_SHOW_SPEAKER_LABEL=true"/>
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
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
