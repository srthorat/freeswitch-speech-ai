# FreeSWITCH Transcription Modules

A collection of production-ready FreeSWITCH modules for real-time speech-to-text transcription and audio streaming, supporting multiple cloud providers.

## Modules

| Module | Provider | Protocol | Key Features |
|--------|----------|----------|--------------|
| [mod_audio_fork](modules/mod_audio_fork/) | Generic | WebSocket (libwebsockets) | Stream audio to external services |
| [mod_aws_transcribe](modules/mod_aws_transcribe/) | AWS | Native SDK | Streaming transcription, speaker diarization |
| [mod_azure_transcribe](modules/mod_azure_transcribe/) | Azure | WebSocket | Real-time transcription, language detection |
| [mod_deepgram_transcribe](modules/mod_deepgram_transcribe/) | Deepgram | WebSocket | Fast transcription, keyword boosting |
| [mod_google_transcribe](modules/mod_google_transcribe/) | Google Cloud | gRPC | High accuracy, punctuation, interim results |

---

## Quick Start

### Build All 5 Modules

```bash
./build-all-modules.sh
```

**Build time:** 45-60 minutes (includes all SDKs)
**Final size:** ~1.5 GB

### Run with Docker Compose

```bash
docker-compose up -d
```

### Or Run with Script

```bash
# Basic run (no credentials)
./run-all-modules.sh freeswitch-speech-ai:all-modules

# With all API keys
./run-all-modules.sh freeswitch-speech-ai:all-modules \
  DEEPGRAM_KEY \
  AZURE_KEY eastus \
  AWS_KEY AWS_SECRET us-east-1 \
  "" \
  /path/to/google-credentials.json
```

---

## Building

### Docker Build (All 5 Modules)

**Build options:**

```bash
# Default build (4 CPUs)
./build-all-modules.sh

# Custom build
./build-all-modules.sh --cpus 8 --tag my-custom-tag

# Without cache
./build-all-modules.sh --no-cache
```

**Features:**
- ✅ All 5 modules in one image
- ✅ Automated validation (build fails if issues detected)
- ✅ Multi-stage build (optimized size)
- ✅ Based on freeswitch-base:latest

**Build validation:**
During the Docker build, all modules are automatically validated:
- ✓ Verifies all 5 module .so files exist
- ✓ Checks dependencies with `ldd` (no missing libraries)
- ✓ Runtime validation (modules load successfully)
- ✓ Build fails immediately if any module has issues

**Modules included:**
- mod_audio_fork (WebSocket streaming)
- mod_aws_transcribe (AWS Transcribe)
- mod_deepgram_transcribe (Deepgram)
- mod_azure_transcribe (Azure Cognitive Services)
- mod_google_transcribe (Google Cloud Speech-to-Text)

**Dependencies:**
- libwebsockets 4.3.3
- AWS SDK C++ 1.11.345
- gRPC 1.64.2 + protobuf + googleapis
- Azure Speech SDK (latest)

---

## Running

### Option 1: Docker Compose (Recommended)

Edit `docker-compose.yml` and uncomment API keys:

```yaml
environment:
  # Deepgram
  - DEEPGRAM_API_KEY=your_key_here

  # AWS (permanent credentials)
  - AWS_ACCESS_KEY_ID=AKIA***
  - AWS_SECRET_ACCESS_KEY=***
  - AWS_REGION=us-east-1

  # AWS (temporary STS credentials)
  - AWS_ACCESS_KEY_ID=ASIA***
  - AWS_SECRET_ACCESS_KEY=***
  - AWS_SESSION_TOKEN=IQoJ***  # Required for ASIA* keys
  - AWS_REGION=us-east-1

  # Azure
  - AZURE_SUBSCRIPTION_KEY=your_key
  - AZURE_REGION=eastus

  # Google Cloud
  - GOOGLE_APPLICATION_CREDENTIALS=/etc/google/credentials.json

volumes:
  # Google credentials file
  - /path/to/google-credentials.json:/etc/google/credentials.json:ro
```

Then run:
```bash
docker-compose up -d
```

### Option 2: Run Script

```bash
./run-all-modules.sh freeswitch-speech-ai:all-modules \
  [DEEPGRAM_KEY] \
  [AZURE_KEY] [AZURE_REGION] \
  [AWS_ACCESS_KEY_ID] [AWS_SECRET_ACCESS_KEY] [AWS_REGION] \
  [AWS_SESSION_TOKEN] \
  [GOOGLE_CREDENTIALS_PATH]
```

