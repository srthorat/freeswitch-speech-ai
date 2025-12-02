# mod_google_transcribe Dependency Analysis

## Overview
This document provides a complete dependency analysis for the original `mod_google_transcribe` module imported from https://github.com/srthorat/freeswitch_modules/tree/main/modules/mod_google_transcribe.

The module provides real-time speech transcription using Google Cloud Speech-to-Text API via gRPC streaming.

## Build Dependencies

### 1. Core Build Tools
| Dependency | Purpose | Ubuntu 24 Package | Status | Notes |
|------------|---------|------------------|--------|-------|
| GCC/G++ | C/C++ compiler | `build-essential` | ✓ Required | Needs C++17 support (GCC 7+) |
| Make | Build automation | `make` | ✓ Required | Part of build-essential |
| pkg-config | Build configuration | `pkg-config` | ✓ Required | For finding libraries |
| autoconf | Build configuration | `autoconf` | ✓ Required | For FreeSWITCH module build |
| automake | Build automation | `automake` | ✓ Required | For FreeSWITCH module build |
| libtool | Build tooling | `libtool libtool-bin` | ✓ Required | For shared library creation |

### 2. FreeSWITCH Development Files
| Dependency | Purpose | Ubuntu 24 Package | Status | Notes |
|------------|---------|------------------|--------|-------|
| FreeSWITCH headers | Module API | N/A | ✓ Required | Must be installed first via FreeSWITCH build |
| libfreeswitch | Core library | N/A | ✓ Required | Built with FreeSWITCH |

### 3. gRPC and Protocol Buffers
| Dependency | Purpose | Ubuntu 24 Package | Status | Notes |
|------------|---------|------------------|--------|-------|
| gRPC C++ | gRPC client library | `libgrpc++-dev` | ✓ Available | v1.51.x in Ubuntu 24.04 |
| gRPC Core | gRPC core library | `libgrpc-dev` | ✓ Available | v1.51.x in Ubuntu 24.04 |
| Protocol Buffers compiler | Proto code generation | `protobuf-compiler` | ✓ Available | v3.21.x in Ubuntu 24.04 |
| gRPC Protocol Buffers plugin | gRPC service code generation | `protobuf-compiler-grpc` | ✓ Available | For generating gRPC stubs |
| libprotobuf | Protocol Buffers runtime | `libprotobuf-dev` | ✓ Available | C++ protobuf library |

**Version Compatibility**: Ubuntu 24.04 provides gRPC 1.51.x and Protobuf 3.21.x, which are compatible with Google Cloud Speech v1p1beta1 API.

### 4. Google Cloud Speech API Proto Definitions
| Dependency | Purpose | Ubuntu 24 Package | Status | Notes |
|------------|---------|------------------|--------|-------|
| googleapis | Google API proto files | N/A | ✗ Not packaged | Must download from https://github.com/googleapis/googleapis |

**Resolution Strategy**: Download googleapis repository and generate C++ code from proto files during build process.

**Required Proto Files**:
- `google/cloud/speech/v1p1beta1/cloud_speech.proto` (primary API)
- `google/api/*.proto` (common API definitions)
- `google/rpc/*.proto` (RPC status codes)
- `google/longrunning/*.proto` (if using long-running operations)

### 5. SSL/TLS Libraries
| Dependency | Purpose | Ubuntu 24 Package | Status | Notes |
|------------|---------|------------------|--------|-------|
| OpenSSL | TLS for gRPC connections | `libssl-dev` | ✓ Available | Required for secure connections to Google |

## Runtime Dependencies

### 1. Runtime Libraries
| Dependency | Purpose | Ubuntu 24 Package | Status | Notes |
|------------|---------|------------------|--------|-------|
| gRPC++ runtime | gRPC client runtime | `libgrpc++1.51` | ✓ Available | Auto-installed with -dev package |
| protobuf runtime | Protocol Buffers runtime | `libprotobuf23` | ✓ Available | Auto-installed with -dev package |
| OpenSSL runtime | TLS runtime | `libssl3` | ✓ Available | Pre-installed on Ubuntu 24 |

### 2. Google Cloud Credentials
| Dependency | Purpose | Configuration Method | Status | Notes |
|------------|---------|---------------------|--------|-------|
| Service Account Key | Google Cloud authentication | Environment variable | ⚠️ User-provided | Set `GOOGLE_APPLICATION_CREDENTIALS` |
| gRPC Channel Credentials | TLS certificate validation | System CA certs | ✓ Auto | Uses `ca-certificates` package |

**Security Note**: Service account JSON key files should NEVER be committed to the repository. Configure via environment variables in FreeSWITCH systemd service.

## Build Process Requirements

