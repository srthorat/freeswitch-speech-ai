# Google Cloud Speech-to-Text V2 API - Comprehensive Analysis

**Date:** 2025-11-25
**Current Module:** mod_google_transcribe (using v1p1beta1)
**Analysis Focus:** V2 API Streaming Features & Protocol Support

---

## 📋 Executive Summary

Google Cloud Speech-to-Text V2 API represents a major evolution with enterprise-grade features, the Chirp 3 model, and enhanced regional deployment capabilities. **Streaming is exclusively available via gRPC bidirectional streaming** - no native REST or WebSocket support exists for real-time transcription.

---

## 🔄 API Evolution Timeline

| Version | Status | Key Note |
|---------|--------|----------|
| **v1beta1** | Deprecated (April 2017) | Still available but not recommended |
| **v1** | Generally Available | Current production standard |
| **v1p1beta1** | Beta | Extended features (used in current module) |
| **v2** | Generally Available (2024) | Latest with Chirp 3, enterprise features |

---

## 🌐 Protocol Support Analysis

### ✅ Supported Protocols

#### 1. **gRPC (Primary - Required for Streaming)**
- **Streaming Method:** `StreamingRecognize` - Bidirectional streaming
- **Non-Streaming Methods:**
  - `Recognize` - Synchronous (< 1 minute audio)
  - `BatchRecognize` - Asynchronous (1 minute - 1 hour audio)
- **Best For:** Low latency, real-time transcription, server-to-server
- **Limitation:** Not directly compatible with web browsers

#### 2. **REST over HTTP/1.1 (Non-Streaming Only)**
- **Supported:** `Recognize` and `BatchRecognize` methods
- **NOT Supported:** Streaming recognition
- **Audio Format:** Must be Base64-encoded for JSON serialization
- **Best For:** Batch processing, simple synchronous requests

### ❌ NOT Supported Protocols

#### WebSocket
- **Status:** Not natively supported by Google Speech-to-Text V2
- **Workaround:** Requires intermediary server acting as gRPC↔WebSocket bridge
- **Architecture Pattern:**
  ```
  Browser (WebSocket) → Intermediary Server (WebSocket + gRPC) → Google Speech API (gRPC)
  ```
- **Alternative:** Use gRPC-Web (supports server streaming but not client streaming)

---

## 🎯 Complete Feature List - V2 API

### Core Recognition Features (`RecognitionFeatures`)

| Feature | Field Name | Type | Description |
|---------|-----------|------|-------------|
| **Automatic Punctuation** | `enable_automatic_punctuation` | Boolean | Adds punctuation to transcripts (select languages) |
| **Spoken Punctuation** | `enable_spoken_punctuation` | Boolean | Converts "question mark" → "?" |
| **Word Time Offsets** | `enable_word_time_offsets` | Boolean | Start/end timestamps for each word |
| **Word Confidence** | `enable_word_confidence` | Boolean | Per-word confidence scores |
| **Profanity Filter** | `profanity_filter` | Boolean | Filters profanity (e.g., "f***") |
| **Multi-Channel Mode** | `multi_channel_mode` | Enum | Handle multi-channel audio files |
| **Speaker Diarization** | `diarization_config` | Object | Identify different speakers |

### Streaming-Specific Features (`StreamingRecognitionFeatures`)

| Feature | Field Name | Type | Description |
|---------|-----------|------|-------------|
| **Voice Activity Events** | `enable_voice_activity_events` | Boolean | Real-time voice activity detection |
| **Interim Results** | `interim_results` | Boolean | Stream partial transcripts before final |
| **Voice Activity Timeout** | `voice_activity_timeout` | Object | Auto-close stream after silence |
| └─ Speech Start Timeout | `speech_start_timeout` | Duration | Timeout for initial speech detection |
| └─ Speech End Timeout | `speech_end_timeout` | Duration | Timeout after last speech event |

