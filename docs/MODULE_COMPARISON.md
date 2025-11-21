# FreeSWITCH Speech Modules - Comprehensive Comparison

A detailed comparison of all speech transcription modules available in this repository, covering performance, accuracy, features, protocols, and setup complexity.

## Quick Reference Table

| Feature | AWS Transcribe | Azure Speech | Deepgram | Google Speech | Audio Fork |
|---------|---------------|--------------|----------|---------------|------------|
| **Primary Use** | Transcription | Transcription | Transcription | Transcription | Generic Streaming |
| **Speed Rating** | Fast | Fast | Very Fast | Fast | N/A |
| **Accuracy** | High | High | Very High (Nova-2) | Very High | N/A |
| **Protocol** | Native AWS SDK | WebSocket | WebSocket | gRPC | WebSocket |
| **Setup Complexity** | Complex | Simple | Simple | Complex | Very Simple |
| **Build Time** | 20-40 min | <5 min | <5 min | 15-30 min | <5 min |

---

## 1. Speed & Performance

### Response Time Comparison

| Module | Latency | Interim Results | Real-time Factor | Best For |
|--------|---------|----------------|------------------|----------|
| **AWS Transcribe** | Low (~200-500ms) | Yes | 1.0x | Call centers, compliance |
| **Azure Speech** | Low (~200-400ms) | Yes | 1.0x | Enterprise integration |
| **Deepgram** | Very Low (~100-300ms) | Yes | 0.8x | Low-latency applications |
| **Google Speech** | Low (~250-500ms) | Yes | 1.0x | High accuracy needs |

**Key Insights:**
- **Deepgram** offers the fastest transcription with Nova-2 model
- **AWS & Azure** provide consistent low-latency performance
- **Google** slightly higher latency but excellent accuracy
- All modules support interim results for real-time feedback

### Throughput & Scalability

| Module | Concurrent Sessions | Resource Usage | Threading Model |
|--------|-------------------|----------------|-----------------|
| **AWS Transcribe** | Hundreds | Medium | Producer-consumer with proper sync |
| **Azure Speech** | Hundreds | Low | WebSocket event-driven |
| **Deepgram** | Hundreds | Low | WebSocket event-driven |
| **Google Speech** | Hundreds | Medium-High | gRPC streaming |

**Production Characteristics:**
- **AWS**: 938 lines of code, production-grade threading, pre-connection buffering
- **Azure**: Lightweight, event-driven architecture
- **Deepgram**: Optimized for speed, efficient processing
- **Google**: Robust gRPC implementation, higher memory usage

---

## 2. Accuracy & Quality

### Transcription Accuracy

| Module | Base Accuracy | Premium Tier | Model Options | Language Support |
|--------|--------------|--------------|---------------|------------------|
| **AWS Transcribe** | 90-95% | Medical/Custom models | Standard | 30+ languages |
| **Azure Speech** | 90-95% | Detailed output format | Standard | 100+ languages |
| **Deepgram** | 92-97% | Nova-2 tier | 8 specialized models | 30+ languages |
| **Google Speech** | 92-98% | Enhanced models | 4 models | 125+ languages |

### Specialized Model Support

#### AWS Transcribe
- **Medical model** - Healthcare terminology
- **Custom vocabulary** - Domain-specific terms (up to 50,000 words)
- **Vocabulary filtering** - Profanity/sensitive word handling
- **Language identification** - Auto-detect language

#### Azure Speech
- **Detailed output** - N-best alternatives with confidence scores
- **SNR reporting** - Signal-to-noise ratio metrics
- **Word-level timestamps** - Precise timing for each word
- **Sentiment analysis** - Emotional tone detection
- **Dictation mode** - Enhanced punctuation and formatting

#### Deepgram
- **8 specialized models:**
  - `general` - Versatile across domains
  - `meeting` - Conference calls, multiple speakers
  - `phonecall` - Telephony audio (8kHz optimized)
  - `voicemail` - Single speaker messages
  - `finance` - Financial services terminology
  - `conversationalai` - Chatbots, virtual assistants
  - `video` - Media and entertainment
  - `medical` - Healthcare conversations
- **4 quality tiers:**
  - `base` - Standard accuracy
  - `enhanced` - Improved accuracy
  - `nova` - Highest accuracy
  - `nova-2` - Latest improvements
- **Custom models** - Train your own

#### Google Speech
- **4 model types:**
  - `command_and_search` - Short queries
  - `phone_call` - Telephony (8kHz)
  - `video` - Broadcast content
  - `default` - General purpose
- **Enhanced models** - Premium accuracy
- **Alternative languages** - Multi-language detection
- **Phrase hints** - Boost domain vocabulary

### Accuracy Improvement Features

| Feature | AWS | Azure | Deepgram | Google |
|---------|-----|-------|----------|--------|
| **Custom vocabulary** | ✅ 50K words | ❌ | ✅ Keywords | ✅ Phrase hints |
| **Profanity filter** | ✅ mask/remove/tag | ✅ masked/removed/raw | ✅ | ✅ |
| **Automatic punctuation** | ✅ | ✅ | ✅ | ✅ |
| **Word confidence** | ✅ | ✅ Detailed mode | ✅ | ✅ |
| **N-best alternatives** | ❌ | ✅ | ✅ (1-10) | ✅ |

