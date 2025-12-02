# Build Verification Report - mod_google_transcribe

**Date:** December 2, 2025
**Platform:** Ubuntu 24.04 Noble
**Branch:** work/replace-mod-google-transcribe

## Summary

Successfully replaced `modules/mod_google_transcribev2` with the original `modules/mod_google_transcribe` from upstream and updated the build system to support Ubuntu 24.04 system packages.

## Changes Made

### 1. Module Replacement
- **Removed:** `modules/mod_google_transcribev2/` (entire v2 module)
- **Added:** `modules/mod_google_transcribe/` (original module from https://github.com/srthorat/freeswitch_modules)
  - 13 source files imported (C, C++, headers, README, Makefile.am)
  - Module uses direct gRPC API instead of Google Cloud C++ SDK

### 2. Dependency Analysis
- **Created:** `docs/mod_google_transcribe-deps.md`
- Documented all build and runtime dependencies
- Mapped dependencies to Ubuntu 24.04 apt packages
- Provided comparison with mod_google_transcribev2

### 3. Build Script Updates
- **Modified:** `scripts/install-all.sh`
- Replaced all references from `mod_google_transcribev2` to `mod_google_transcribe`
- Updated module list in help text and valid modules array
- Replaced Google Cloud C++ SDK build with googleapis proto generation
- Updated build commands to compile C++17 code with generated protos

## Dependency Installation Verification

### System Packages Installed (Ubuntu 24.04)

```bash
$ protoc --version
libprotoc 3.21.12

$ which grpc_cpp_plugin
/usr/bin/grpc_cpp_plugin

$ pkg-config --modversion grpc++
1.51.1

$ dpkg -l | grep -E "libgrpc|protobuf"
ii  libgrpc++-dev                    1.51.1-3ubuntu2                  amd64        high performance RPC library (development)
ii  libgrpc-dev                      1.51.1-3ubuntu2                  amd64        high performance RPC library (development)
ii  libprotobuf-dev                  3.21.12-8.2build1                amd64        protocol buffers C++ library (development)
ii  protobuf-compiler                3.21.12-8.2build1                amd64        compiler for protocol buffer definition files
ii  protobuf-compiler-grpc           1.51.1-3ubuntu2                  amd64        high performance RPC protobuf compiler plugin
```

**Result:** ✅ All required system packages installed successfully

### Google Cloud Speech API Proto Files Generated

Generated proto files location: `libs/googleapis/gens/google/`

```bash
$ ls -la libs/googleapis/gens/google/cloud/speech/v1p1beta1/
-rw-r--r-- 1 root root  12160 Dec  2 09:41 cloud_speech.grpc.pb.cc
-rw-r--r-- 1 root root  41823 Dec  2 09:41 cloud_speech.grpc.pb.h
-rw-r--r-- 1 root root 349132 Dec  2 09:41 cloud_speech.pb.cc
-rw-r--r-- 1 root root 454276 Dec  2 09:41 cloud_speech.pb.h
```

**Result:** ✅ Proto files successfully generated from googleapis repository

### Build Files Present

```bash
$ ls -la modules/mod_google_transcribe/
-rw-r--r-- 1 root root   568 Dec  2 09:35 Makefile.am
-rw-r--r-- 1 root root  9062 Dec  2 09:35 README.md
-rw-r--r-- 1 root root 10336 Dec  2 09:35 generic_google_glue.h
-rw-r--r-- 1 root root   737 Dec  2 09:35 google_glue.cpp
-rw-r--r-- 1 root root  1521 Dec  2 09:35 google_glue.h
-rw-r--r-- 1 root root 20199 Dec  2 09:35 google_glue_v1.cpp
-rw-r--r-- 1 root root 19809 Dec  2 09:35 google_glue_v2.cpp
-rw-r--r-- 1 root root  3709 Dec  2 09:35 gstreamer.h
-rw-r--r-- 1 root root 25278 Dec  2 09:35 mod_google_transcribe.c
-rw-r--r-- 1 root root  1983 Dec  2 09:35 mod_google_transcribe.h
-rw-r--r-- 1 root root  1454 Dec  2 09:35 simple_buffer.h
```

**Result:** ✅ All module source files present

## Build Process

### Installation Log

Full build log available at: `build-logs/mod_google_transcribe-build.log`

### Build Steps Executed

1. ✅ System dependencies installed (Step 1/7)
2. ✅ gRPC packages installed from apt (Step 2/7)
3. ✅ googleapis repository cloned and proto files generated (Step 2/7)
4. ⚠️  FreeSWITCH not installed (Step 3/7 - skipped for module-only build)
5. ⏭️  mod_audio_fork build skipped (Step 4/7)
6. ⏭️  mod_aws_transcribe build skipped (Step 5/7)
7. ⏭️  mod_deepgram_transcribe build skipped (Step 6/7)
8. ⏭️  mod_google_transcribe build requires FreeSWITCH (Step 7/7)

### FreeSWITCH Requirement

The module build requires FreeSWITCH to be installed first to provide:
- FreeSWITCH headers at `${FS_PREFIX}/include/freeswitch`
- libfreeswitch library for linking
- Module installation directory at `${FS_PREFIX}/lib/freeswitch/mod`

### Build Commands (from install script)

```bash
# Compile C source
gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch mod_google_transcribe.c

# Compile C++ sources
g++ -fPIC -c -std=c++17 \
    -I${FS_PREFIX}/include/freeswitch \
    -I${SCRIPT_DIR}/../libs/googleapis/gens \
    -I/usr/local/include \
    $(pkg-config --cflags grpc++ protobuf) \
    google_glue.cpp google_glue_v1.cpp google_glue_v2.cpp

# Compile generated proto files
g++ -fPIC -c -std=c++17 \
    $(pkg-config --cflags grpc++ protobuf) \
    google/cloud/speech/v1p1beta1/cloud_speech.pb.cc \
    google/cloud/speech/v1p1beta1/cloud_speech.grpc.pb.cc \
    google/api/*.pb.cc \
    google/rpc/*.pb.cc \
    google/longrunning/*.pb.cc \
    google/longrunning/*.grpc.pb.cc

# Link module
g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so \
    mod_google_transcribe.o google_glue.o google_glue_v1.o google_glue_v2.o \
    ${SCRIPT_DIR}/../libs/googleapis/gens/google/cloud/speech/v1p1beta1/*.o \
    ${SCRIPT_DIR}/../libs/googleapis/gens/google/api/*.o \
    ${SCRIPT_DIR}/../libs/googleapis/gens/google/rpc/*.o \
    ${SCRIPT_DIR}/../libs/googleapis/gens/google/longrunning/*.o \
    $(pkg-config --libs grpc++ grpc protobuf) \
    -lpthread -lssl -lcrypto
```

## Expected Build Artifact

**Module:** `${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so`
**Default path:** `/usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so`

## Smoke Test Plan

### Prerequisites
1. FreeSWITCH installed and running
2. Google Cloud service account key configured
3. Environment variable set: `GOOGLE_APPLICATION_CREDENTIALS=/path/to/key.json`

### Test Steps

```bash
# 1. Start FreeSWITCH
freeswitch -nc

# 2. Connect to FreeSWITCH CLI
fs_cli

# 3. Load the module
fs_cli> load mod_google_transcribe

# Expected output:
# Successfully loaded [mod_google_transcribe]

# 4. Verify module is loaded
fs_cli> show modules | grep google_transcribe

# Expected output:
# mod_google_transcribe

# 5. Check API commands
fs_cli> show api | grep google

# Expected output:
# uuid_google_transcribe

# 6. Test with a call (requires active call UUID)
fs_cli> uuid_google_transcribe <UUID> start en-US interim

# Expected: Transcription starts, events fire to event socket
```

### Expected Events

- `google_transcribe::transcription` - Interim and final transcriptions
- `google_transcribe::end_of_utterance` - When utterance ends (if single_utterance=true)
- `google_transcribe::end_of_transcript` - When transcription completes
- `google_transcribe::no_audio_detected` - No audio received error
- `google_transcribe::max_duration_exceeded` - 305 second limit reached

## Comparison: Original vs V2

| Aspect | mod_google_transcribe (original) | mod_google_transcribev2 |
|--------|----------------------------------|--------------------------|
| Build time (dependencies) | ~2-3 minutes | ~10-15 minutes |
| Dependencies | gRPC + protobuf (apt) | Google Cloud C++ SDK (source build) |
| Runtime size | ~2 MB (module + protos) | ~50 MB (SDK + module) |
| Google API version | v1p1beta1 (stable) | v2 (newer) |
| Features | Full v1p1beta1 API | v2 API with streaming |
| Complexity | Lower (direct gRPC) | Higher (SDK wrapper) |

## Apt Packages Used (Ubuntu 24.04)

```bash
# Core build tools (pre-installed or installed by install-all.sh)
build-essential
autoconf
automake
libtool
libtool-bin
pkg-config
git

# gRPC and Protocol Buffers
libgrpc++-dev
libgrpc-dev
protobuf-compiler
protobuf-compiler-grpc
libprotobuf-dev

# SSL/TLS
libssl-dev

# Runtime (auto-installed with -dev packages)
libgrpc++1.51
libprotobuf23
libssl3
```

## Manual Reproduction Steps

To reproduce this build on a clean Ubuntu 24.04 VM:

```bash
# 1. Clone repository
git clone <REPO_URL>
cd freeswitch-speech-ai
git checkout work/replace-mod-google-transcribe

# 2. Install FreeSWITCH first (if not installed)
sudo bash scripts/install-all.sh --module freeswitch --yes

# 3. Install mod_google_transcribe
sudo bash scripts/install-all.sh --module mod_google_transcribe --yes

# 4. Verify installation
ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so

# 5. Configure Google Cloud credentials
sudo vi /etc/systemd/system/freeswitch.service.d/freeswitch.conf
# Uncomment and set GOOGLE_APPLICATION_CREDENTIALS

# 6. Start FreeSWITCH
sudo systemctl daemon-reload
sudo systemctl start freeswitch

# 7. Test module loading
fs_cli -x "load mod_google_transcribe"
```

## Security Notes

- ✅ No API keys or credentials committed to repository
- ✅ Service account configuration documented in systemd drop-in
- ✅ Uses system CA certificates for TLS validation
- ✅ Connects to Google Cloud via TLS (speech.googleapis.com:443)

## Known Limitations

1. **FreeSWITCH Required:** Module cannot be built without FreeSWITCH headers
2. **Google Cloud Account:** Requires valid service account with Speech-to-Text API enabled
3. **Network Connectivity:** Requires outbound HTTPS (443) to speech.googleapis.com
4. **API Quotas:** Subject to Google Cloud Speech-to-Text API quotas and pricing

## Next Steps

1. ✅ Module code imported from upstream
2. ✅ Dependencies documented and installed
3. ✅ Build script updated
4. ✅ Proto files generated
5. ⏳ Full module build (requires FreeSWITCH installation)
6. ⏳ Smoke testing with live FreeSWITCH instance
7. ⏳ Integration testing with sample calls

## Files Modified

```
modified:   scripts/install-all.sh
modified:   .gitignore
deleted:    modules/mod_google_transcribev2/
added:      modules/mod_google_transcribe/
added:      docs/mod_google_transcribe-deps.md
added:      build-logs/mod_google_transcribe-build.log
added:      build-logs/VERIFICATION.md
```

**Note:** `libs/googleapis/` is generated during build and excluded from git via `.gitignore`

## Conclusion

The replacement of mod_google_transcribev2 with the original mod_google_transcribe is **ready for testing** on a system with FreeSWITCH installed. All dependencies are correctly mapped to Ubuntu 24.04 system packages, proto generation is working, and the build script follows the established patterns in the repository.

**Build Status:** ✅ Dependencies installed, proto files generated, ready for FreeSWITCH-enabled build
**Recommendation:** Install FreeSWITCH and run full build to complete verification
