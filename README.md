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

Choose your build method:

| Script | Best For | Time | Location |
|--------|----------|------|----------|
| `./build-locally.sh` | Production/CI/CD | 60-90 min | Docker container |
| `sudo ./build-batch.sh all` | Production standalone | 75-130 min | /usr/local/src |
| `sudo ./test-batch-simple.sh all` | Testing/Validation | 75-130 min | /tmp/freeswitch-build |

---

## Building

### Option 1: Docker Build (Production)

**Best for:** Production deployment, CI/CD pipelines

```bash
./build-locally.sh
```

**Features:**
- Isolated, reproducible builds
- Optimized container (~500MB)
- All 5 modules included
- **Automated validation** - Build fails if modules missing or have dependency issues
- Production-ready

**Build validation:**
During the Docker build, all modules are automatically validated:
- ✓ Verifies all 5 module .so files exist
- ✓ Checks dependencies with `ldd` (no missing libraries)
- ✓ Build fails immediately if any module has issues

**Run the container:**
```bash
docker run -d \
  --name freeswitch \
  -p 5060:5060/udp \
  -p 5060:5060/tcp \
  -p 8021:8021/tcp \
  -v $(pwd)/logs:/usr/local/freeswitch/log \
  freeswitch-transcribe:latest
```

---

### Option 2: Batch Build (Production Standalone)

**Best for:** Production standalone builds, learning dependencies, debugging

**Prerequisites:**
- Ubuntu 20.04/22.04/24.04 or Debian 11
- Root access (sudo)
- 20GB+ free disk space
- 8GB+ RAM
- 4+ CPU cores

**Build all batches:**
```bash
sudo ./build-batch.sh all
```

**Or build individually:**
```bash
sudo ./build-batch.sh 1  # CMake (1 min)
sudo ./build-batch.sh 2  # gRPC + Protobuf (15-30 min)
sudo ./build-batch.sh 3  # googleapis + libwebsockets (5-10 min)
sudo ./build-batch.sh 4  # Azure Speech SDK (1 min)
sudo ./build-batch.sh 5  # spandsp + sofia-sip + libfvad (10-15 min)
sudo ./build-batch.sh 6  # AWS SDK C++ + AWS C Common (20-40 min)
sudo ./build-batch.sh 7  # FreeSWITCH + Modules (20-30 min)
```

**Features:**
- Catch errors early (fail fast)
- Resume from last successful batch
- Understand dependencies step-by-step
- Production-ready standalone builds

**What it does:**
1. Installs system dependencies
2. Builds CMake 3.28.3
3. Builds gRPC 1.64.2 + Protocol Buffers
4. Builds googleapis for Google Cloud
5. Builds libwebsockets 4.3.3
6. Installs Azure Speech SDK 1.37.0
7. Builds spandsp, sofia-sip 1.13.17, libfvad
8. Builds AWS SDK C++ 1.11.345
9. Builds FreeSWITCH 1.10.11
10. Copies and builds all 5 modules
11. Verifies all modules load correctly

**Duration:** 75-130 minutes

**Output:** FreeSWITCH installed at `/usr/local/freeswitch/`

---

### Option 3: Simplified Test Build

**Best for:** Testing without apt-get, validation

```bash
# Run all batches
sudo ./test-batch-simple.sh all

# Or specific batch
sudo ./test-batch-simple.sh 6
```

**Features:**
- Builds in `/tmp/freeswitch-build/`
- No system package installation
- Requires root only for `make install` and `ldconfig`
- Good for restricted environments

---

## Build Scripts Comparison

| Feature | Docker | Batch | Test |
|---------|--------|-------|------|
| Install system packages | ✓ | ✓ | ✗ |
| Requires root | ✓ | ✓ | ✓* |
| Build location | Container | /usr/local/src | /tmp |
| Resume capability | ✗ | ✓ | ✓ |
| Module verification | ✓ | ✓ | ✓ |
| Production ready | ✓ | ✓ | ✗ |

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

- **[Stereo Channel Assignment Guide](docs/STEREO_CHANNEL_ASSIGNMENT.md)** - Complete guide on FreeSWITCH stereo channel assignment for consistent agent/customer labeling
- **[Stereo Quick Reference](docs/QUICK_REFERENCE_STEREO_CHANNELS.md)** - Quick configuration snippets for stereo setup
- **[Docker Deployment Guide](dockerfiles/README.md)** - Complete Docker build and deployment instructions
- **[Per-User Multi-Service Setup](examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)** - Enable different transcription services per user

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
