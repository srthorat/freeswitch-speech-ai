# FreeSWITCH with mod_google_transcribev2 - Docker Image

Complete Docker solution for running FreeSWITCH 1.10.11 with Google Speech-to-Text v2 API integration (`mod_google_transcribev2`).

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Prerequisites](#prerequisites)
- [Quick Start](#quick-start)
- [Detailed Build Instructions](#detailed-build-instructions)
- [Running the Container](#running-the-container)
- [Validation](#validation)
- [Expected Outputs](#expected-outputs)
- [Troubleshooting](#troubleshooting)
- [Production Deployment](#production-deployment)

---

## Overview

### What's Included

- **Base**: FreeSWITCH 1.10.11 fully built from source
- **Module**: mod_google_transcribev2 for real-time speech-to-text
- **SDK**: Google Cloud C++ Speech library v2.30.0
- **Dependencies**: gRPC, Protocol Buffers, Abseil
- **Management**: Supervisor for process management
- **Configuration**: Pre-configured SIP extensions (1000, 1001, 1002)

### Image Specifications

- **Base Image**: `srt2011/freeswitch-base:latest`
- **Final Tag**: `freeswitch:mod-google-transcribev2`
- **Build Time**: ~15-20 minutes
- **Final Size**: ~1.1 GB
- **FreeSWITCH Install Path**: `/usr/local/freeswitch`
- **Module Install Path**: `/usr/local/freeswitch/lib/freeswitch/mod/`

---

## Architecture

### Multi-Stage Build

```
Stage 1 (builder):
  ├── Install build dependencies (gRPC, protobuf, cmake, etc.)
  ├── Build Google Cloud C++ Speech SDK v2.30.0
  ├── Build mod_google_transcribev2 using Makefile
  ├── Generate protobuf files from speech.proto
  └── Validate module dependencies (ldd)

Stage 2 (runtime):
  ├── Copy Google Cloud C++ libraries
  ├── Copy mod_google_transcribev2.so
  ├── Install runtime dependencies only
  ├── Configure modules.conf.xml
  ├── Configure supervisor with environment variables
  └── Runtime validation
```

### Key Dependencies

| Component | Version | Purpose |
|-----------|---------|---------|
| Google Cloud C++ Speech | 2.30.0 | Speech-to-Text API client |
| gRPC | System package | RPC framework |
| Protocol Buffers | System package | Data serialization |
| Abseil | System package | Google utilities |
| OpenSSL | 1.1 | TLS/encryption |
| cURL | 4.x | HTTP client |

---

## Prerequisites

### 1. Docker Environment

```bash
# Verify Docker is installed and running
docker --version
# Expected: Docker version 20.10.x or later

docker info
# Should show server info without errors
```

### 2. Base Image

You need the `freeswitch-base` image. Either:

**Option A: Pull from registry** (recommended):
```bash
docker pull srt2011/freeswitch-base:latest
```

**Option B: Build locally**:
```bash
docker build -f dockerfiles/Dockerfile.freeswitch-base \
  -t srt2011/freeswitch-base:latest .
```

### 3. Google Cloud Setup

1. **Create GCP Project**:
   - Go to [Google Cloud Console](https://console.cloud.google.com/)
   - Create a new project or select existing one
   - Note your `PROJECT_ID`

2. **Enable Speech-to-Text API**:
   ```bash
   gcloud services enable speech.googleapis.com --project=YOUR_PROJECT_ID
   ```

3. **Create Service Account**:
   - Navigate to IAM & Admin > Service Accounts
   - Click "Create Service Account"
   - Name: `freeswitch-speech`
   - Role: `Cloud Speech-to-Text API User`
   - Create and download JSON key

4. **Save Credentials**:
   ```bash
   mkdir -p secrets/
   mv ~/Downloads/your-project-abc123-xyz.json secrets/google-creds.json
   chmod 600 secrets/google-creds.json
   ```

---

## Quick Start

### Build the Image

```bash
# Basic build with defaults
./dockerfiles/docker-build-mod-google-transcribev2.sh

# Or manual build
docker build -f dockerfiles/Dockerfile.mod_google_transcribev2 \
  -t freeswitch:mod-google-transcribev2 \
  --build-arg BUILD_CPUS=8 \
  .
```

### Run the Container

```bash
# Start the container
docker run -d --name freeswitch-google \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  freeswitch:mod-google-transcribev2
```

### Configure Google Credentials

```bash
# 1. Copy credentials file to container
docker cp secrets/google-creds.json freeswitch-google:/etc/freeswitch/google-creds.json

# 2. Edit supervisor config inside container
docker exec -it freeswitch-google bash
vi /etc/supervisor/conf.d/freeswitch-google.conf

# 3. Update the environment line with your settings:
# environment=LD_LIBRARY_PATH="/usr/local/lib:/usr/local/freeswitch/lib",GOOGLE_APPLICATION_CREDENTIALS="/etc/freeswitch/google-creds.json",GOOGLE_PROJECT_ID="your-project-id",GCP_LOCATION="us-central1"

# 4. Restart FreeSWITCH process (not container)
supervisorctl restart freeswitch
exit
```

### Validate

```bash
# Check if module is loaded
docker exec freeswitch-google fs_cli -x "module_exists mod_google_transcribev2"
# Expected: true
```

---

## Detailed Build Instructions

### Build Command

```bash
docker build \
  -f dockerfiles/Dockerfile.mod_google_transcribev2 \
  -t freeswitch:mod-google-transcribev2 \
  --build-arg BASE_IMAGE=srt2011/freeswitch-base:latest \
  --build-arg GOOGLE_CLOUD_CPP_VERSION=2.30.0 \
  --build-arg BUILD_CPUS=4 \
  .
```

### Build Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| `BASE_IMAGE` | `srt2011/freeswitch-base:latest` | Base FreeSWITCH image |
| `GOOGLE_CLOUD_CPP_VERSION` | `2.30.0` | Google Cloud SDK version |
| `BUILD_CPUS` | `4` | CPU cores for compilation |

### Build Time Breakdown

| Stage | Duration | Description |
|-------|----------|-------------|
| Build dependencies install | ~2 min | apt-get install |
| Google Cloud C++ build | ~10-12 min | cmake + make |
| mod_google_transcribev2 build | ~1-2 min | Makefile build |
| Runtime image creation | ~1 min | Layer copy + validation |
| **Total** | **~15-20 min** | Full build |

### Build Output (Expected)

```
Step 1/45 : ARG BASE_IMAGE=srt2011/freeswitch-base:latest
...
Step 20/45 : RUN echo "Building Google Cloud C++ Speech library..."
=============================================
Building Google Cloud C++ Speech library 2.30.0...
This will take 10-15 minutes...
=============================================
[  1%] Building CXX object google/cloud/CMakeFiles/google_cloud_cpp_common.dir/...
...
[100%] Built target google_cloud_cpp_speech
✅ Google Cloud C++ Speech library 2.30.0 built and installed
=============================================

Step 25/45 : RUN echo "Building mod_google_transcribev2..."
=============================================
Building mod_google_transcribev2...
=============================================

Source files:
-rw-r--r-- 1 root root  22514 mod_google_transcribev2.c
-rw-r--r-- 1 root root  17155 google_transcribe_glue.cpp
-rw-r--r-- 1 root root  22311 speech.proto

✅ Pre-generated protobuf files found
Building module using Makefile...
Compiling mod_google_transcribev2.c...
Compiling google_transcribe_glue.cpp...
Compiling speech.pb.cc...
Compiling speech.grpc.pb.cc...
Linking mod_google_transcribev2.so...
✅ mod_google_transcribev2 compiled

Installing module...
✅ mod_google_transcribev2 installed

Step 30/45 : RUN echo "Module Validation"
=============================================
Module Validation
=============================================

1. Check module file exists:
✅ Module file exists
-rwxr-xr-x 1 root root 450K /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so

2. Check module dependencies with ldd:
	linux-vdso.so.1 (0x00007ffd...)
	libgoogle_cloud_cpp_speech.so.2 => /usr/local/lib/libgoogle_cloud_cpp_speech.so.2
	libgrpc++.so.1 => /usr/lib/x86_64-linux-gnu/libgrpc++.so.1
	libprotobuf.so.23 => /usr/lib/x86_64-linux-gnu/libprotobuf.so.23
	libssl.so.1.1 => /usr/lib/x86_64-linux-gnu/libssl.so.1.1
	...

3. Verify Google Cloud SDK linkage:
✅ Module correctly linked with Google Cloud C++ Speech
	libgoogle_cloud_cpp_speech.so.2 => /usr/local/lib/libgoogle_cloud_cpp_speech.so.2
	libgoogle_cloud_cpp_common.so.2 => /usr/local/lib/libgoogle_cloud_cpp_common.so.2

4. Verify gRPC linkage:
✅ Module correctly linked with gRPC++
	libgrpc++.so.1 => /usr/lib/x86_64-linux-gnu/libgrpc++.so.1
	libgrpc.so.10 => /usr/lib/x86_64-linux-gnu/libgrpc.so.10

5. Verify protobuf linkage:
✅ Module correctly linked with protobuf
	libprotobuf.so.23 => /usr/lib/x86_64-linux-gnu/libprotobuf.so.23

6. Check for missing dependencies:
✅ No missing dependencies

=============================================
✅ Static validation passed!
=============================================
```

---

## Running the Container

### Step 1: Start the Container

```bash
docker run -d --name freeswitch-google \
  --network host \
  freeswitch:mod-google-transcribev2
```

### Step 2: Configure Google Cloud Credentials

```bash
# Copy credentials file to container
docker cp secrets/google-creds.json freeswitch-google:/etc/freeswitch/google-creds.json

# Edit supervisor configuration
docker exec -it freeswitch-google bash

# Inside container, edit the supervisor config
vi /etc/supervisor/conf.d/freeswitch-google.conf

# Update the environment line with your Google Cloud settings:
# environment=LD_LIBRARY_PATH="/usr/local/lib:/usr/local/freeswitch/lib",GOOGLE_APPLICATION_CREDENTIALS="/etc/freeswitch/google-creds.json",GOOGLE_PROJECT_ID="your-actual-project-id",GCP_LOCATION="us-central1"

# Restart FreeSWITCH process (not the container)
supervisorctl restart freeswitch
exit
```

### Environment Variables (Configured via Supervisor)

| Variable | Required | Default | Description |
|----------|----------|---------|-------------|
| `GOOGLE_APPLICATION_CREDENTIALS` | **Yes** | `/etc/freeswitch/google-creds.json` | Path to service account JSON inside container |
| `GOOGLE_PROJECT_ID` | **Yes** | - | GCP project ID |
| `GCP_LOCATION` | No | `us-central1` | GCP region |
| `LD_LIBRARY_PATH` | Auto-set | `/usr/local/lib:/usr/local/freeswitch/lib` | Library search path |

### Volume Mounts (Optional)

```bash
# Optional: Custom configuration
-v ./custom-conf:/usr/local/freeswitch/conf

# Optional: Persist logs
-v ./logs:/usr/local/freeswitch/log

# Optional: Persist database
-v ./db:/usr/local/freeswitch/db

# Note: Credentials are copied using 'docker cp' instead of volume mount
```

### Port Mappings

```bash
# SIP signaling
-p 5060:5060/tcp -p 5060:5060/udp

# Event Socket (fs_cli)
-p 8021:8021/tcp

# RTP media (adjust range as needed)
-p 16384-16484:16384-16484/udp
```

---

## Validation

### Validation Commands

```bash
# 1. Check module file
docker exec freeswitch-google \
  ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so

# Expected: -rwxr-xr-x 1 freeswitch freeswitch 450K ...

# 2. Check dependencies
docker exec freeswitch-google \
  ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so

# Expected: All libraries found, no "not found" errors

# 3. Check if module is loaded
docker exec freeswitch-google \
  /usr/local/freeswitch/bin/fs_cli -x "module_exists mod_google_transcribev2"

# Expected: true

# 4. Check FreeSWITCH status
docker exec freeswitch-google \
  /usr/local/freeswitch/bin/fs_cli -x "status"

# Expected: FreeSWITCH Version 1.10.11-release~64bit ... UP ...

# 5. View logs
docker exec freeswitch-google \
  tail -f /usr/local/freeswitch/log/freeswitch.log | grep -i google

# Expected: [NOTICE] Successfully Loaded [mod_google_transcribev2]
```

---

## Troubleshooting

### Build Failures

**Problem**: `ERROR: Module not linked with Google Cloud C++ SDK`

**Solution**:
```bash
# Check if Google Cloud C++ built successfully
docker build --target builder \
  -f dockerfiles/Dockerfile.mod_google_transcribev2 \
  -t test-builder .

docker run --rm test-builder \
  ls -la /usr/local/lib/libgoogle_cloud_cpp_*.so*
```

**Problem**: `protoc: command not found`

**Solution**:
```bash
# Verify base image has gRPC/protobuf
docker run --rm srt2011/freeswitch-base:latest which protoc
# If not found, rebuild base image
```

### Runtime Failures

**Problem**: Module not loading

**Solution**:
```bash
# Check logs for errors
docker logs freeswitch-google | grep -i error

# Try loading manually
docker exec freeswitch-google \
  /usr/local/freeswitch/bin/fs_cli -x "load mod_google_transcribev2"
```

**Problem**: `cannot open shared object file: libgoogle_cloud_cpp_speech.so.2`

**Solution**:
```bash
# Verify library was copied to runtime stage
docker exec freeswitch-google \
  ls -la /usr/local/lib/libgoogle_cloud_cpp_*.so*

# Update library cache
docker exec freeswitch-google ldconfig
```

### Google Cloud Errors

**Problem**: `PERMISSION_DENIED` or `UNAUTHENTICATED`

**Solution**:
```bash
# 1. Verify credentials file exists
docker exec freeswitch-google \
  cat /etc/freeswitch/google-creds.json | jq .project_id

# 2. Verify environment variables
docker exec freeswitch-google env | grep GOOGLE

# 3. Test credentials outside container
gcloud auth activate-service-account \
  --key-file=secrets/google-creds.json
gcloud auth list
```

**Problem**: `API not enabled`

**Solution**:
```bash
# Enable Speech-to-Text API
gcloud services enable speech.googleapis.com \
  --project=YOUR_PROJECT_ID
```

---

## Production Deployment

### Security Best Practices

1. **Use Docker Secrets** (for Swarm/Kubernetes):
   ```bash
   echo "your-creds.json" | docker secret create google_creds -
   ```

2. **Run as Non-Root**:
   Container already runs FreeSWITCH as `freeswitch` user (UID 1000)

3. **Read-Only Filesystem**:
   ```bash
   docker run --read-only \
     --tmpfs /tmp \
     --tmpfs /var/log \
     ...
   ```

4. **Resource Limits**:
   ```yaml
   deploy:
     resources:
       limits:
         cpus: '2.0'
         memory: 2G
       reservations:
         cpus: '1.0'
         memory: 1G
   ```

### Monitoring

```bash
# Container stats
docker stats freeswitch-google

# Health check
docker inspect freeswitch-google --format='{{.State.Health.Status}}'

# Logs
docker logs -f --tail=100 freeswitch-google
```

### Backup

```bash
# Backup configuration
docker cp freeswitch-google:/usr/local/freeswitch/conf ./backup/conf-$(date +%Y%m%d)

# Backup database
docker cp freeswitch-google:/usr/local/freeswitch/db ./backup/db-$(date +%Y%m%d)
```

---

## Summary

### Deliverables

✅ **Dockerfile**: `dockerfiles/Dockerfile.mod_google_transcribev2`
✅ **Build Script**: `dockerfiles/docker-build-mod-google-transcribev2.sh`
✅ **Documentation**: `dockerfiles/README-mod-google-transcribev2.md`

### Quick Commands Reference

```bash
# Build
./dockerfiles/docker-build-mod-google-transcribev2.sh

# Run
docker run -d --name freeswitch-google \
  --network host \
  freeswitch:mod-google-transcribev2

# Configure Google credentials
docker cp secrets/google-creds.json freeswitch-google:/etc/freeswitch/google-creds.json
docker exec -it freeswitch-google bash
# Edit /etc/supervisor/conf.d/freeswitch-google.conf and update environment line
# Then: supervisorctl restart freeswitch && exit

# Validate
docker exec freeswitch-google fs_cli -x "module_exists mod_google_transcribev2"

# Access
docker exec -it freeswitch-google fs_cli

# Logs
docker logs -f freeswitch-google
```

---

For issues or questions, see [Troubleshooting](#troubleshooting) or check the module README at `modules/mod_google_transcribev2/README.md`.
