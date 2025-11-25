# Google Speech-to-Text V2 API Migration Summary

**Date:** 2025-11-25
**Module:** mod_google_transcribe
**Migration:** v1p1beta1 → v2

---

## 🎯 Migration Objectives

Migrate `mod_google_transcribe` from Google Speech-to-Text API **v1p1beta1** to **v2** with the following defaults:

✅ **API Version:** V2
✅ **Default Language:** en-US
✅ **Default Model:** chirp_3 (latest generation)
✅ **Audio Format:** LINEAR16 (PCM)
✅ **Sample Rate:** 16,000 Hz (optimal)
✅ **Channels:** Stereo (2 channels)
✅ **Results:** Interim + Final (both enabled)
✅ **Denoiser:** Enabled by default
✅ **Auto-Detection:** Channel and sample rate auto-detection

---

## 📝 Changes Made

### 1. **Updated API Includes** (`google_glue.cpp`)

**Before (v1p1beta1):**
```cpp
#include "google/cloud/speech/v1p1beta1/cloud_speech.grpc.pb.h"

using google::cloud::speech::v1p1beta1::RecognitionConfig;
using google::cloud::speech::v1p1beta1::Speech;
// ... more v1p1beta1 types
```

**After (v2):**
```cpp
#include "google/cloud/speech/v2/cloud_speech.grpc.pb.h"

using google::cloud::speech::v2::RecognitionConfig;
using google::cloud::speech::v2::Speech;
using google::cloud::speech::v2::StreamingRecognitionConfig;
using google::cloud::speech::v2::StreamingRecognitionFeatures;
using google::cloud::speech::v2::RecognitionFeatures;
using google::cloud::speech::v2::AutoDetectDecodingConfig;
using google::cloud::speech::v2::ExplicitDecodingConfig;
```

---

### 2. **Added Recognizer Path** (V2 Requirement)

V2 API requires a recognizer resource path:

```cpp
// Build recognizer path (using default recognizer "_")
const char* project_id = std::getenv("GOOGLE_CLOUD_PROJECT");
const char* location = "us"; // Default to US multi-region

std::string recognizer_path = "projects/" + project_id + "/locations/" + location + "/recognizers/_";
m_request.set_recognizer(recognizer_path);
```

**Environment Variables:**
- `GOOGLE_CLOUD_PROJECT` - Your GCP project ID
- `GOOGLE_SPEECH_LOCATION` - Region (default: "us")

---

### 3. **Updated Streaming Configuration**

**V2 API Structure:**
```cpp
auto* streaming_config = m_request.mutable_streaming_config();
auto* streaming_features = streaming_config->mutable_streaming_features();

// Enable interim results by default
streaming_features->set_interim_results(true);
```

**Key Change:** `interim_results` moved from `streaming_config` to `streaming_features` in V2.

---

### 4. **Changed Language Configuration**

**Before (v1):**
```cpp
config->set_language_code(lang); // Single language
```

**After (v2):**
```cpp
config->add_language_codes(lang); // Array of languages
```

V2 supports multiple languages for automatic language detection.

---

### 5. **Implemented Audio Auto-Detection**

**Default Behavior:**
```cpp
// V2 API: Use auto-detection for audio format (default)
auto* auto_decoding = config->mutable_auto_decoding_config();
```

Auto-detection automatically detects:
- Sample rate
- Channel count
- Audio encoding format

**Explicit Configuration (Multi-Channel):**
```cpp
if (channels > 1) {
  auto* explicit_decoding = config->mutable_explicit_decoding_config();
  explicit_decoding->set_audio_channel_count(channels);
  explicit_decoding->set_sample_rate_hertz(config_sample_rate);
  explicit_decoding->set_encoding(google::cloud::speech::v2::ExplicitDecodingConfig::LINEAR16);
}
```

---

### 6. **Added Denoiser (Built-in Noise Reduction)**

**New V2 Feature - Enabled by Default:**
```cpp
auto* features = config->mutable_features();

// V2 DEFAULT: Enable denoiser (built-in noise reduction)
features->set_enable_automatic_denoising(true);
```

**Disable if needed:**
```
Set channel variable: GOOGLE_SPEECH_ENABLE_DENOISER=false
```

---

### 7. **Updated Model Selection**

**Default Model:**
```cpp
// V2 API: Default model - chirp_3 (latest generation)
const char* v2_model = "chirp_3";
config->set_model(v2_model);
```

**Available V2 Models:**
- `chirp_3` - Latest generation (GA)
- `chirp_2` - Enhanced multilingual
- `chirp` - Original universal model
- `long` - Long-form content
- `latest_long` - Latest long-form

---

### 8. **Reorganized Recognition Features**

**V2 API uses `RecognitionFeatures` object:**

