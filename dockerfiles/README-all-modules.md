# FreeSWITCH Speech AI - Unified Docker Image

This directory contains a unified Dockerfile that builds **all three speech transcription modules** in a single image:

- ✅ **mod_audio_fork** - Generic WebSocket audio streaming
- ✅ **mod_aws_transcribe** - AWS Transcribe Streaming
- ✅ **mod_deepgram_transcribe** - Deepgram Streaming

## 🎯 Features

All three modules have **complete feature parity**:

| Feature | Support |
|---------|---------|
| **Sampling Rate** | User choice (8k/16k/custom) |
| **Mix Type** | Mono/Mixed/Stereo |
| **Metadata** | JSON up to 8KB |
| **Session Events** | start/stop events |
| **Unified API** | Same syntax across all modules |

---

## 🚀 Quick Start

### Option 1: Build Locally

```bash
# Using the build script (recommended)
cd /home/user/freeswitch-speech-ai
chmod +x dockerfiles/build-all-modules.sh
./dockerfiles/build-all-modules.sh

# Or build directly
docker build -f dockerfiles/Dockerfile.all-modules \
  -t freeswitch-speech-ai:latest .
```

### Option 2: Using Docker Compose

```bash
# Start FreeSWITCH with all modules
docker-compose up -d

# View logs
docker-compose logs -f

# Stop
docker-compose down
```

---

## 📦 Build Options

The build script supports several options:

```bash
# Custom number of CPUs
./dockerfiles/build-all-modules.sh --cpus 8

# Custom AWS SDK version
./dockerfiles/build-all-modules.sh --aws-version 1.11.694

# Custom image tag
./dockerfiles/build-all-modules.sh --tag my-freeswitch:v1.0

# No cache (clean build)
./dockerfiles/build-all-modules.sh --no-cache

# Combine options
./dockerfiles/build-all-modules.sh --cpus 8 --tag my-freeswitch:v1.0
```

---

## 🎬 Running the Container

### Basic Run

```bash
docker run -d \
  --name freeswitch-speech-ai \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -p 16384-16484:16384-16484/udp \
  freeswitch-speech-ai:latest
```

### With AWS Credentials

```bash
docker run -d \
  --name freeswitch-speech-ai \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -p 16384-16484:16384-16484/udp \
  -e AWS_ACCESS_KEY_ID=your_key \
  -e AWS_SECRET_ACCESS_KEY=your_secret \
  -e AWS_REGION=us-east-1 \
  freeswitch-speech-ai:latest
```

### With Deepgram API Key

```bash
docker run -d \
  --name freeswitch-speech-ai \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -p 16384-16484:16384-16484/udp \
  -e DEEPGRAM_API_KEY=your_api_key \
  freeswitch-speech-ai:latest
```

---

## 🔧 Testing the Modules

### Access FreeSWITCH CLI

```bash
docker exec -it freeswitch-speech-ai fs_cli
```

### Verify Modules Loaded

```bash
# In fs_cli
freeswitch@localhost> module_exists mod_audio_fork
freeswitch@localhost> module_exists mod_aws_transcribe
freeswitch@localhost> module_exists mod_deepgram_transcribe
```

### Test Commands

All three modules use the **same unified syntax**:

#### mod_audio_fork
```bash
uuid_audio_fork <uuid> start wss://your-server.com/audio \
  mixed 16k '{"session_id":"abc123","caller":{"id":"1000"}}'
```

#### mod_aws_transcribe
```bash
uuid_aws_transcribe <uuid> start en-US interim \
  mixed 16k '{"session_id":"abc123","caller":{"id":"1000"}}'
```

#### mod_deepgram_transcribe
```bash
uuid_deepgram_transcribe <uuid> start en-US interim \
  mixed 16k '{"session_id":"abc123","caller":{"id":"1000"}}'
```

---

## 📊 Module Comparison