**Examples:**

```bash
# Deepgram only
./run-all-modules.sh freeswitch-speech-ai:all-modules \
  sk_***

# AWS permanent credentials (AKIA*)
./run-all-modules.sh freeswitch-speech-ai:all-modules \
  "" "" "" \
  AKIA*** secret us-east-1

# AWS temporary STS credentials (ASIA*)
./run-all-modules.sh freeswitch-speech-ai:all-modules \
  "" "" "" \
  ASIA*** secret us-east-1 IQoJ***

# All services
./run-all-modules.sh freeswitch-speech-ai:all-modules \
  sk_deepgram \
  azure_key eastus \
  AKIA*** aws_secret us-east-1 \
  "" \
  /path/to/google-creds.json
```

---

## AWS Credentials (Including STS Tokens)

### Permanent Credentials (AKIA*)

```bash
docker run -e AWS_ACCESS_KEY_ID=AKIA*** \
           -e AWS_SECRET_ACCESS_KEY=*** \
           -e AWS_REGION=us-east-1 \
           freeswitch-speech-ai:all-modules
```

### Temporary STS Credentials (ASIA*)

**Important:** ASIA* keys require `AWS_SESSION_TOKEN`

```bash
docker run -e AWS_ACCESS_KEY_ID=ASIA*** \
           -e AWS_SECRET_ACCESS_KEY=*** \
           -e AWS_SESSION_TOKEN=IQoJ*** \
           -e AWS_REGION=us-east-1 \
           freeswitch-speech-ai:all-modules
```

### IAM Roles (EC2/ECS)

No credentials needed - automatically uses instance role:

```bash
docker run freeswitch-speech-ai:all-modules
```

---

## Configuration

### Extension Setup

Each extension can have different transcription services enabled:

```xml
<!-- User Directory: /usr/local/freeswitch/conf/directory/default/1000.xml -->
<user id="1000">
  <params>
    <param name="password" value="1234"/>
  </params>
  <variables>
    <variable name="effective_caller_id_name" value="Alice Johnson"/>
    <variable name="effective_caller_id_number" value="1000"/>

    <!-- Enable service for this extension -->
    <variable name="enable_audio_fork" value="true"/>
    <!-- or -->
    <variable name="enable_deepgram" value="true"/>
    <!-- or -->
    <variable name="enable_aws_transcribe" value="true"/>
    <!-- or -->
    <variable name="enable_azure" value="true"/>
    <!-- or -->
    <variable name="enable_google_transcribe" value="true"/>
  </variables>
</user>
```

### Dialplan Configuration

See `examples/freeswitch-config/dialplan/default.xml` for complete examples with:
- Speaker information setup
- Conditional transcription based on user flags
- External inbound/outbound call handling
- Pusher integration with enhanced metadata

---

## API Commands

### mod_audio_fork

```bash
# Start streaming
uuid_audio_fork <uuid> start ws://server:port/path [mono|mixed|stereo] [8k|16k|48k]

# Stop streaming
uuid_audio_fork <uuid> stop
```

### mod_deepgram_transcribe

```bash
# Start transcription
uuid_deepgram_transcribe <uuid> start <lang> [interim] [stereo]

# Stop transcription
uuid_deepgram_transcribe <uuid> stop
```

### mod_aws_transcribe

```bash
# Start transcription
uuid_aws_transcribe <uuid> start <lang> [interim] [stereo]

# Stop transcription
uuid_aws_transcribe <uuid> stop
```

### mod_azure_transcribe

```bash
# Start transcription
uuid_azure_transcribe <uuid> start <lang> [interim]

# Stop transcription
uuid_azure_transcribe <uuid> stop
```

### mod_google_transcribe

```bash
# Start transcription
uuid_google_transcribe <uuid> start <lang> [interim]

# Stop transcription
uuid_google_transcribe <uuid> stop
```

---

## Testing

### Extension Credentials

| Extension | Username | Password | Service Enabled |
|-----------|----------|----------|-----------------|
| 1000 | 1000 | 1234 | Audio Fork |
| 1001 | 1001 | 1234 | Deepgram |
| 1002 | 1002 | 1234 | Azure |
| 1003 | 1003 | 1234 | AWS |
| 1004 | 1004 | 1234 | Google |

### Verify Modules

