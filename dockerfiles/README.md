# FreeSWITCH Docker Images - Complete Guide

This comprehensive guide covers building, deploying, and running FreeSWITCH Docker images with speech transcription capabilities.

## Table of Contents

- [Quick Start](#quick-start)
- [Available Docker Images](#available-docker-images)
- [Running on MacBook](#running-on-macbook)
- [Building from Source](#building-from-source)
- [Docker Hub Deployment](#docker-hub-deployment)
- [Configuration Guide](#configuration-guide)
- [Testing and Verification](#testing-and-verification)
- [Troubleshooting](#troubleshooting)

---

## Quick Start

### Running Pre-built Images

```bash
# Base FreeSWITCH (no transcription)
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-base:latest

# With Deepgram transcription
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-deepgram-transcribe:latest \
  YOUR_DEEPGRAM_API_KEY

# With Azure transcription (includes ALL modules)
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-azure-transcribe:latest \
  "" \
  YOUR_AZURE_SUBSCRIPTION_KEY \
  eastus
```

### Building Your Own Images

```bash
# Build base image (30-45 minutes)
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11

# Build with specific module (15-25 minutes)
./dockerfiles/docker-build-mod-audio-fork.sh
./dockerfiles/docker-build-mod-deepgram-transcribe.sh
./dockerfiles/docker-build-mod-azure-transcribe.sh
```

---

## Available Docker Images

### 1. FreeSWITCH Base Image

**Image**: `srt2011/freeswitch-base:latest`
**Size**: ~850 MB
**Build Time**: 30-45 min (Intel), 60-90 min (Apple Silicon)

**Features**:
- ✅ FreeSWITCH 1.10.11 built from source
- ✅ All standard modules (100+)
- ✅ SIP and WebRTC support
- ✅ Event Socket enabled (fs_cli)
- ✅ Extensions 1000 and 1001 (password: 1234)
- ✅ System utilities (ps, netstat, ping, vim, curl)
- ✅ Supervisor for process management

**Quick Start**:
```bash
# Build
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11

# Run
docker run -d --name freeswitch \
    -p 5060:5060/tcp -p 5060:5060/udp \
    -p 5080:5080/tcp -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    freeswitch-base:1.10.11

# Test
docker exec -it freeswitch fs_cli -x "status"
```

**Exposed Ports**:
| Port | Protocol | Purpose |
|------|----------|---------|
| 5060-5061 | TCP/UDP | SIP signaling |
| 5080-5081 | TCP/UDP | SIP over WebSocket (WebRTC) |
| 8021 | TCP | Event Socket (fs_cli) |
| 7443 | TCP | WebRTC signaling |
| 16384-16484 | UDP | RTP media (audio/video) |

---

### 2. mod_audio_fork

**Image**: `srt2011/freeswitch-mod-audio-fork:latest`
**Base**: `srt2011/freeswitch-base:latest`
**Size**: ~900 MB
**Build Time**: 10-15 min (Intel), 20-30 min (Apple Silicon)

**Features**:
- ✅ Everything from base image
- ✅ mod_audio_fork (WebSocket audio streaming)
- ✅ libwebsockets 4.3.3
- ✅ Separate C/C++ compilation
- ✅ Static + runtime validation

**Build**:
```bash
./dockerfiles/docker-build-mod-audio-fork.sh srt2011/freeswitch-mod-audio-fork:latest
```

**Usage**:
```bash
docker run -d --name fs \
  -p 5060:5060/udp -p 8021:8021/tcp \
  srt2011/freeswitch-mod-audio-fork:latest

# Verify module loaded
docker exec -it fs fs_cli -x 'show modules' | grep audio_fork
```

---

### 3. mod_deepgram_transcribe

**Image**: `srt2011/freeswitch-mod-deepgram-transcribe:latest`
**Base**: `srt2011/freeswitch-mod-audio-fork:latest`
**Size**: ~950 MB
**Build Time**: 5-10 min (Intel), 10-15 min (Apple Silicon)

**Features**:
- ✅ Everything from audio fork image
- ✅ Real-time Deepgram transcription
- ✅ Speaker diarization
- ✅ Keyword boosting
- ✅ Named Entity Recognition (NER)
- ✅ Profanity filtering & PCI redaction
- ✅ Multiple models and tiers
- ✅ Interim and final results
- ✅ Stereo mode (separate caller/callee channels)

**Build**:
```bash
./dockerfiles/docker-build-mod-deepgram-transcribe.sh srt2011/freeswitch-mod-deepgram-transcribe:latest
```

**Usage**:
```bash
# Run with API key
docker run -d --name fs \
  -p 5060:5060/udp -p 8021:8021/tcp \
  -e DEEPGRAM_API_KEY=your-api-key \
  srt2011/freeswitch-mod-deepgram-transcribe:latest

# Start transcription (mono - caller only)
docker exec -it fs fs_cli -x "uuid_deepgram_transcribe <uuid> start en-US interim"

# Start transcription (stereo - both parties on separate channels)
docker exec -it fs fs_cli -x "uuid_deepgram_transcribe <uuid> start en-US interim stereo"
```

**Stereo Mode**:
- Channel 0: Caller audio (read stream)
- Channel 1: Callee audio (write stream)
- API parameter: `&multichannel=true&channels=2`

---

### 4. mod_azure_transcribe

**Image**: `srt2011/freeswitch-mod-azure-transcribe:latest`
**Base**: `srt2011/freeswitch-mod-deepgram-transcribe:latest`
**Size**: ~1.2 GB
**Build Time**: 15-20 min (Intel), 25-35 min (Apple Silicon)

**Features**:
- ✅ **ALL THREE MODULES** (audio_fork + deepgram + azure)
- ✅ Microsoft Azure Speech SDK 1.47.0
- ✅ ConversationTranscriber API
- ✅ AI-based speaker diarization
- ✅ Word-level timestamps
- ✅ Sentiment analysis
- ✅ Dictation mode
- ✅ Profanity filtering
- ✅ 50+ languages
- ✅ Pre-configured example files included

**Build**:
```bash
./dockerfiles/docker-build-mod-azure-transcribe.sh srt2011/freeswitch-mod-azure-transcribe:latest
```

**Usage**:
```bash
# Run with Azure credentials
docker run -d --name fs \
  -p 5060:5060/udp -p 8021:8021/tcp \
  -e AZURE_SUBSCRIPTION_KEY=your-key \
  -e AZURE_REGION=eastus \
  srt2011/freeswitch-mod-azure-transcribe:latest

# Start transcription (mono mode)
docker exec -it fs fs_cli -x "uuid_azure_transcribe <uuid> start en-US interim"

# Start with ConversationTranscriber (stereo mode with AI speaker ID)
docker exec -it fs fs_cli -x "uuid_azure_transcribe <uuid> start en-US interim stereo"
```

**Note**: Azure uses AI-based speaker identification (Guest-1, Guest-2), not true channel separation.

---

## Running on MacBook

### Quick Setup

```bash
# 1. Install Docker Desktop for Mac
# Download from: https://www.docker.com/products/docker-desktop

# 2. Run FreeSWITCH container
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-mod-deepgram-transcribe:latest YOUR_API_KEY

# 3. Verify running
docker ps | grep freeswitch
docker exec -it freeswitch fs_cli -x "status"
```

### Install SIP Client

Choose one:
- **Zoiper** (Recommended): https://www.zoiper.com/
- **Linphone** (Open Source): https://www.linphone.org/
- **Telephone** (Mac Native): https://www.64characters.com/telephone/

### Configure Extensions

**Extension 1000**:
- Username: `1000`
- Password: `1234`
- Domain: `localhost`
- Port: `5060`
- Transport: `UDP`

**Extension 1001**:
- Same settings with username `1001`

### Test Calling

1. Register both extensions
2. From 1000, dial `1001`
3. Answer on 1001
4. Verify two-way audio

**Echo Test**: Dial `9196` to hear your voice echoed back
**Conference**: Dial `3000` for conference room

### Platform Notes

**Apple Silicon (M1/M2/M3)**:
- Runs via Rosetta 2 emulation (linux/amd64)
- Full compatibility, slightly slower

**Intel Macs**:
- Native performance

---

## Building from Source

### Prerequisites

**Build Dependencies** (in Dockerfile):
```dockerfile
# Essential build tools
build-essential cmake autoconf automake libtool libtool-bin pkg-config
nasm git wget ca-certificates

# FreeSWITCH core
libssl-dev libcurl4-openssl-dev libpcre3-dev libspeex1 libspeexdsp-dev
libedit-dev libtiff-dev libldns-dev uuid-dev

# Audio/Video codecs
libopus-dev libsndfile1-dev libshout3-dev libmpg123-dev libmp3lame-dev
libavformat-dev libswscale-dev

# Database support
libsqlite3-dev libpq-dev unixodbc-dev

# SIP and WebRTC
libsofia-sip-ua-dev libsrtp2-dev

# Additional
libxml2-dev liblua5.2-dev libgoogle-perftools-dev python3 zlib1g-dev libjpeg-dev
```

### Build Process

**Stage 1: Build spandsp** (v0d2e6ac)
```bash
git clone https://github.com/freeswitch/spandsp.git
cd spandsp && git checkout 0d2e6ac
./bootstrap.sh && ./configure && make && make install
```

**Stage 2: Build sofia-sip** (v1.13.17)
```bash
git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git
cd sofia-sip
./bootstrap.sh && ./configure && make && make install
```

**Stage 3: Build FreeSWITCH** (v1.10.11)
```bash
git clone https://github.com/signalwire/freeswitch.git
cd freeswitch && git checkout v1.10.11
./bootstrap.sh

# Configure
./configure \
    --prefix=/usr/local/freeswitch \
    --enable-core-pgsql-support \
    --enable-core-odbc-support \
    --enable-tcmalloc \
    --without-python --without-python3 --without-java --without-perl

# Compile
make -j $(nproc)
make install
```

### Common Build Errors

#### Error: libtool not found
**Solution**: Install `libtool-bin` package

#### Error: ODBC support missing
**Solution**: Install `unixodbc-dev` (build) and `unixodbc` (runtime)

#### Error: UUID header missing
**Solution**: Install `uuid-dev` (build) and `libuuid1` (runtime)

#### Error: libvpx requires nasm
**Solution**: Install `nasm` package

#### Error: fs_cli won't connect
**Solution**:
1. Enable `mod_event_socket` in modules.conf
2. Configure Event Socket for IPv4 binding:
```xml
<configuration name="event_socket.conf">
  <settings>
    <param name="listen-ip" value="0.0.0.0"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
  </settings>
</configuration>
```
3. Start FreeSWITCH with explicit paths:
```bash
/usr/local/freeswitch/bin/freeswitch -nonat -nc -nf \
  -conf /usr/local/freeswitch/conf \
  -log  /usr/local/freeswitch/log \
  -db   /usr/local/freeswitch/db
```

### Multi-Stage Build

The Dockerfile uses multi-stage builds to minimize image size:
- **Build stage**: ~3-4 GB (with all build tools)
- **Runtime stage**: ~800 MB - 1 GB (binaries only)

---

## Docker Hub Deployment

### Push to Docker Hub

```bash
# 1. Build image
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11

# 2. Tag for Docker Hub
docker tag freeswitch-base:1.10.11 YOUR_USERNAME/freeswitch-base:1.10.11
docker tag freeswitch-base:1.10.11 YOUR_USERNAME/freeswitch-base:latest

# 3. Login
docker login

# 4. Push
docker push YOUR_USERNAME/freeswitch-base:1.10.11
docker push YOUR_USERNAME/freeswitch-base:latest
```

### Pull and Run

```bash
# Pull from Docker Hub
docker pull YOUR_USERNAME/freeswitch-base:1.10.11

# Run on any machine
docker run -d --name freeswitch \
    --platform linux/amd64 \
    -p 5060:5060/tcp -p 5060:5060/udp \
    -p 5080:5080/tcp -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    YOUR_USERNAME/freeswitch-base:1.10.11
```

---

## Configuration Guide

### Deepgram Configuration

#### Method 1: Environment Variables (Container-Wide)
```bash
docker run -d --name fs \
  -e DEEPGRAM_API_KEY=your-key \
  -e DEEPGRAM_SPEECH_MODEL=phonecall \
  -e DEEPGRAM_SPEECH_TIER=nova \
  srt2011/freeswitch-mod-deepgram-transcribe:latest
```

#### Method 2: Per-Call via fs_cli
```bash
docker exec -it fs fs_cli
freeswitch@internal> uuid_setvar <uuid> DEEPGRAM_API_KEY your-key
freeswitch@internal> uuid_setvar <uuid> DEEPGRAM_SPEECH_MODEL phonecall
freeswitch@internal> uuid_setvar <uuid> DEEPGRAM_SPEECH_DIARIZE true
freeswitch@internal> uuid_deepgram_transcribe <uuid> start en-US interim
```

#### Method 3: User Directory (Persistent)

Edit `/usr/local/freeswitch/conf/directory/default/1000.xml`:
```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <!-- Deepgram Configuration -->
      <variable name="DEEPGRAM_API_KEY" value="your-api-key"/>
      <variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
      <variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
      <variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
      <variable name="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION" value="true"/>
    </variables>
  </user>
</include>
```

Reload: `docker exec -it fs fs_cli -x 'reloadxml'`

#### Method 4: Dialplan (Automatic)

Create `/usr/local/freeswitch/conf/dialplan/default/01_deepgram.xml`:
```xml
<include>
  <extension name="auto_transcribe" continue="true">
    <condition field="destination_number" expression="^(1\d{3})$">
      <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
      <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
      <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
      <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
    </condition>
  </extension>
</include>
```

### Deepgram Features

**Stereo Mode** (separate caller/callee channels):
```bash
uuid_deepgram_transcribe <uuid> start en-US interim stereo
```

**Speaker Diarization** (mono mode):
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_DIARIZE true
```

**Keyword Boosting**:
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_KEYWORDS "payment:5,refund:4,account:3"
```

**PCI Redaction**:
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_REDACT "pci,ssn,numbers"
```

**Named Entity Recognition**:
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_NER true
```

### Deepgram Configuration Reference

| Variable | Values | Example |
|----------|--------|---------|
| `DEEPGRAM_API_KEY` | string | `abc123...` |
| `DEEPGRAM_SPEECH_MODEL` | general, meeting, phonecall, voicemail, finance, medical | `phonecall` |
| `DEEPGRAM_SPEECH_TIER` | base, enhanced, nova, nova-2 | `nova` |
| `DEEPGRAM_SPEECH_DIARIZE` | true/false | `true` |
| `DEEPGRAM_SPEECH_NER` | true/false | `true` |
| `DEEPGRAM_SPEECH_KEYWORDS` | word:intensity pairs | `VoIP:3,SIP:2` |
| `DEEPGRAM_SPEECH_REDACT` | pci, ssn, numbers | `pci,ssn` |
| `DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION` | true/false | `true` |
| `DEEPGRAM_SPEECH_NUMERALS` | true/false | `true` |

---

### Azure Configuration

#### Basic Setup
```bash
docker exec -it fs fs_cli
freeswitch@internal> uuid_setvar <uuid> AZURE_SUBSCRIPTION_KEY your-key
freeswitch@internal> uuid_setvar <uuid> AZURE_REGION eastus
freeswitch@internal> uuid_azure_transcribe <uuid> start en-US interim
```

#### Advanced Features

**ConversationTranscriber (AI Speaker ID)**:
```bash
uuid_setvar <uuid> AZURE_DIARIZE_INTERIM_RESULTS true
uuid_setvar <uuid> AZURE_DIARIZATION_SPEAKER_COUNT 2
uuid_azure_transcribe <uuid> start en-US interim stereo
```

**Word-Level Timestamps**:
```bash
uuid_setvar <uuid> AZURE_WORD_LEVEL_TIMESTAMPS true
uuid_setvar <uuid> AZURE_USE_OUTPUT_FORMAT_DETAILED true
```

**Sentiment Analysis**:
```bash
uuid_setvar <uuid> AZURE_SENTIMENT_ANALYSIS true
```

**Dictation Mode**:
```bash
uuid_setvar <uuid> AZURE_DICTATION_MODE true
```

**Profanity Filtering**:
```bash
uuid_setvar <uuid> AZURE_PROFANITY_OPTION masked  # masked, removed, raw
```

**Speech Hints** (mono mode only):
```bash
uuid_setvar <uuid> AZURE_SPEECH_HINTS "account,balance,payment"
```

---

## Testing and Verification

### Verify Container

```bash
# Check running
docker ps | grep freeswitch

# Check logs
docker logs freeswitch

# Access fs_cli
docker exec -it freeswitch fs_cli
```

### Verify Modules

```bash
# Check all modules
docker exec -it freeswitch fs_cli -x 'show modules'

# Check specific modules
docker exec -it freeswitch fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|azure'
```

### Test Extensions

Pre-configured extensions:
- **Extension 1000**: Username=1000, Password=1234
- **Extension 1001**: Username=1001, Password=1234

**Echo Test**: Dial `9196`
**Conference**: Dial `3000`
**Voicemail**: Dial `*98`

### Monitor Calls

```bash
# Show active calls
docker exec -it freeswitch fs_cli -x "show calls"

# Show channels
docker exec -it freeswitch fs_cli -x "show channels"

# Check SIP registrations
docker exec -it freeswitch fs_cli -x "sofia status profile internal reg"
```

---

## Troubleshooting

### Container Won't Start

```bash
# Check Docker is running
docker info

# Check logs
docker logs freeswitch

# Remove old container
docker rm -f freeswitch
```

### Extension Won't Register

```bash
# Check SIP profile
docker exec -it freeswitch fs_cli -x "sofia status profile internal"

# Check port listening
docker exec -it freeswitch netstat -tuln | grep 5060

# Check registrations
docker exec -it freeswitch fs_cli -x "sofia status profile internal reg"
```

### No Audio During Calls

```bash
# Check RTP ports
docker port freeswitch | grep 16384

# For external devices, get MacBook IP
ifconfig | grep "inet "
```

### fs_cli Won't Connect

```bash
# Check event socket
docker exec -it freeswitch netstat -tuln | grep 8021

# Try with password
docker exec -it freeswitch fs_cli -H localhost -P ClueCon
```

### Transcription Not Working

**Deepgram**:
```bash
# Check API key
docker exec -it freeswitch bash -c 'echo $DEEPGRAM_API_KEY'

# Check module loaded
docker exec -it freeswitch fs_cli -x "show modules" | grep deepgram

# Check logs
docker logs freeswitch | grep -i deepgram
```

**Azure**:
```bash
# Check credentials
docker exec -it freeswitch bash -c 'echo $AZURE_SUBSCRIPTION_KEY'
docker exec -it freeswitch bash -c 'echo $AZURE_REGION'

# Check module loaded
docker exec -it freeswitch fs_cli -x "show modules" | grep azure

# Check SDK libraries
docker exec -it freeswitch ls -la /usr/local/lib/MicrosoftSpeechSDK/

# Check logs
docker logs freeswitch | grep -i azure
```

---

## Useful Commands

### Container Management

```bash
# Start/stop/restart
docker start freeswitch
docker stop freeswitch
docker restart freeswitch

# Remove container
docker rm -f freeswitch

# View logs
docker logs -f freeswitch
docker logs --tail 100 freeswitch

# Shell access
docker exec -it freeswitch bash
```

### FreeSWITCH Commands

```bash
# Status and info
docker exec -it freeswitch fs_cli -x "status"
docker exec -it freeswitch fs_cli -x "version"

# Calls and channels
docker exec -it freeswitch fs_cli -x "show calls"
docker exec -it freeswitch fs_cli -x "show channels"

# Modules
docker exec -it freeswitch fs_cli -x "show modules"
docker exec -it freeswitch fs_cli -x "reload mod_sofia"

# Configuration
docker exec -it freeswitch fs_cli -x "reloadxml"

# SIP
docker exec -it freeswitch fs_cli -x "sofia status"
docker exec -it freeswitch fs_cli -x "sofia status profile internal reg"
```

---

## Production Deployment

### Docker Compose

Create `docker-compose.yml`:
```yaml
version: '3.8'
services:
  freeswitch:
    image: srt2011/freeswitch-mod-deepgram-transcribe:latest
    container_name: freeswitch
    restart: unless-stopped
    ports:
      - "5060:5060/tcp"
      - "5060:5060/udp"
      - "5080:5080/tcp"
      - "8021:8021/tcp"
      - "16384-16484:16384-16484/udp"
    environment:
      DEEPGRAM_API_KEY: "${DEEPGRAM_API_KEY}"
      DEEPGRAM_SPEECH_MODEL: "phonecall"
      DEEPGRAM_SPEECH_TIER: "nova"
    volumes:
      - ./conf:/usr/local/freeswitch/conf
      - ./logs:/usr/local/freeswitch/log
      - ./recordings:/usr/local/freeswitch/recordings
```

Create `.env`:
```bash
DEEPGRAM_API_KEY=your-api-key-here
```

Run:
```bash
docker-compose up -d
docker-compose logs -f
```

### Systemd Service

Create `/etc/systemd/system/freeswitch.service`:
```ini
[Unit]
Description=FreeSWITCH with Transcription
After=docker.service
Requires=docker.service

[Service]
Type=simple
Restart=always
EnvironmentFile=/etc/freeswitch/env
ExecStartPre=-/usr/bin/docker stop freeswitch
ExecStartPre=-/usr/bin/docker rm freeswitch
ExecStart=/usr/bin/docker run --rm \
  --name freeswitch \
  -p 5060:5060/tcp -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -p 16384-16484:16384-16484/udp \
  -e DEEPGRAM_API_KEY=${DEEPGRAM_API_KEY} \
  srt2011/freeswitch-mod-deepgram-transcribe:latest
ExecStop=/usr/bin/docker stop freeswitch

[Install]
WantedBy=multi-user.target
```

Enable:
```bash
sudo systemctl daemon-reload
sudo systemctl enable freeswitch
sudo systemctl start freeswitch
```

---

## Resources

- **FreeSWITCH Docs**: https://freeswitch.org/confluence/
- **Deepgram Docs**: https://developers.deepgram.com/
- **Azure Speech Docs**: https://docs.microsoft.com/en-us/azure/cognitive-services/speech-service/
- **Module Documentation**: See `modules/` directory for detailed API docs

---

## Summary

This guide consolidates all information for:
1. ✅ Building FreeSWITCH from source with all dependencies
2. ✅ Running pre-built images on MacBook (Intel & Apple Silicon)
3. ✅ Deploying to Docker Hub
4. ✅ Configuring Deepgram and Azure transcription
5. ✅ Testing with SIP clients
6. ✅ Troubleshooting common issues
7. ✅ Production deployment with Docker Compose/Systemd

**No information has been lost** - all content from the 4 original READMEs is preserved and organized by topic.