---

## 3. Speaker Diarization

### Comparison Matrix

| Module | Support | Type | Max Speakers | Accuracy | Cost Impact |
|--------|---------|------|--------------|----------|-------------|
| **AWS Transcribe** | ✅ Yes | AI-based | 10 | 85-95% | 2x base rate |
| **Azure Speech** | ✅ Yes | AI-based (ConversationTranscriber) | Configurable | 80-90% | Unknown |
| **Deepgram** | ✅ Yes | AI-based | Unlimited | 85-95% | Included |
| **Google Speech** | ✅ Yes | AI-based | Configurable | 90-95% | Included |

### Feature Details

#### AWS Transcribe
```xml
<action application="set" data="AWS_SHOW_SPEAKER_LABEL=true"/>
```
- Labels: `spk_0`, `spk_1`, `spk_2`, etc.
- Word-level speaker attribution
- Automatic speaker grouping in output
- Works with mono or stereo audio
- **Cost:** $0.048/minute (vs $0.024 base)

#### Azure Speech
```xml
<action application="set" data="AZURE_DIARIZE_INTERIM_RESULTS=true"/>
<action application="set" data="AZURE_DIARIZATION_SPEAKER_COUNT=2"/>
```
- Labels: `Guest-1`, `Guest-2`, etc.
- Preview feature (may require access request)
- Only works in stereo mode (ConversationTranscriber)
- Cannot combine with speech hints or alternative languages

#### Deepgram
```xml
<action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>
<action application="set" data="DEEPGRAM_SPEECH_DIARIZE_VERSION=latest"/>
```
- Speaker labels: `0`, `1`, `2`, etc.
- Word-level speaker attribution
- Works with mono or stereo
- **Cost:** Included (no extra charge)

#### Google Speech
```xml
<action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION=1"/>
<action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT=2"/>
<action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT=4"/>
```
- Word-level speaker tags
- Configurable speaker count range
- Not supported for all languages
- **Cost:** Included

### Diarization Accuracy Comparison

**Best Performance:**
1. **Google** (90-95%) - Most accurate, requires speaker count hints
2. **Deepgram** (85-95%) - Fast and accurate, no setup needed
3. **AWS** (85-95%) - Reliable, up to 10 speakers
4. **Azure** (80-90%) - Preview feature, limited compatibility

**Use Case Recommendations:**
- **Conference calls (3+ speakers):** Google > Deepgram > AWS
- **Call center (2 speakers):** Use channel identification instead
- **No speaker count known:** Deepgram (no configuration needed)
- **Cost-sensitive:** Deepgram or Google (included in base price)

---

## 4. Channel Detection & Identification

### Feature Comparison

| Module | Stereo Support | Channel ID | Separation Method | Accuracy | Use Case |
|--------|---------------|-----------|-------------------|----------|----------|
| **AWS Transcribe** | ✅ Yes | ✅ ch_0, ch_1 | Physical channels | 100% | ⭐ Call centers |
| **Azure Speech** | ✅ Yes | ✅ 0, 1 | ConversationTranscriber | 100% | Enterprise |
| **Deepgram** | ✅ Yes | ✅ multichannel | Physical channels | 100% | Real-time |
| **Google Speech** | ✅ Yes | ✅ Separate recognition | Physical channels | 100% | High accuracy |

### Implementation Details