### Advanced Features

#### 1. **Speech Adaptation**
- **Phrase Sets:** Boost recognition of specific phrases (0-20 boost value)
- **Custom Classes:** Domain-specific vocabulary groups
- **Capacity:** Up to 1,000 phrases per request
- **Use Cases:** Medical terminology, product names, technical jargon

#### 2. **Speaker Diarization (Chirp 3)**
- **Supported Languages:** 14 languages (EN, ES, FR, DE, ZH, JA, KO, PT, IT, HI)
- **Channel Support:** Single-channel audio only
- **Output:** Speaker labels for each word/utterance
- **Configuration:** Min/max speaker count settings

#### 3. **Automatic Language Detection**
- **Capability:** Detects dominant language automatically
- **Multi-Language:** Specify alternative language codes (up to 3)
- **Language-Agnostic Mode:** Available with Chirp models

#### 4. **Audio Enhancement**
- **Built-in Denoiser:** Reduces background noise and music
- **SNR Threshold:** Signal-to-noise ratio filtering (0-1000 recommended)
- **Auto-Detection:** Automatically detects sample rate, channels, encoding

#### 5. **Translation** (Chirp 3)
- **Capability:** Automatic translation to target language
- **Models:** Available for supported Chirp 3 language pairs

#### 6. **Transcript Normalization**
- **Auto-Replacement:** Standardizes transcript segments
- **Use Case:** Consistent formatting of numbers, dates, currencies

#### 7. **Output Format Options**
- **Native:** Protocol Buffer serialization
- **VTT:** WebVTT caption format
- **SRT:** SubRip subtitle format

---

## 🎵 Audio Encoding Support

### Audio Chunk Size Limit
- **Maximum:** 25 KB per request message
- **Applies To:** Initial request AND each individual stream message
- **Error:** Exceeding limit throws error

### Supported Encodings

#### Lossless Formats (Recommended)

| Codec | Type | Sample Rate | Bit Depth | Auto-Detect | Notes |
|-------|------|-------------|-----------|-------------|-------|
| **LINEAR16** | PCM | 8000-48000 Hz | 16-bit | ✅ WAV | Headerless or WAV container |
| **FLAC** | Free Lossless | 8000-48000 Hz | 16/24-bit | ✅ | Must include header |

#### Lossy Formats (Use with Caution)

| Codec | Sample Rate | Auto-Detect | Notes |
|-------|-------------|-------------|-------|
| **MULAW** | 8000 Hz typical | ✅ WAV | 8-bit μ-law PCM |
| **ALAW** | 8000 Hz typical | ✅ WAV | 8-bit A-law PCM |
| **AMR** | 8000 Hz only | ❌ | Adaptive Multi-Rate Narrowband |
| **AMR_WB** | 16000 Hz only | ❌ | Adaptive Multi-Rate Wideband |
| **OGG_OPUS** | 8k/12k/16k/24k/48k Hz | ✅ | Opus in Ogg container |
| **WEBM_OPUS** | 8k/12k/16k/24k/48k Hz | ✅ | Opus in WebM container |
| **SPEEX_WITH_HEADER_BYTE** | 16000 Hz only | ❌ | Speex wideband |
| **MP3** | Various | ✅ | MPEG Audio Layer 3 |
| **MP4_AAC** | Various | ✅ | AAC in MP4 container |
| **M4A_AAC** | Various | ✅ | AAC in M4A container |
| **MOV_AAC** | Various | ✅ | AAC in MOV container |

### Sample Rate Recommendations

| Sample Rate | Recommendation | Use Case |
|-------------|---------------|----------|
| **16000 Hz** | ✅ **Optimal** | Best quality/performance balance |
| **8000 Hz** | ⚠️ Acceptable | Telephony, lower quality acceptable |
| **< 8000 Hz** | ❌ Not Recommended | Impairs recognition accuracy |
| **> 16000 Hz** | ⚠️ No Benefit | No accuracy improvement, larger files |
| **48000 Hz** | ⚠️ Supported | Broadcast quality, no accuracy gain |