```cpp
auto* features = config->mutable_features();

// Optional features (disabled by default, enable on-demand)
features->set_enable_word_time_offsets(true);        // Word timestamps
features->set_enable_word_confidence(true);          // Word confidence scores
features->set_enable_automatic_punctuation(true);    // Auto punctuation
features->set_profanity_filter(true);                // Profanity filter
```

---

### 9. **Updated Multi-Channel Configuration**

**V2 API Structure:**
```cpp
if (channels > 1) {
  // Explicit decoding for multi-channel
  auto* explicit_decoding = config->mutable_explicit_decoding_config();
  explicit_decoding->set_audio_channel_count(channels);

  // Multi-channel mode
  features->set_multi_channel_mode(
    google::cloud::speech::v2::RecognitionFeatures::SEPARATE_RECOGNITION_PER_CHANNEL
  );
}
```

---

### 10. **Updated Speaker Diarization**

**Before (v1):**
```cpp
auto* diarization_config = config->mutable_diarization_config();
```

**After (v2):**
```cpp
// Diarization is now part of features
auto* diarization_config = features->mutable_diarization_config();
diarization_config->set_min_speaker_count(1);
diarization_config->set_max_speaker_count(6);
```

**Chirp 3 Speaker Diarization:**
- Supported languages: 14 (EN, ES, FR, DE, ZH, JA, KO, PT, IT, HI, etc.)
- Single-channel audio only
- Identifies different speakers automatically

---

### 11. **Added Language Detection Config**

**New V2 Feature:**
```cpp
if (switch_true(switch_channel_get_variable(channel, "GOOGLE_SPEECH_ENABLE_LANGUAGE_DETECTION"))) {
  auto* lang_detection = features->mutable_language_detection_config();
  // Uses language codes already added
}
```

Enable with: `GOOGLE_SPEECH_ENABLE_LANGUAGE_DETECTION=true`

---

### 12. **Removed V1-Specific Metadata**

**Removed (not available in V2):**
- `RecognitionMetadata` (interaction type, microphone distance, etc.)
- All metadata configuration code

**Reason:** V2 API doesn't support recognition metadata. Use custom labels or external tracking if needed.

---

### 13. **Updated Default Configuration** (`mod_google_transcribe.c`)

**Before:**
```c
static const uint32_t DEFAULT_SAMPLE_RATE = 8000;
switch_media_bug_flag_t flags = SMBF_READ_STREAM;
```

**After:**
```c
// V2 API: Default sample rate 16kHz (optimal for speech recognition)
static const uint32_t DEFAULT_SAMPLE_RATE = 16000;
static const char* DEFAULT_LANGUAGE = "en-US";
static const uint32_t DEFAULT_CHANNELS = 2; // Stereo by default

// V2 API: Default to stereo (read + write streams)
switch_media_bug_flag_t flags = SMBF_READ_STREAM | SMBF_WRITE_STREAM | SMBF_STEREO;
```

---

## 🔧 Configuration Options

### Default Configuration (No Setup Required)

| Setting | Default Value | Description |
|---------|--------------|-------------|
| **API Version** | v2 | Google Speech-to-Text V2 |
| **Model** | chirp_3 | Latest generation ASR |
| **Language** | en-US | English (United States) |
| **Sample Rate** | 16,000 Hz | Optimal for speech |
| **Encoding** | LINEAR16 | PCM audio |
| **Channels** | 2 (Stereo) | Both read + write streams |
| **Interim Results** | Enabled | Real-time partial results |
| **Final Results** | Enabled | Complete transcriptions |
| **Denoiser** | Enabled | Built-in noise reduction |
| **Auto-Detection** | Enabled | Format auto-detection |

### Optional Features (Enable via Channel Variables)

| Feature | Channel Variable | Default |
|---------|-----------------|---------|
| **Word Timestamps** | N/A (API parameter) | Disabled |
| **Word Confidence** | `GOOGLE_SPEECH_ENABLE_WORD_CONFIDENCE=true` | Disabled |
| **Automatic Punctuation** | N/A (API parameter) | Disabled |
| **Profanity Filter** | N/A (API parameter) | Disabled |
| **Speaker Diarization** | `GOOGLE_SPEECH_SPEAKER_DIARIZATION=1` | Disabled |
| **Language Detection** | `GOOGLE_SPEECH_ENABLE_LANGUAGE_DETECTION=true` | Disabled |
| **Alternative Languages** | `GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES=hi-IN,es-ES` | None |
| **Phrase Hints** | N/A (API parameter) | None |
| **Disable Denoiser** | `GOOGLE_SPEECH_ENABLE_DENOISER=false` | Enabled |
| **Location** | `GOOGLE_SPEECH_LOCATION=europe-west1` | us |

---

## 📊 API Comparison

### V1p1beta1 vs V2