### Step 1: Install System Packages
```bash
apt-get install -y \
    build-essential \
    autoconf automake libtool libtool-bin \
    pkg-config \
    libgrpc++-dev libgrpc-dev \
    protobuf-compiler protobuf-compiler-grpc \
    libprotobuf-dev \
    libssl-dev \
    git
```

### Step 2: Download and Generate Google API Proto Files
```bash
# Clone googleapis repository
cd /usr/local/src
git clone --depth 1 https://github.com/googleapis/googleapis.git

# Generate C++ code for Speech API v1p1beta1
cd googleapis
protoc \
    --cpp_out=. \
    --grpc_out=. \
    --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
    -I. \
    google/cloud/speech/v1p1beta1/cloud_speech.proto \
    google/api/*.proto \
    google/rpc/*.proto

# Install generated headers and libraries
mkdir -p /usr/local/include/googleapis
cp -r google /usr/local/include/googleapis/
```

### Step 3: Build Module
The module should be built with:
- C++17 standard (`-std=c++17`)
- Include paths: FreeSWITCH headers, googleapis generated headers
- Link flags: `pkg-config --libs grpc++ grpc`, `-lprotobuf`

## Environment Variables (Runtime Configuration)

| Variable | Purpose | Example | Required |
|----------|---------|---------|----------|
| `GOOGLE_APPLICATION_CREDENTIALS` | Path to service account key | `/etc/freeswitch/google-sa.json` | ✓ Yes |
| `GRPC_DEFAULT_SSL_ROOTS_FILE_PATH` | Custom CA certs (optional) | `/etc/ssl/certs/ca-certificates.crt` | ✗ Optional |
| `GRPC_TRACE` | Debug gRPC connections | `all` | ✗ Debug only |
| `GRPC_VERBOSITY` | gRPC log level | `DEBUG` | ✗ Debug only |

## Package Summary for Ubuntu 24.04

### Install Command
```bash
sudo apt-get update
sudo apt-get install -y \
    libgrpc++-dev \
    libgrpc-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libssl-dev \
    git \
    pkg-config
```

### Verification
```bash
# Verify protoc
protoc --version  # Should show: libprotoc 3.21.x

# Verify grpc_cpp_plugin
which grpc_cpp_plugin  # Should return path

# Verify pkg-config can find gRPC
pkg-config --libs grpc++ grpc
pkg-config --cflags grpc++
```

## Known Issues and Workarounds

### Issue 1: Missing googleapis Proto Files
**Problem**: Ubuntu does not package Google Cloud API proto definitions.

**Workaround**: Download googleapis repo and generate C++ code during build (automated in install script).

### Issue 2: gRPC Version Compatibility
**Problem**: Some older proto files may require specific gRPC versions.

**Resolution**: Ubuntu 24.04's gRPC 1.51.x is compatible with Google Speech v1p1beta1 API. No action needed.

### Issue 3: C++17 Requirement
**Problem**: The Makefile.am specifies `-std=c++17`.

**Resolution**: Ubuntu 24.04's GCC 13 fully supports C++17. No action needed.

## Testing Dependencies

No unit tests are included in the original module. Smoke testing requires:
- Running FreeSWITCH instance
- Valid Google Cloud credentials
- Test audio stream or live call

## External Service Dependencies

| Service | Purpose | API Endpoint | Authentication |
|---------|---------|--------------|----------------|
| Google Cloud Speech-to-Text | Real-time transcription | `speech.googleapis.com:443` | Service Account Key (JSON) |

## Comparison to mod_google_transcribev2

| Aspect | mod_google_transcribe (original) | mod_google_transcribev2 (v2 module) |
|--------|----------------------------------|--------------------------------------|
| Google Cloud SDK | Uses raw gRPC + protobuf | Uses full Google Cloud C++ SDK |
| Build complexity | Moderate (generate protos) | High (build entire SDK) |
| Build time | ~2-3 minutes | ~10-15 minutes |
| Runtime dependencies | gRPC, protobuf only | Google Cloud C++ SDK (~50 MB) |
| API Version | v1p1beta1 (stable) | v2 (newer) |
| FreeSWITCH integration | Direct gRPC streams | Wrapped in SDK abstractions |

**Recommendation**: The original mod_google_transcribe is lighter weight and faster to build, making it preferable for production deployments where the v1p1beta1 API features are sufficient.

## References

- [Google Cloud Speech-to-Text gRPC API](https://cloud.google.com/speech-to-text/docs/reference/rpc)
- [googleapis GitHub Repository](https://github.com/googleapis/googleapis)
- [gRPC C++ Documentation](https://grpc.io/docs/languages/cpp/)
- [Protocol Buffers Documentation](https://protobuf.dev/)
- [Ubuntu 24.04 gRPC Packages](https://packages.ubuntu.com/noble/libgrpc++-dev)