### Channel Support
- **Mono:** Single channel (default)
- **Stereo:** Dual channel (2 channels)
- **Multi-Channel:** Up to 8 channels supported
- **Separate Recognition:** Optional per-channel transcription

---

## 🤖 Chirp Model Capabilities

### Model Variants

| Model ID | Availability | Best For |
|----------|--------------|----------|
| **chirp_3** | V2 only (GA) | Latest generation, most accurate |
| **chirp_2** | V2 only | Enhanced multilingual accuracy |
| **chirp** | V2 only | Original universal speech model |

### Chirp 3 Specifications

#### Supported Recognition Methods
- ✅ StreamingRecognize (real-time)
- ✅ Recognize (< 1 minute)
- ✅ BatchRecognize (1 min - 1 hour)

#### Language Support
- **Transcription:** 70+ languages and locales (GA)
- **Diarization:** 14 languages
- **Automatic Detection:** Language-agnostic mode available

#### Regional Availability
- **GA Regions:** US multi-region, EU multi-region, asia-northeast1, asia-southeast1
- **Preview Regions:** asia-south1, europe-west2, europe-west3

#### Feature Matrix

| Feature | Chirp 3 Support | Notes |
|---------|----------------|-------|
| Automatic Punctuation | ✅ Auto-generated | Can be disabled |
| Capitalization | ✅ Automatic | |
| Utterance Timestamps | ✅ Automatic | |
| Word-Level Timestamps | ⚠️ Limited | Available but degrades quality |
| Word-Level Confidence | ⚠️ Not True | Approximated values |
| Speaker Diarization | ✅ Yes | 14 languages, single-channel |
| Speech Adaptation | ✅ Yes | Up to 1,000 phrases |
| Language Detection | ✅ Yes | Automatic, language-agnostic |
| Denoiser | ✅ Built-in | Background noise/music reduction |
| Translation | ✅ Yes | Supported language pairs |

---

## 🔐 Enterprise Features (V2 Only)

### 1. **Data Residency**
- Deploy in specific Google Cloud regions (Belgium, Singapore, etc.)
- Full regionalization for regulatory compliance
- Regional endpoint format: `projects/{PROJECT_ID}/locations/{location}/recognizers/_`

### 2. **Security & Encryption**
- **CMEK:** Customer-Managed Encryption Keys for all resources
- **Audit Logging:** Resource creation and transcription logs in Cloud Console
- **VPC Service Controls:** Network security perimeter support

### 3. **Recognizer Resources**
- **Reusable Configurations:** Store default recognition settings
- **Reduces Latency:** Avoid per-request configuration overhead
- **Override Capability:** Use FieldMask for per-request modifications
- **Quota Optimization:** Reduces quota consumption

### 4. **Enhanced Logging & Telemetry**
- Detailed logs in Google Cloud Console
- Better debugging and monitoring
- Transcription telemetry data

---

## 📊 Streaming Configuration Options

### StreamingRecognitionConfig Structure

```protobuf
message StreamingRecognitionConfig {
  // Required: Recognition configuration
  RecognitionConfig config = 1;

  // Optional: Override specific fields from default recognizer
  google.protobuf.FieldMask config_mask = 2;

  // Optional: Streaming-specific features
  StreamingRecognitionFeatures streaming_features = 3;
}
```

### Key Streaming Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `interim_results` | Boolean | false | Stream partial results before final |
| `enable_voice_activity_events` | Boolean | false | Real-time VAD events |
| `voice_activity_timeout` | Object | - | Auto-close after silence |
| `config` | RecognitionConfig | Required | Model, language, features |
| `config_mask` | FieldMask | - | Override recognizer defaults |

### StreamingRecognize Request Flow