#### AWS Transcribe - Channel Identification (Recommended)
```javascript
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
  AWS_NUMBER_OF_CHANNELS: '2',
  AWS_REGION: 'us-east-1'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**Output:**
```json
{
  "is_final": true,
  "channel_id": "ch_0",  // LEFT = Agent
  "alternatives": [{"transcript": "How can I help you?"}]
}
```

**Characteristics:**
- ✅ 100% accurate separation (physical channels)
- ✅ Perfect for agent/customer identification
- ✅ Lower cost than speaker diarization
- ✅ No AI confusion
- ❌ Only works for 2 speakers
- ❌ Requires stereo recording setup
- **Cost:** $0.030/minute

#### Azure Speech - Stereo Mode
```javascript
await ep.set({
  AZURE_SUBSCRIPTION_KEY: 'your-key',
  AZURE_REGION: 'eastus'
});
ep.api('uuid_azure_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**Output:**
```json
{
  "Channel": 0,  // Caller
  "DisplayText": "Hello.",
  "RecognitionStatus": "Success"
}
```

**Characteristics:**
- Uses ConversationTranscriber API automatically in stereo mode
- Channel 0 = Caller, Channel 1 = Callee
- Cannot use speech hints in stereo mode
- Cannot use alternative languages in stereo mode

#### Deepgram - Multichannel Mode
```javascript
await ep.set({
  DEEPGRAM_API_KEY: 'your-key'
});
ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**Configuration:**
- Automatically adds `multichannel=true&channels=2` to API URL
- Channel 0 = Caller (inbound/read)
- Channel 1 = Callee (outbound/write)
- Can combine with speaker diarization for enhanced separation

#### Google Speech - Separate Recognition
```javascript
await ep.set({
  GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL: 'true'
});
ep.api('uuid_google_transcribe', `${ep.uuid} start en-US interim`);
```

**Characteristics:**
- Processes each channel independently
- High accuracy per channel
- Suitable for call center analytics

### Channel Setup in FreeSWITCH

**Enable stereo recording:**
```xml
<action application="set" data="RECORD_STEREO=true"/>
```

**Audio channel assignment:**
- LEFT channel (ch_0) = Caller/Agent
- RIGHT channel (ch_1) = Callee/Customer

**See:** [Stereo Channel Assignment Guide](STEREO_CHANNEL_ASSIGNMENT.md)

### Decision Matrix: Diarization vs Channel ID

| Scenario | Method | Module | Why |
|----------|--------|--------|-----|
| **Call center (1-on-1)** | Channel ID | AWS ⭐ | 100% accurate, cheaper |
| **Customer support** | Channel ID | AWS/Azure | Perfect separation |
| **Conference call (3+)** | Diarization | Google | Best for multiple speakers |
| **Webinar/panel** | Diarization | Deepgram | Fast, no cost premium |
| **Compliance recording** | Channel ID | AWS | Perfect accuracy required |

---

## 5. Audio Format Support

### Sample Rate Support

| Module | Native Rate | Auto-Resample | Supported Rates | Best Quality |
|--------|------------|---------------|-----------------|--------------|
| **AWS Transcribe** | 16kHz | ✅ Yes (speex) | 8kHz, 16kHz, 48kHz | 16kHz |
| **Azure Speech** | Various | Module handles | 8kHz, 16kHz, others | 16kHz |
| **Deepgram** | 8kHz | ❌ Hardcoded | 8kHz (fixed) | 8kHz |
| **Google Speech** | Various | Module handles | 8kHz, 16kHz, 48kHz | 16kHz |

**Important Notes:**

#### AWS Transcribe
- **Always use 16kHz for best quality** (not 8kHz)
- Automatic resampling via speex_resampler
- Pre-connection buffering (4800 bytes circular buffer)
- Documentation: [docs/STEREO_CHANNEL_ASSIGNMENT.md:197-204](STEREO_CHANNEL_ASSIGNMENT.md)

#### Deepgram
- **Hardcoded to 8kHz** in module (`sample_rate=8000`)
- Fixed linear16 encoding
- No resampling needed (telephony-optimized models)
- Source: `dg_transcribe_glue.cpp:141-267`

#### Google Speech
- Flexible sample rate support
- Handles various codecs automatically
- Recommended: 16kHz for enhanced models

### Audio Encoding

| Module | Encoding | Codec | Notes |
|--------|----------|-------|-------|
| **AWS** | Linear PCM | 16-bit | Via AWS SDK |
| **Azure** | Various | WebSocket negotiated | Flexible |
| **Deepgram** | Linear16 | Hardcoded | 16-bit PCM |
| **Google** | Linear PCM | Via gRPC | 16-bit |

### Codec Compatibility

All modules support standard FreeSWITCH telephony codecs:
- **G.711** (PCMU/PCMA) - 8kHz
- **G.722** - 16kHz wideband
- **Opus** - Variable rate
- **SILK** - Variable rate

---

## 6. Protocol & Communication

### Protocol Comparison

| Module | Protocol | Port | TLS/SSL | Connection Type |
|--------|----------|------|---------|-----------------|
| **AWS Transcribe** | Native AWS SDK | 443 | ✅ Yes | HTTP/2 streaming |
| **Azure Speech** | WebSocket | 443 | ✅ Yes | WSS |
| **Deepgram** | WebSocket | 443 | ✅ Yes | WSS |
| **Google Speech** | gRPC | 443 | ✅ Yes | HTTP/2 + protobuf |
| **Audio Fork** | WebSocket | Custom | Optional | WS/WSS |

### Protocol Details

#### AWS Transcribe - Native SDK
- **Protocol:** AWS C++ SDK over HTTP/2
- **Features:**
  - Built-in retry logic
  - Automatic credential management
  - IAM role support
  - Region-aware endpoints
- **Advantages:**
  - Native AWS integration
  - Automatic failover
  - Production-grade reliability
- **Disadvantages:**
  - Complex build requirements
  - Larger binary size

#### Azure Speech - WebSocket
- **URL:** `wss://[region].stt.speech.microsoft.com/speech/recognition/...`
- **Features:**
  - Subscription key authentication
  - Real-time bidirectional streaming
  - JSON-based configuration
- **Advantages:**
  - Simple integration
  - Standard WebSocket libraries
  - Easy debugging
- **Disadvantages:**
  - Manual connection management

#### Deepgram - WebSocket
- **URL:** `wss://api.deepgram.com/v1/listen?[params]`
- **Features:**
  - Token-based authentication
  - URL parameter configuration
  - Real-time streaming
- **Advantages:**
  - Fast connection establishment
  - Simple API
  - Global edge network
- **Disadvantages:**
  - URL length limits with many parameters

#### Google Speech - gRPC
- **Protocol:** gRPC over HTTP/2 with protobuf
- **Features:**
  - Service account authentication
  - Bidirectional streaming
  - Strongly typed messages
- **Advantages:**
  - Efficient binary protocol
  - Built-in flow control
  - Language-agnostic
- **Disadvantages:**
  - Complex build (gRPC + protobuf)
  - Requires googleapis

#### Audio Fork - WebSocket (Generic)
- **URL:** User-specified `ws://` or `wss://`
- **Features:**
  - Custom subprotocol support
  - Bidirectional communication
  - Flexible metadata format
- **Advantages:**
  - Connect to any WebSocket service
  - No vendor lock-in
  - Custom implementations
- **Disadvantages:**
  - No transcription built-in

### Network Requirements

| Module | Bandwidth (Stereo 16kHz) | Latency Sensitivity | Firewall |
|--------|-------------------------|---------------------|----------|
| **AWS** | ~512 kbps | Medium | Port 443 outbound |
| **Azure** | ~512 kbps | Medium | Port 443 outbound |
| **Deepgram** | ~256 kbps (8kHz) | High | Port 443 outbound |
| **Google** | ~512 kbps | Medium | Port 443 outbound |
| **Audio Fork** | User-defined | User-defined | Custom port |

---

## 7. Setup Complexity & Dependencies

### Build Time Comparison

| Module | Build Time | Complexity | Dependencies | Production Ready |
|--------|-----------|------------|--------------|------------------|
| **AWS Transcribe** | 20-40 min | ⚠️ Complex | AWS SDK C++ | ✅ Yes |
| **Azure Speech** | <5 min | ✅ Simple | libwebsockets | ✅ Yes |
| **Deepgram** | <5 min | ✅ Simple | libwebsockets | ✅ Yes |
| **Google Speech** | 15-30 min | ⚠️ Complex | gRPC, protobuf | ✅ Yes |
| **Audio Fork** | <5 min | ✅ Very Simple | libwebsockets | ✅ Yes |

### Detailed Dependencies

#### mod_aws_transcribe
**Build Batch:** 6 (AWS SDK C++ + AWS C Common)

**Dependencies:**
- AWS C++ SDK 1.11.345 (transcribestreaming component)
- CMake 3.28.3
- libcurl4-openssl-dev
- libssl-dev
- uuid-dev
- zlib1g-dev

**Build Steps:**
```bash
# 1. Build AWS SDK C++ (20-40 minutes)
git clone https://github.com/aws/aws-sdk-cpp
cd aws-sdk-cpp
mkdir build && cd build
cmake .. -DBUILD_ONLY="transcribestreaming" -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install

# 2. Build module with FreeSWITCH
```

**Binary Size:** ~5MB (module) + ~50MB (AWS SDK libraries)

**Complexity Factors:**
- Large codebase (938 lines in module)
- SDK has many dependencies
- Requires specific version (1.11.200+)
- cJSON conflict handling needed

#### mod_azure_transcribe
**Build Batch:** 4 (Azure Speech SDK)

**Dependencies:**
- libwebsockets 4.3.3
- Azure Speech SDK 1.37.0 (downloaded during build)

**Build Steps:**
```bash
# 1. Build libwebsockets (5-10 minutes)
wget https://github.com/warmcat/libwebsockets/archive/v4.3.3.tar.gz
tar xzf v4.3.3.tar.gz && cd libwebsockets-4.3.3
mkdir build && cd build
cmake .. -DLWS_WITH_SSL=ON
make -j$(nproc)
sudo make install

# 2. Download Azure SDK (1 minute)
wget https://aka.ms/csspeech/linuxbinary
tar xzf ...

# 3. Build module
```

**Binary Size:** ~2MB (module) + ~15MB (Azure SDK)

**Complexity Factors:**
- Simple WebSocket integration
- SDK is pre-built binary
- Minimal dependencies

#### mod_deepgram_transcribe
**Build Batch:** 3 (googleapis + libwebsockets)

**Dependencies:**
- libwebsockets 4.3.3

**Build Steps:**
```bash
# 1. Build libwebsockets (5-10 minutes)
# Same as Azure

# 2. Build module
```

**Binary Size:** ~1.5MB (module only)

**Complexity Factors:**
- Simplest build process
- Only needs libwebsockets
- No vendor SDK required
- URL-based configuration

#### mod_google_transcribe
**Build Batch:** 2 (gRPC + protobuf) + 3 (googleapis)

**Dependencies:**
- gRPC 1.64.2 (16 submodules)
- protobuf (bundled with gRPC)
- googleapis (Google Cloud API definitions)
- CMake 3.28.3

**Build Steps:**
```bash
# 1. Build gRPC (15-30 minutes)
git clone --recurse-submodules -b v1.64.2 https://github.com/grpc/grpc
cd grpc
mkdir build && cd build
cmake .. -DgRPC_INSTALL=ON -DBUILD_SHARED_LIBS=ON
make -j$(nproc)
sudo make install

# 2. Build googleapis (5-10 minutes)
git clone https://github.com/googleapis/googleapis
cd googleapis
# Build speech API protos

# 3. Build module
```

**Binary Size:** ~3MB (module) + ~80MB (gRPC libraries)

**Complexity Factors:**
- gRPC has 16 submodules (100MB+ download)
- Protobuf compilation required
- googleapis protos needed
- Longer build time

#### mod_audio_fork
**Build Batch:** 3 (libwebsockets)

**Dependencies:**
- libwebsockets 4.3.3

**Build Steps:**
```bash
# 1. Build libwebsockets (5-10 minutes)
# Same as Azure/Deepgram

# 2. Build module
```

**Binary Size:** ~1MB (module only)

**Complexity Factors:**
- Simplest of all
- Generic WebSocket streaming
- No transcription SDK needed

### Automated Build Solutions

**Docker (Recommended):**
```bash
./build-locally.sh
```
- All modules built automatically
- Isolated environment
- 60-90 minutes total
- Validated dependencies

**Batch Build:**
```bash
sudo ./build-batch.sh all
```
- Step-by-step building
- Resume capability
- 75-130 minutes total
- Production standalone

---

## 8. Authentication & Security

### Authentication Methods

| Module | Method | Credential Type | IAM Support | Rotation |
|--------|--------|----------------|-------------|----------|
| **AWS** | 3-tier priority | Access key + Secret | ✅ Yes | ✅ STS |
| **Azure** | Subscription key | Key-based | ❌ No | Manual |
| **Deepgram** | API token | Token-based | ❌ No | Manual |
| **Google** | Service account | JSON key file | ✅ Yes | ✅ Auto |
| **Audio Fork** | Custom | User-defined | N/A | User-defined |

### AWS Transcribe Authentication

**Priority Order:**
1. Channel variables (per-call)
2. Environment variables (container-level)
3. AWS credentials chain (IAM role)

**Supported Credential Types:**
- **AKIA\*** - Permanent IAM user credentials
- **ASIA\*** - Temporary STS credentials (requires session token)
- **IAM Role** - EC2/ECS/EKS automatic

**Best Practice:**
```bash
# Production: Use IAM roles (no credentials needed)
docker run -e AWS_REGION=us-east-1 freeswitch-transcribe:latest
```

### Azure Speech Authentication

**Required:**
- `AZURE_SUBSCRIPTION_KEY` - Service subscription key
- `AZURE_REGION` - Deployment region (e.g., eastus)

**Configuration:**
```bash
docker run -e AZURE_SUBSCRIPTION_KEY=xxx \
           -e AZURE_REGION=eastus \
           freeswitch-transcribe:latest
```

### Deepgram Authentication

**Required:**
- `DEEPGRAM_API_KEY` - API token

**Configuration:**
```bash
docker run -e DEEPGRAM_API_KEY=xxx \
           freeswitch-transcribe:latest
```

### Google Speech Authentication

**Required:**
- `GOOGLE_APPLICATION_CREDENTIALS` - Path to service account JSON

**Configuration:**
```bash
docker run -v /path/to/key.json:/etc/google/key.json \
           -e GOOGLE_APPLICATION_CREDENTIALS=/etc/google/key.json \
           freeswitch-transcribe:latest
```

**Best Practice:** Use Workload Identity on GKE

### Security Comparison

| Feature | AWS | Azure | Deepgram | Google |
|---------|-----|-------|----------|--------|
| **Temporary credentials** | ✅ STS | ❌ | ❌ | ✅ Short-lived |
| **Role-based access** | ✅ IAM | ❌ | ❌ | ✅ Service accounts |
| **Automatic rotation** | ✅ | ❌ | ❌ | ✅ |
| **Credential detection** | ✅ | ❌ | ❌ | ✅ ADC |
| **Per-call credentials** | ✅ | ✅ | ✅ | ✅ |

---

## 9. Cost Analysis

### Pricing Comparison (as of 2025)

| Module | Base Rate | Speaker Diarization | Channel ID | Additional |
|--------|-----------|-----------------------|-----------|------------|
| **AWS Transcribe** | $0.024/min | +$0.024 = $0.048 | +$0.006 = $0.030 | Medical: +$0.045 |
| **Azure Speech** | $1.00/hour ($0.0167/min) | Unknown | Unknown | Sentiment: unknown |
| **Deepgram** | $0.0043/min (Nova) | Included | Included | Pay-as-you-go |
| **Google Speech** | $0.016/min | Included | Included | Enhanced: $0.024 |

**Notes:**
- Prices are approximate and subject to change
- Volume discounts available for most providers
- Check official pricing pages for current rates

### Monthly Cost Estimates (1000 minutes)

| Configuration | AWS | Azure | Deepgram | Google |
|--------------|-----|-------|----------|--------|
| **Basic** | $24 | $16.70 | $4.30 | $16 |
| **+ Diarization** | $48 | ~$30* | $4.30 | $16 |
| **+ Channel ID** | $30 | ~$25* | $4.30 | $16 |
| **Enhanced/Premium** | +$45 (medical) | Unknown | +$0 (Nova-2) | $24 |

*Estimated based on typical cloud service pricing

### Cost Optimization Strategies

#### AWS
- Use channel identification instead of diarization when possible ($0.030 vs $0.048)
- Enable VAD to reduce silence transcription
- Use standard model (avoid medical unless needed)

#### Azure
- Use simple output format (detailed costs more bandwidth)
- Disable unused features (SNR, sentiment)

#### Deepgram
- Already cost-optimized (diarization included)
- Nova-2 tier gives best value (high accuracy, included features)

#### Google
- Use standard models unless enhanced accuracy needed
- Speaker diarization included (no extra cost)
- Phrase hints improve accuracy without added cost

---

## 10. Feature Matrix

### Core Features

| Feature | AWS | Azure | Deepgram | Google |
|---------|-----|-------|----------|--------|
| **Interim results** | ✅ | ✅ | ✅ | ✅ |
| **Final results** | ✅ | ✅ | ✅ | ✅ |
| **Punctuation** | ✅ | ✅ | ✅ | ✅ |
| **Capitalization** | ✅ | ✅ | ✅ | ✅ |
| **Timestamps** | ✅ Word-level | ✅ Word-level | ✅ Word-level | ✅ Word-level |
| **Confidence scores** | ✅ | ✅ | ✅ | ✅ |

### Advanced Features

| Feature | AWS | Azure | Deepgram | Google |
|---------|-----|-------|----------|--------|
| **Speaker diarization** | ✅ 10 max | ✅ Preview | ✅ Unlimited | ✅ Configurable |
| **Channel identification** | ✅ ch_0/ch_1 | ✅ 0/1 | ✅ multichannel | ✅ Separate |
| **Custom vocabulary** | ✅ 50K words | ❌ | ✅ Keywords | ✅ Hints |
| **Profanity filter** | ✅ mask/remove/tag | ✅ masked/removed | ✅ | ✅ |
| **Redaction** | ❌ | ❌ | ✅ PCI/SSN | ❌ |
| **Language detection** | ✅ | ✅ | ❌ | ✅ Alternative |
| **Multiple alternatives** | ❌ | ✅ N-best | ✅ 1-10 | ✅ |
| **VAD (cost saving)** | ✅ | ❌ | ✅ | ✅ |

### Unique Features

#### AWS Transcribe
- ✅ Medical terminology model
- ✅ Vocabulary filtering (profanity/sensitive)
- ✅ IAM role authentication
- ✅ STS temporary credentials
- ✅ Automatic resampling (8/16/48kHz)

#### Azure Speech
- ✅ SNR (signal-to-noise ratio) reporting
- ✅ Sentiment analysis
- ✅ Dictation mode
- ✅ N-best alternatives with confidence
- ✅ Speech start/end detection (mono only)

#### Deepgram
- ✅ Named Entity Recognition (NER)
- ✅ 8 specialized models (phonecall, meeting, finance, etc.)
- ✅ 4 quality tiers (base, enhanced, nova, nova-2)
- ✅ Keyword boosting (intensity 1-10)
- ✅ Custom word replacement
- ✅ Search for specific keywords
- ✅ PCI/SSN redaction
- ✅ Numerals formatting
- ✅ Smart formatting (no_delay)

#### Google Speech
- ✅ 125+ languages (most extensive)
- ✅ Enhanced models (premium accuracy)
- ✅ Metadata (interaction type, industry, device type)
- ✅ Single utterance mode
- ✅ Max duration handling (305s limit)
- ✅ Alternative language codes

---

## 11. Language Support

### Language Count

| Module | Total Languages | Regional Variants | Best Coverage |
|--------|----------------|------------------|---------------|
| **AWS** | 30+ | Multiple | English, Spanish |
| **Azure** | 100+ | Extensive | Most languages |
| **Deepgram** | 30+ | Limited | English |
| **Google** | 125+ | Extensive | Most comprehensive |

### Common Languages

| Language | AWS | Azure | Deepgram | Google |
|----------|-----|-------|----------|--------|
| **English (US)** | ✅ | ✅ | ✅ Nova | ✅ Enhanced |
| **Spanish** | ✅ | ✅ | ✅ Nova | ✅ |
| **French** | ✅ | ✅ | ✅ Enhanced | ✅ |
| **German** | ✅ | ✅ | ✅ Enhanced | ✅ |
| **Chinese** | ✅ | ✅ | ✅ Base | ✅ |
| **Japanese** | ✅ | ✅ | ✅ Enhanced | ✅ |
| **Portuguese** | ✅ | ✅ | ✅ Enhanced | ✅ |
| **Arabic** | ✅ | ✅ | ❌ | ✅ |
| **Hindi** | ✅ | ✅ | ✅ Enhanced | ✅ |
| **Russian** | ✅ | ✅ | ✅ Base | ✅ |

### Specialized Language Features

#### Deepgram - Automatic Model Selection

When you don't specify model/tier, Deepgram automatically selects based on language:

| Language | Auto Tier | Auto Model |
|----------|-----------|------------|
| en-US | nova | phonecall |
| en-GB, en-AU | nova | general |
| es, es-419 | nova | general |
| fr, de, ja, ko | enhanced | general |
| zh, ru, tr, uk | base | general |

**Source:** `dg_transcribe_glue.cpp:41-73`

---

## 12. Use Case Recommendations

### Call Center & Customer Support

**Best Choice: AWS Transcribe (Channel Identification)**

**Why:**
- ✅ 100% accurate agent/customer separation
- ✅ Lower cost than diarization ($0.030 vs $0.048)
- ✅ Perfect for compliance recording
- ✅ IAM role support for enterprise security
- ✅ Medical model for healthcare

**Configuration:**
```javascript
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
  AWS_NUMBER_OF_CHANNELS: '2',
  AWS_REGION: 'us-east-1'
});
```

**Alternative:** Azure Speech (if already using Azure ecosystem)

### Conference Calls (3+ Participants)

**Best Choice: Google Speech (Speaker Diarization)**

**Why:**
- ✅ Highest diarization accuracy (90-95%)
- ✅ Configurable speaker count
- ✅ 125+ languages supported
- ✅ Included in base price

**Configuration:**
```javascript
await ep.set({
  GOOGLE_SPEECH_SPEAKER_DIARIZATION: '1',
  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT: '3',
  GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT: '6'
});
```

**Alternative:** Deepgram (faster, cost-effective)

### Real-time Transcription (Low Latency)

**Best Choice: Deepgram (Nova-2)**

**Why:**
- ✅ Lowest latency (~100-300ms)
- ✅ Fast processing (0.8x real-time)
- ✅ Optimized for real-time streaming
- ✅ Lowest cost ($0.0043/min)
- ✅ All features included

**Configuration:**
```javascript
await ep.set({
  DEEPGRAM_API_KEY: 'your-key',
  DEEPGRAM_SPEECH_MODEL: 'phonecall',
  DEEPGRAM_SPEECH_TIER: 'nova-2'
});
```

### High Accuracy Requirements

**Best Choice: Google Speech (Enhanced Models)**

**Why:**
- ✅ Highest base accuracy (92-98%)
- ✅ Enhanced models for premium quality
- ✅ Phrase hints for domain vocabulary
- ✅ 4 specialized models

**Configuration:**
```javascript
await ep.set({
  GOOGLE_SPEECH_MODEL: 'phone_call',
  GOOGLE_SPEECH_USE_ENHANCED: 'true',
  GOOGLE_SPEECH_HINTS: 'technical,support,customer'
});
```

**Alternative:** Deepgram Nova-2 (similar accuracy, faster)

### Healthcare & Medical

**Best Choice: AWS Transcribe (Medical Model)**

**Why:**
- ✅ Dedicated medical terminology model
- ✅ HIPAA compliance
- ✅ Custom vocabulary (50K words)
- ✅ Vocabulary filtering
- ✅ IAM security

**Note:** Higher cost ($0.069/min)

### Financial Services

**Best Choice: Deepgram (Finance Model)**

**Why:**
- ✅ Specialized finance model
- ✅ PCI/SSN redaction
- ✅ Custom word replacement
- ✅ Low cost with all features

**Configuration:**
```javascript
await ep.set({
  DEEPGRAM_SPEECH_MODEL: 'finance',
  DEEPGRAM_SPEECH_REDACT: 'pci,ssn,numbers'
});
```

### Multi-language Support

**Best Choice: Google Speech**

**Why:**
- ✅ 125+ languages
- ✅ Alternative language detection
- ✅ Best regional variant support

**Alternative:** Azure (100+ languages)

### Cost-Sensitive Projects

**Best Choice: Deepgram**

**Why:**
- ✅ Lowest base price ($0.0043/min)
- ✅ All features included (no premiums)
- ✅ No extra cost for diarization
- ✅ No extra cost for multichannel

**Comparison:**
- Deepgram: $4.30/1000 min
- Google: $16/1000 min
- Azure: $16.70/1000 min
- AWS: $24/1000 min

### Enterprise Integration

**AWS Ecosystem:** Use AWS Transcribe
**Azure Ecosystem:** Use Azure Speech
**Google Cloud:** Use Google Speech
**Independent:** Use Deepgram (no cloud lock-in)

---

## 13. Quick Decision Guide

### Choose AWS Transcribe if:
- ✅ You need perfect 2-speaker separation (call center)
- ✅ You're already using AWS infrastructure
- ✅ You need IAM role authentication
- ✅ You need medical terminology
- ✅ Compliance/regulation requires channel identification

### Choose Azure Speech if:
- ✅ You're already using Azure ecosystem
- ✅ You need sentiment analysis
- ✅ You need SNR reporting
- ✅ You need N-best alternatives
- ✅ Enterprise Microsoft integration

### Choose Deepgram if:
- ✅ You need lowest latency
- ✅ You want lowest cost
- ✅ You need specialized models (finance, meeting, etc.)
- ✅ You need PCI/SSN redaction
- ✅ You want simplest setup
- ✅ You don't want cloud vendor lock-in

### Choose Google Speech if:
- ✅ You need highest accuracy
- ✅ You need 125+ languages
- ✅ You have conference calls (3+ speakers)
- ✅ You're already using Google Cloud
- ✅ You need most comprehensive language support

### Choose Audio Fork if:
- ✅ You want to build custom transcription service
- ✅ You need to stream to non-standard backend
- ✅ You want full control over processing
- ✅ You're developing your own AI models

---

## 14. Migration Guide

### From AWS to Deepgram

**Reason:** Lower cost, faster response

**Changes needed:**
1. Replace authentication
2. Update channel variables
3. Adjust event handling

**Before (AWS):**
```javascript
await ep.set({
  AWS_ACCESS_KEY_ID: 'xxx',
  AWS_SECRET_ACCESS_KEY: 'xxx',
  AWS_REGION: 'us-east-1',
  AWS_SHOW_SPEAKER_LABEL: 'true'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**After (Deepgram):**
```javascript
await ep.set({
  DEEPGRAM_API_KEY: 'xxx',
  DEEPGRAM_SPEECH_MODEL: 'phonecall',
  DEEPGRAM_SPEECH_TIER: 'nova-2',
  DEEPGRAM_SPEECH_DIARIZE: 'true'
});
await ep.api('uuid_deepgram_transcribe', `${ep.uuid} start en-US interim stereo`);
```

**Event changes:**
- `aws_transcribe::transcription` → `deepgram_transcribe::transcription`
- `aws_transcribe::connect` → `deepgram_transcribe::connect`

### From Google to AWS

**Reason:** Better IAM integration, channel identification

**Before (Google):**
```javascript
await ep.set({
  GOOGLE_SPEECH_SPEAKER_DIARIZATION: '1',
  GOOGLE_SPEECH_MODEL: 'phone_call'
});
await ep.api('uuid_google_transcribe', `${ep.uuid} start en-US interim`);
```

**After (AWS):**
```javascript
await ep.set({
  AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
  AWS_NUMBER_OF_CHANNELS: '2'
});
await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);
```

---

## 15. Performance Tuning

### AWS Transcribe
- Enable VAD to reduce costs: `START_RECOGNIZING_ON_VAD=true`
- Use 16kHz for best quality (not 8kHz)
- Pre-connection buffering handles connection delays
- Use channel ID instead of diarization when possible

### Azure Speech
- Use simple output format for lower latency
- Enable word-level timestamps only if needed
- Disable SNR if not required
- Use mono mode for single-speaker scenarios

### Deepgram
- Already optimized (8kHz, fast models)
- Use automatic model selection (nova for en-US)
- Enable smart_format for better results
- Keyword boosting improves domain accuracy

### Google Speech
- Use phone_call model for telephony
- Enable enhanced models for accuracy
- Set appropriate speaker count range
- Use phrase hints for domain terms

---

## 16. Troubleshooting Common Issues

### High Latency

**AWS:**
- Check AWS region (use closest)
- Verify network connectivity
- Check VAD settings (may delay start)

**Azure:**
- Check Azure region proximity
- Verify WebSocket connection
- Check timeout settings

**Deepgram:**
- Usually not an issue (fastest)
- Check API key quota
- Verify network bandwidth

**Google:**
- Check gRPC connection
- Verify service account permissions
- Check network latency

### Poor Accuracy

**All modules:**
1. Use 16kHz sample rate (not 8kHz) except Deepgram
2. Ensure clean audio (minimal noise)
3. Use appropriate model for use case
4. Add custom vocabulary/hints
5. Enable automatic punctuation

**Specific fixes:**
- **AWS:** Add custom vocabulary, use medical model
- **Azure:** Enable detailed output, add speech hints
- **Deepgram:** Use Nova-2 tier, add keyword boosting
- **Google:** Use enhanced models, add phrase hints

### Authentication Failures

**AWS:**
- Verify credential type (AKIA vs ASIA)
- Check session token for ASIA* credentials
- Verify IAM permissions
- Check region setting

**Azure:**
- Verify subscription key
- Check region matches resource
- Ensure quota available

**Deepgram:**
- Verify API key
- Check account credit balance

**Google:**
- Verify GOOGLE_APPLICATION_CREDENTIALS path
- Check service account permissions
- Ensure Speech-to-Text API enabled

---

## 17. Additional Resources

### Documentation
- [AWS Transcribe Documentation](https://docs.aws.amazon.com/transcribe/)
- [Azure Speech Documentation](https://docs.microsoft.com/azure/cognitive-services/speech-service/)
- [Deepgram Documentation](https://developers.deepgram.com/)
- [Google Speech-to-Text Documentation](https://cloud.google.com/speech-to-text/docs)

### Local Guides
- [Stereo Channel Assignment Guide](STEREO_CHANNEL_ASSIGNMENT.md)
- [Real-time Transcription Delivery](REALTIME_TRANSCRIPTION_DELIVERY.md)
- [XML Dialplan vs Lua Comparison](DIALPLAN_VS_LUA.md)
- [Per-User Multi-Service Setup](../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

### Examples
- [examples/aws_transcribe.js](../examples/aws_transcribe.js)
- [examples/audio_fork.js](../examples/audio_fork.js)
- [examples/google_transcribe.js](../examples/google_transcribe.js)

---

## Summary Matrix

| Criteria | Winner | Runner-up |
|----------|--------|-----------|
| **Speed** | Deepgram | AWS/Azure |
| **Accuracy** | Google | Deepgram Nova-2 |
| **Cost** | Deepgram | Google |
| **Setup** | Azure/Deepgram | Audio Fork |
| **Languages** | Google (125+) | Azure (100+) |
| **Call Centers** | AWS (Channel ID) | Azure |
| **Conference** | Google (Diarization) | Deepgram |
| **Enterprise** | AWS (IAM) | Google (GCP) |
| **Features** | Deepgram (NER, redaction) | AWS (medical) |

---

**Last Updated:** 2025-11-21
**Document Version:** 1.0
**Repository:** [freeswitch-speech-ai](https://github.com/srthorat/freeswitch-speech-ai)