```bash
# Access FreeSWITCH CLI
docker exec -it freeswitch fs_cli

# Check loaded modules
freeswitch@internal> show modules | grep -E 'audio_fork|deepgram|aws|azure|google'

# Expected output (all 5 modules):
api,uuid_audio_fork,mod_audio_fork,/usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so
api,uuid_aws_transcribe,mod_aws_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so
api,uuid_azure_transcribe,mod_azure_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so
api,uuid_deepgram_transcribe,mod_deepgram_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so
api,uuid_google_transcribe,mod_google_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so
```

*Only for `make install` and `ldconfig`

---

## Configuration

Each module has its own configuration file in `/usr/local/freeswitch/conf/autoload_configs/`:

- `audio_fork.conf.xml`
- `aws_transcribe.conf.xml`
- `azure_transcribe.conf.xml`
- `deepgram_transcribe.conf.xml`
- `google_transcribe.conf.xml`

---

## Usage Examples

### AWS Transcribe with Speaker Diarization

```xml
<action application="aws_transcribe" data="en-US,us-east-1,interim=true,speaker_diarization=true"/>
```

### Google Cloud Speech-to-Text

```xml
<action application="google_transcribe" data="en-US,interim=true"/>
```

### Deepgram Real-time

```xml
<action application="deepgram_transcribe" data="en-US,interim=true"/>
```

### Azure Speech Services

```xml
<action application="azure_transcribe" data="en-US,interim=true"/>
```

### Audio Fork to WebSocket

```xml
<action application="audio_fork" data="wss://your-service.com/ws"/>
```

---

## Build Dependencies

| Module | Dependencies |
|--------|--------------|
| mod_audio_fork | libwebsockets 4.3.3, libspeexdsp (speex resampler) |
| mod_aws_transcribe | AWS C++ SDK 1.11.345 (transcribestreaming) |
| mod_azure_transcribe | libwebsockets 4.3.3, Azure Speech SDK 1.37.0 |
| mod_deepgram_transcribe | libwebsockets 4.3.3 |
| mod_google_transcribe | gRPC 1.64.2, protobuf, Google Cloud Speech API |

**Common dependencies:**
- CMake 3.28.3
- FreeSWITCH 1.10.11
- spandsp (signal processing)
- sofia-sip 1.13.17 (SIP stack)
- libfvad (voice activity detection)

---

## Files Analysis Summary

The `files/` directory contains critical build configurations, patches, and source modifications required for transcription modules.

### Build Configuration Files

| File | Lines | Purpose |
|------|-------|---------|
| **configure.ac.extra** | 2,471 | FreeSWITCH autoconf configuration with custom flags |
| **Makefile.am.extra** | 1,052 | Top-level build targets and module inclusion |
| **ax_check_compile_flag.m4** | 50 | Autoconf macro for SIMD optimization detection (AVX2/SSE2) |

**configure.ac.extra adds these critical flags:**
- `--with-lws` - Enables libwebsockets support (required for Deepgram, Azure, Audio Fork)
- `--with-extra` - Enables gRPC/protobuf modules (required for Google Cloud)
- `--with-aws` - Enables AWS SDK modules (required for AWS Transcribe)

### Patch Files (Security & Functionality)

| File | Lines | Purpose |
|------|-------|---------|
| **switch_core_media.c.patch** | 12 | **Security fix**: Adds buffer overflow protection for RED frames |
| **switch_rtp.c.patch** | 38 | **Disables RTP packet flushing** - ensures all audio packets received (critical for transcription) |
| **mod_avmd.c.patch** | 23 | Forces READ_REPLACE for inbound calls (drachtio-fsmrf compatibility) |
| **mod_httapi.c.patch** | 57 | **AWS S3 signed URL support** - proper caching of S3 presigned URLs |

**Security Details:**
- `switch_core_media.c.patch` adds `count >= MAX_RED_FRAMES` bounds check to prevent buffer overflow
- `switch_rtp.c.patch` comments out packet flush logic to guarantee packet delivery for real-time transcription

**Functional Modifications:**
- RTP packet handling optimized for continuous audio streaming
- HTTP API enhanced for AWS S3 integration
- AVMD (answering machine detection) configured for media server use cases

### Source File Replacements

| File | Lines | Purpose |
|------|-------|---------|
| **switch_event.c** | 3,844 | Complete replacement of FreeSWITCH core event system |
| **conference_api.c** | 4,375 | Conference module API implementation (custom commands) |
| **mod_conference.h** | 1,336 | Conference module header (matches conference_api.c) |