```
Client → Server:
┌─────────────────────────────────────────┐
│ 1. Initial Request (config + audio)    │
│    - StreamingRecognitionConfig         │
│    - First audio chunk (optional)       │
├─────────────────────────────────────────┤
│ 2. Audio Chunk Messages                 │
│    - audio bytes only (25 KB max each)  │
│    - Continue until done                 │
├─────────────────────────────────────────┤
│ 3. Close stream (client-initiated)      │
└─────────────────────────────────────────┘

Server → Client:
┌─────────────────────────────────────────┐
│ 1. Interim Results (if enabled)         │
│    - stability score                     │
│    - partial transcript                  │
├─────────────────────────────────────────┤
│ 2. Voice Activity Events (if enabled)   │
│    - VOICE_ACTIVITY detected             │
├─────────────────────────────────────────┤
│ 3. Final Results                         │
│    - is_final = true                     │
│    - confidence scores                   │
│    - complete transcript                 │
├─────────────────────────────────────────┤
│ 4. Stream end or error                   │
└─────────────────────────────────────────┘
```

---

## 🆚 V2 vs V1/V1beta1 Comparison

### Major Differences

| Aspect | V1/V1beta1 | V2 |
|--------|-----------|-----|
| **Chirp Models** | ❌ Not available | ✅ Exclusive (chirp, chirp_2, chirp_3) |
| **Data Residency** | ❌ Limited | ✅ Multi-region, regional deployment |
| **Recognizers** | ❌ No concept | ✅ Reusable configurations |
| **CMEK** | ⚠️ Limited | ✅ All resources and batch |
| **Audit Logging** | ⚠️ Basic | ✅ Comprehensive |
| **Auto Audio Detection** | ❌ Manual | ✅ Automatic detection |
| **Denoiser** | ❌ No | ✅ Built-in (Chirp 3) |
| **Translation** | ❌ No | ✅ Yes (Chirp 3) |
| **Max Streaming Duration** | 305 seconds | 305 seconds (same) |

### API Structure Changes

| Change | V1beta1 → V1 | V1 → V2 |
|--------|-------------|---------|
| Method Names | `SyncRecognize` → `Recognize` | Same methods |
| Field Names | `sample_rate` → `sample_rate_hertz` | No major rename |
| Language Code | Optional → **Required** | **Required** |
| Recognition Config | Simple object | Supports recognizer resources |
| Endpoint Format | Global only | Regional endpoints supported |

### Migration Considerations

#### V1beta1 → V1 (2017)
- ✅ Add language code (required)
- ✅ Update field name: `sample_rate_hertz`
- ✅ Update method: `Recognize` instead of `SyncRecognize`
- ✅ Update enum: `SpeechEventType` instead of `EndpointerType`

#### V1 → V2 (2024)
- ✅ **Minimal changes required**
- ✅ Optional: Use recognizers for config reuse
- ✅ Optional: Enable regional endpoints
- ✅ Optional: Use Chirp models for better accuracy
- ✅ Test auto-detection vs explicit config
- ❌ **No automatic migration** - must update manually

---

## 🔧 Current Module Status (mod_google_transcribe)

### What We Use Now
- **API Version:** `v1p1beta1` (beta)
- **Protocol:** gRPC bidirectional streaming
- **Encoding:** LINEAR16 PCM
- **Sample Rate:** Configurable (default 8000 Hz)
- **Channels:** Mono or Stereo with separate recognition

### Features Already Supported
- ✅ Streaming recognition
- ✅ Interim results
- ✅ Single utterance mode
- ✅ Speaker diarization
- ✅ Word time offsets
- ✅ Automatic punctuation
- ✅ Phrase hints with boost
- ✅ Alternative languages
- ✅ Profanity filter
- ✅ Speech models (phone_call, video, etc.)
- ✅ Enhanced models
- ✅ Metadata (interaction type, microphone distance, etc.)
- ✅ Multi-channel with separate recognition
- ✅ VAD (Voice Activity Detection)