| Feature | audio_fork | aws_transcribe | deepgram |
|---------|------------|----------------|----------|
| **Backend** | Any WebSocket server | AWS Transcribe API | Deepgram API |
| **Transport** | WebSocket (wss://) | HTTP/2 + AWS SDK | WebSocket (wss://) |
| **Buffering** | Direct LWS | Deque + thread | Direct LWS |
| **Bi-directional** | ✅ Yes | ❌ No | ❌ No |
| **Resampling** | ✅ User choice | ✅ User choice | ✅ User choice |
| **Mix-type** | ✅ Mono/Mixed/Stereo | ✅ Mono/Mixed/Stereo | ✅ Mono/Mixed/Stereo |
| **Metadata** | ✅ JSON (8KB) | ✅ JSON (8KB) | ✅ JSON (8KB) |

---

## 📝 Configuration

### Environment Variables

```bash
# AWS Transcribe (optional - can use IAM roles instead)
AWS_ACCESS_KEY_ID=AKIA...
AWS_SECRET_ACCESS_KEY=...
AWS_REGION=us-east-1

# Deepgram (optional - can set per-call via channel variables)
DEEPGRAM_API_KEY=...

# Module buffer settings
MOD_AUDIO_FORK_BUFFER_SECS=2
MOD_AUDIO_FORK_SERVICE_THREADS=1
```

### Channel Variables (Per-Call Configuration)

#### AWS Transcribe
```xml
<action application="set" data="AWS_ACCESS_KEY_ID=AKIA..."/>
<action application="set" data="AWS_SECRET_ACCESS_KEY=..."/>
<action application="set" data="AWS_REGION=us-east-1"/>
<action application="set" data="AWS_SHOW_SPEAKER_LABEL=true"/>
<action application="set" data="AWS_VOCABULARY_NAME=my-vocab"/>
```

#### Deepgram
```xml
<action application="set" data="DEEPGRAM_API_KEY=..."/>
<action application="set" data="DEEPGRAM_SPEECH_MODEL=nova-2"/>
<action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
<action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>
```

---

## 🐛 Troubleshooting

### Check Module Dependencies

```bash
# Inside container
docker exec -it freeswitch-speech-ai bash

# Check mod_audio_fork
ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so

# Check mod_aws_transcribe
ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so

# Check mod_deepgram_transcribe
ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so
```

### View Logs

```bash
# Docker logs
docker logs -f freeswitch-speech-ai

# FreeSWITCH logs
docker exec -it freeswitch-speech-ai tail -f /usr/local/freeswitch/log/freeswitch.log
```

### Common Issues

#### Module not loading
- Check `modules.conf.xml` has `<load module="mod_X"/>`
- Verify dependencies: `ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_X.so`
- Check logs for error messages

#### AWS authentication fails
- Verify AWS credentials are set (env vars or IAM role)
- Check AWS region is correct
- Test credentials with `aws sts get-caller-identity`

#### Deepgram connection fails
- Verify API key is set
- Check network connectivity to api.deepgram.com
- Ensure port 443 (wss) is not blocked

---

## 📋 Build Details

### Build Time
- **Total**: ~30-40 minutes
  - libwebsockets: ~2 minutes
  - AWS SDK C++: ~25 minutes
  - Modules: ~3 minutes
  - Validation: ~5 minutes

### Image Size
- **Final image**: ~950 MB
  - Base FreeSWITCH: ~700 MB
  - AWS SDK: ~150 MB
  - libwebsockets: ~5 MB
  - Modules: ~5 MB

### Dependencies
- **libwebsockets 4.3.3** (mod_audio_fork, mod_deepgram_transcribe)
- **AWS SDK C++ 1.11.345** (mod_aws_transcribe)
- **speexdsp** (all modules - resampling)

---

## 🔍 What's Inside

### Directory Structure
```
/usr/local/freeswitch/
├── bin/
│   ├── freeswitch
│   └── fs_cli
├── lib/freeswitch/mod/
│   ├── mod_audio_fork.so
│   ├── mod_aws_transcribe.so
│   └── mod_deepgram_transcribe.so
├── conf/
│   └── autoload_configs/
│       └── modules.conf.xml (all modules auto-loaded)
└── log/
    └── freeswitch.log

/usr/local/lib/
├── libwebsockets.so*
├── libaws-cpp-sdk-core.so*
├── libaws-cpp-sdk-transcribestreaming.so*
└── (other AWS SDK libs)
```

---

## 🎯 Next Steps

1. **Build the image**:
   ```bash
   ./dockerfiles/build-all-modules.sh
   ```

2. **Run the container**:
   ```bash
   docker-compose up -d
   ```

3. **Test a module**:
   ```bash
   docker exec -it freeswitch-speech-ai fs_cli
   ```

4. **Make a test call** and use the transcription commands!

---

## 📚 Documentation

- [mod_audio_fork README](../modules/mod_audio_fork/README.md)
- [mod_aws_transcribe README](../modules/mod_aws_transcribe/README.md)
- [mod_deepgram_transcribe README](../modules/mod_deepgram_transcribe/README.md)

---

## ✅ Features Implemented

All three modules now have complete feature parity:

- ✅ User-configurable sampling rate (8k/16k/custom)
- ✅ Mono/Mixed/Stereo support via SMBF flags
- ✅ Metadata support (up to 8KB JSON)
- ✅ Session lifecycle events (session_start, session_stop)
- ✅ Unified API syntax
- ✅ No unused files (simple_buffer.h removed)
- ✅ Optimal buffering for each backend

**All modules are production-ready!** 🚀