These files provide custom event handling and conference functionality tailored for media server applications.

### What's NOT Included

**XML Configuration Files (Removed - Using FreeSWITCH Defaults):**
- No custom `acl.conf.xml`, `switch.conf.xml`, `vars.xml`, etc.
- FreeSWITCH uses vanilla default configurations
- Transcription modules use default FreeSWITCH dialplan and SIP profiles
- Runtime configuration can be added post-installation as needed

**Downloaded Automatically During Build:**
- **Azure Speech SDK** - Downloaded from Microsoft (https://aka.ms/csspeech/linuxbinary) - always latest version
- **AWS SDK C++** - Downloaded from GitHub if not cached in files/

---

## Batch Build Details

### Batch 1: CMake (1 min)
- Downloads CMake 3.28.3 source
- Bootstraps and builds CMake
- Installs to /usr/local/bin/cmake

**Verify:**
```bash
cmake --version  # Expected: 3.28.3
```

### Batch 2: gRPC + Protocol Buffers (15-30 min)
- Clones gRPC 1.64.2 with 16 submodules
- Builds gRPC with shared libraries
- Installs protoc and grpc_cpp_plugin

**Verify:**
```bash
/usr/local/bin/protoc --version
ls -lh /usr/local/lib/libgrpc++.so
```

### Batch 3: googleapis + libwebsockets (5-10 min)
- Clones googleapis for Google Cloud APIs
- Builds libwebsockets 4.3.3 for WebSocket modules

**Verify:**
```bash
ls -lh /usr/local/lib/libwebsockets.so
```

### Batch 4: Azure Speech SDK (1-2 min)
- Extracts Azure Speech SDK 1.37.0 from tarball
- Installs headers and libraries

**Verify:**
```bash
ls -lh /usr/local/lib/libMicrosoft.CognitiveServices.Speech.core.so
```

### Batch 5: spandsp + sofia-sip + libfvad (10-15 min)
- Builds spandsp for signal processing
- Builds sofia-sip 1.13.17 SIP stack
- Builds libfvad for voice activity detection

**Verify:**
```bash
ls -lh /usr/local/lib/libspandsp.so
ls -lh /usr/local/lib/libsofia-sip-ua.so
ls -lh /usr/local/lib/libfvad.so
```

### Batch 6: AWS SDK C++ + AWS C Common (20-40 min)
- Builds AWS SDK C++ 1.11.345
- Only builds transcribestreaming and lexv2-runtime
- Builds AWS C Common library

**Verify:**
```bash
ls -lh /usr/local/lib/libaws-cpp-sdk-transcribestreaming.so
ls -lh /usr/local/lib/libaws-c-common.a
```

### Batch 7: FreeSWITCH + Modules (20-30 min)
- Clones FreeSWITCH 1.10.11
- Copies all 5 transcription modules
- Applies patches and configures
- Builds and installs FreeSWITCH
- Verifies all modules with dependency checking

**Verify:**
```bash
ls -lh /usr/local/freeswitch/mod/mod_*.so
ldd /usr/local/freeswitch/mod/mod_aws_transcribe.so
ldd /usr/local/freeswitch/mod/mod_google_transcribe.so
```

---

## Monitoring Build Progress

### Check if build is running:
```bash
ps aux | grep -E "(build-batch|test-batch)"
```

### Monitor logs:
```bash
tail -f /tmp/batch*.log
```

### Check build processes:
```bash
# CMake/make builds
ps aux | grep -E "cmake|make"

# Git operations
ps aux | grep git
```

---

## Troubleshooting

### Common Issues

**1. cJSON conflicts**
- Handled automatically by build scripts
- AWS SDK cJSON headers are patched to avoid conflicts

**2. Missing symbols / Library not found**
- Verify ldconfig was run after each library installation
- Check `LD_LIBRARY_PATH` includes `/usr/local/lib`

**3. Module not loading**
- Check FreeSWITCH logs: `/usr/local/freeswitch/log/freeswitch.log`
- Ensure module is in `modules.conf`
- Verify dependencies with: `ldd /usr/local/freeswitch/mod/mod_*.so`

**4. Out of memory during build**
- Close other applications
- Reduce parallel jobs (edit BUILD_CPUS in scripts)
- Add swap space if needed

**5. Git clone very slow**
- Normal for gRPC (has 16 submodules, 100MB+ total)
- Monitor progress: `du -sh /tmp/freeswitch-build/grpc`

**6. Build stalls at specific percentage**
- Likely compiling large template file
- Check CPU usage: `top` (look for cc1plus at 100%)
- If CPU is high, build is progressing (just slow)

### Batch fails with missing library
- Previous batch may have failed silently
- Re-run previous batch and check for errors
- Check logs in `/tmp/batch*.log`

---

## Version Configuration

Edit `.env` to customize build versions:

```env
cmakeVersion=3.28.3
grpcVersion=1.64.2
libwebsocketsVersion=4.3.3
speechSdkVersion=1.37.0
spandspVersion=0d2e6ac
sofiaVersion=1.13.17
awsSdkCppVersion=1.11.345
freeswitchModulesVersion=claude/fix-incremental-batch-build-all-01WfPfYsy5N1LDzLrLiBokRy
freeswitchVersion=1.10.11
```

---

## Module Details

For detailed documentation on each module:

- [mod_audio_fork README](modules/mod_audio_fork/README.md)
- [mod_aws_transcribe README](modules/mod_aws_transcribe/README.md)
- [mod_azure_transcribe README](modules/mod_azure_transcribe/README.md)
- [mod_deepgram_transcribe README](modules/mod_deepgram_transcribe/README.md)
- [mod_google_transcribe README](modules/mod_google_transcribe/README.md)

---

## Testing FreeSWITCH

### Check Version
```bash
/usr/local/freeswitch/bin/freeswitch -version
```

### Test Module Loading
```bash
/usr/local/freeswitch/bin/freeswitch -nc -nonat &
/usr/local/freeswitch/bin/fs_cli -x "module_exists mod_aws_transcribe"
```

### Check Logs
```bash
tail -f /usr/local/freeswitch/log/freeswitch.log
```

---

## System Requirements

### Minimum
- 4 CPU cores
- 8GB RAM
- 20GB free disk space
- Ubuntu 20.04/22.04/24.04 or Debian 11

### Recommended
- 8+ CPU cores
- 16GB+ RAM
- 30GB+ free disk space
- SSD for faster builds

---

## Documentation

### 📚 Guides & References

- **[Speech Module Comparison](docs/MODULE_COMPARISON.md)** - Comprehensive comparison of all transcription modules (speed, accuracy, features, cost)
- **[Stereo Channel Assignment Guide](docs/STEREO_CHANNEL_ASSIGNMENT.md)** - Complete guide on FreeSWITCH stereo channel assignment for consistent agent/customer labeling
- **[Stereo Quick Reference](docs/QUICK_REFERENCE_STEREO_CHANNELS.md)** - Quick configuration snippets for stereo setup
- **[Real-Time Transcription Delivery](docs/REALTIME_TRANSCRIPTION_DELIVERY.md)** - Guide for delivering transcription events to frontend applications
- **[XML Dialplan vs Lua](docs/DIALPLAN_VS_LUA.md)** - Comparison of FreeSWITCH call control approaches
- **[Docker Deployment Guide](dockerfiles/README.md)** - Complete Docker build and deployment instructions
- **[Per-User Multi-Service Setup](docs/PER_USER_MULTI_SERVICE.md)** - Enable different transcription services per user

### 🔧 Module Documentation

Each module has detailed configuration documentation:
- [mod_aws_transcribe](modules/mod_aws_transcribe/README.md) - AWS Transcribe with channel identification
- [mod_deepgram_transcribe](modules/mod_deepgram_transcribe/README.md) - Deepgram Nova-2 with multichannel
- [mod_azure_transcribe](modules/mod_azure_transcribe/README.md) - Azure Speech Services with conversation transcriber
- [mod_google_transcribe](modules/mod_google_transcribe/README.md) - Google Cloud Speech-to-Text with separate recognition
- [mod_audio_fork](modules/mod_audio_fork/README.md) - Generic audio streaming over WebSockets

---

## Credits

These modules are based on work from:
- [drachtio-freeswitch-modules](https://github.com/mdslaney/drachtio-freeswitch-modules) by mdslaney
- [drachtio project](https://drachtio.org/) by Dave Horton

---

## License

See individual module source files for licensing information.

---

## Contributing

Contributions are welcome! Please:
1. Test your changes with the batch build process
2. Update module README files
3. Follow FreeSWITCH coding conventions
4. Submit pull requests with clear descriptions

---

**Last Updated:** 2025-11-15
**Build Environment:** Ubuntu 24.04 (Noble)
**CPU Cores:** 16
**RAM:** 8GB+ recommended