### Missing V2 Features
- ❌ Chirp models (chirp, chirp_2, chirp_3)
- ❌ Recognizer resources (config reuse)
- ❌ Auto audio detection
- ❌ Built-in denoiser
- ❌ Translation capability
- ❌ Regional endpoints
- ❌ Enhanced audit logging
- ❌ Voice activity events (real-time VAD)
- ❌ Voice activity timeout
- ❌ Spoken punctuation/emoji

---

## 🎯 Recommendations for Upgrade

### High Priority (Immediate Value)
1. **Chirp 3 Model** - Significant accuracy improvement
2. **Auto Audio Detection** - Simplifies configuration
3. **Recognizer Resources** - Reduces latency and quota usage
4. **Built-in Denoiser** - Better quality for noisy environments

### Medium Priority (Enterprise Features)
5. **Regional Endpoints** - Compliance and data residency
6. **Voice Activity Events** - Better real-time responsiveness
7. **Enhanced Audit Logging** - Better monitoring and debugging

### Low Priority (Nice to Have)
8. **Translation** - If multilingual support needed
9. **Spoken Punctuation** - Natural language input
10. **Voice Activity Timeout** - Automatic session cleanup

---

## 📚 Sources & References

### Official Documentation
- [Speech-to-Text V2 Documentation](https://cloud.google.com/speech-to-text/v2/docs)
- [Streaming Recognition V2](https://cloud.google.com/speech-to-text/v2/docs/streaming-recognize)
- [Migration Guide V1 to V2](https://cloud.google.com/speech-to-text/docs/migration)
- [Chirp 3 Model](https://cloud.google.com/speech-to-text/v2/docs/chirp_3-model)
- [Audio Encoding](https://cloud.google.com/speech-to-text/v2/docs/encoding)
- [Recognizers](https://cloud.google.com/speech-to-text/v2/docs/recognizers)
- [API Reference](https://cloud.google.com/speech-to-text/v2/docs/reference/rpc/google.cloud.speech.v2)

### Community & Technical Resources
- [Speech-to-Text Release Notes](https://cloud.google.com/speech-to-text/docs/release-notes)
- [Speech-to-Text Basics](https://cloud.google.com/speech-to-text/v2/docs/basics)
- [Request Construction](https://cloud.google.com/speech-to-text/docs/speech-to-text-requests)
- [Stack Overflow: V2 Setup](https://stackoverflow.com/questions/76722471/how-to-setup-streamingrecognize-google-cloud-speech-to-text-v2-in-node-js)
- [GitHub Issues: gRPC Streaming](https://github.com/grpc/grpc-java/issues/12072)

### Protocol & Architecture
- [gRPC vs WebSocket Comparison](https://ably.com/topic/grpc-vs-websocket)
- [Speech-to-Text with Socket.io](https://rohitp934.medium.com/speech-to-text-in-realtime-with-gcp-and-socket-io-ad6fc788c7bb)
- [gRPC-Web for Browsers](https://grpc.io/blog/postman-grpcweb/)

---

## ✅ Summary

### Protocol Support
- ✅ **gRPC:** Full bidirectional streaming (required for real-time)
- ✅ **REST:** Synchronous and batch only (no streaming)
- ❌ **WebSocket:** Not supported (requires bridge server)

### Key V2 Advantages
1. **Chirp 3 Model** - Best-in-class accuracy
2. **Enterprise Features** - CMEK, regional, audit logs
3. **Auto-Detection** - Simplifies audio configuration
4. **Recognizers** - Reduces latency and costs
5. **Built-in Denoiser** - Better audio quality

### Current Module (v1p1beta1)
- Solid feature set, production-ready
- Missing latest Chirp models and V2 enterprise features
- Upgrade path is straightforward with minimal code changes

---

**End of Analysis**