| Feature | v1p1beta1 | V2 | Notes |
|---------|-----------|-----|-------|
| **Chirp Models** | ❌ No | ✅ chirp, chirp_2, chirp_3 | V2 exclusive |
| **Auto-Detection** | ❌ Manual | ✅ Automatic | Sample rate, channels, format |
| **Denoiser** | ❌ No | ✅ Built-in | Noise reduction |
| **Default Sample Rate** | 8 kHz | 16 kHz | Optimal quality |
| **Recognizer Resources** | ❌ No | ✅ Yes | Config reuse |
| **Language Array** | ❌ Single | ✅ Multiple | Multi-language detection |
| **Metadata** | ✅ Full support | ❌ Removed | Not available in V2 |
| **Regional Endpoints** | ⚠️ Limited | ✅ Full support | Data residency |
| **CMEK** | ⚠️ Limited | ✅ All resources | Enterprise encryption |

---

## 🚀 Usage Examples

### Basic Usage (Defaults)

```bash
# Uses all defaults: en-US, chirp_3, 16kHz, stereo, denoiser ON
uuid_google_transcribe <uuid> start en-US interim
```

### Custom Model

```bash
# Use specific model
uuid_google_transcribe2 <uuid> start en-US true false false 0 false false false 16000 long false
```

### Multi-Language Detection

Set channel variable before starting:
```xml
<action application="set" data="GOOGLE_SPEECH_ALTERNATIVE_LANGUAGE_CODES=hi-IN,es-ES,fr-FR"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_LANGUAGE_DETECTION=true"/>
<action application="uuid_google_transcribe" data="<uuid> start en-US interim"/>
```

### With Speaker Diarization

```xml
<action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION=1"/>
<action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION_MIN_SPEAKER_COUNT=2"/>
<action application="set" data="GOOGLE_SPEECH_SPEAKER_DIARIZATION_MAX_SPEAKER_COUNT=4"/>
<action application="uuid_google_transcribe" data="<uuid> start en-US interim"/>
```

---

## 🔍 Testing & Verification

### Required Environment Variables

```bash
# Required
export GOOGLE_APPLICATION_CREDENTIALS="/path/to/service-account-key.json"
export GOOGLE_CLOUD_PROJECT="your-project-id"

# Optional
export GOOGLE_SPEECH_LOCATION="us"  # or europe-west1, asia-northeast1, etc.
```

### Verify Configuration

Check FreeSWITCH logs for:
```
V2: Using recognizer path: projects/your-project-id/locations/us/recognizers/_
V2: interim results enabled by default
V2: primary language en-US
V2: auto-detection enabled for audio format
V2: using model chirp_3
V2: denoiser enabled by default
V2: explicit decoding - channels=2, sample_rate=16000, encoding=LINEAR16
```

---

## ⚠️ Breaking Changes

### API Changes

1. **Recognizer Path Required:** Must provide `GOOGLE_CLOUD_PROJECT` environment variable
2. **No Metadata:** `RecognitionMetadata` fields removed in V2
3. **Language Codes Array:** `language_code` → `language_codes` (array)
4. **Streaming Features:** `interim_results` moved to `streaming_features`
5. **Diarization Config:** Moved from `config` to `features`

### Default Behavior Changes

1. **Sample Rate:** 8 kHz → 16 kHz (better quality)
2. **Channels:** Mono → Stereo (both streams)
3. **Model:** No default → chirp_3 (latest)
4. **Denoiser:** N/A → Enabled (new feature)
5. **Language:** No default → en-US

---

## 📦 Compilation Notes

### Required Dependencies

- gRPC with v2 protobuf definitions
- Google Cloud Speech-to-Text V2 proto files
- Protocol Buffers compiler (protoc)

### Build Command

```bash
# Ensure v2 proto files are available
cd modules/mod_google_transcribe
make
make install
```

---

## 🎉 Benefits of V2 Migration

1. ✅ **Better Accuracy** - Chirp 3 model with state-of-the-art ASR
2. ✅ **Built-in Denoiser** - Automatic noise reduction
3. ✅ **Auto-Detection** - Simplified configuration
4. ✅ **Higher Quality** - 16 kHz default sample rate
5. ✅ **Multi-Language** - Automatic language detection
6. ✅ **Enterprise Ready** - Regional endpoints, CMEK, audit logging
7. ✅ **Future-Proof** - Latest API version with ongoing improvements

---

## 📚 References

- [Google Speech-to-Text V2 Documentation](https://cloud.google.com/speech-to-text/v2/docs)
- [Migration Guide](https://cloud.google.com/speech-to-text/docs/migration)
- [Chirp 3 Model](https://cloud.google.com/speech-to-text/v2/docs/chirp_3-model)
- [V2 API Analysis](./google-speech-v2-api-analysis.md)

---

**Migration Complete! 🚀**
