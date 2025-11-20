# FreeSWITCH Docker Images - Complete Documentation

> **Comprehensive guide for building, deploying, and running FreeSWITCH Docker images with speech transcription capabilities.**

---

## 📑 Table of Contents

### Quick Navigation
- [🚀 Quick Start](#-quick-start)
- [🐳 Available Docker Images](#-available-docker-images)
  - [1. FreeSWITCH Base Image](#1-freeswitch-base-image)
  - [2. mod_audio_fork](#2-mod_audio_fork)
  - [3. mod_deepgram_transcribe](#3-mod_deepgram_transcribe)
  - [4. mod_azure_transcribe](#4-mod_azure_transcribe)
  - [5. mod_aws_transcribe](#5-mod_aws_transcribe)
- [⚙️ Configuration Guide](#️-configuration-guide)
  - [Deepgram Configuration](#deepgram-configuration)
  - [Azure Configuration](#azure-configuration)
  - [AWS Configuration](#aws-configuration)
- [🧪 Testing & Verification](#-testing--verification)
- [🔧 Troubleshooting](#-troubleshooting)
- [🚢 Production Deployment](#-production-deployment)
- [📚 Useful Commands Reference](#-useful-commands-reference)

### Detailed Guides (Appendices)
- [📦 Appendix A: Complete FreeSWITCH Installation Guide](#appendix-a-complete-freeswitch-installation-guide)
  - Build dependencies, errors & solutions, multi-stage builds
- [🍎 Appendix B: Complete MacBook Testing Guide](#appendix-b-complete-macbook-testing-guide)
  - SIP client setup, testing procedures, platform notes
- [☁️ Appendix C: Docker Hub Deployment Guide](#appendix-c-docker-hub-deployment-guide)
  - Push/pull workflow, production deployment

---

## 🚀 Quick Start

### Running Pre-built Images

\`\`\`bash
# Base FreeSWITCH (no transcription)
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-base:latest

# With Deepgram transcription
./dockerfiles/run-on-macbook.sh \\
  srt2011/freeswitch-mod-deepgram-transcribe:latest \\
  YOUR_DEEPGRAM_API_KEY

# With Azure transcription (includes ALL modules)
./dockerfiles/run-on-macbook.sh \\
  srt2011/freeswitch-mod-azure-transcribe:latest \\
  "" \\
  YOUR_AZURE_SUBSCRIPTION_KEY \\
  eastus

# With AWS transcription
./dockerfiles/run-on-macbook.sh \\
  srt2011/freeswitch-mod-aws-transcribe:latest
\`\`\`

### Building Your Own Images

\`\`\`bash
# Build base image (30-45 minutes)
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11

# Build with specific module (15-35 minutes)
./dockerfiles/docker-build-mod-audio-fork.sh
./dockerfiles/docker-build-mod-deepgram-transcribe.sh
./dockerfiles/docker-build-mod-azure-transcribe.sh
./dockerfiles/docker-build-mod-aws-transcribe.sh
\`\`\`

---


## 1. FreeSWITCH Base Image (Recommended Starting Point)

**Files**:
- Dockerfile: `Dockerfile.freeswitch-base`
- Build Script: `build-freeswitch-base.sh`
- Detailed Install Guide: See [Appendix A: Complete FreeSWITCH Installation Guide](#appendix-a-complete-freeswitch-installation-guide)
- Deployment Guide: See [Appendix C: Docker Hub Deployment Guide](#appendix-c-docker-hub-deployment-guide)

### Features
- ✅ FreeSWITCH 1.10.11 (production release) built from source
- ✅ All standard modules compiled (100+ modules)
- ✅ SIP and WebRTC support
- ✅ Event socket enabled (fs_cli ready)
- ✅ Extensions 1000 and 1001 pre-configured (password: 1234)
- ✅ System utilities (ps, netstat, ping, vim, curl)
- ✅ Supervisor for process management

### Quick Start

#### Build the Image
```bash
# Build (30-45 minutes on Intel, 60-90 on Apple Silicon)
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11
```

#### Run FreeSWITCH
```bash
# Run with all ports mapped
docker run -d --name freeswitch \
    -p 5060:5060/tcp -p 5060:5060/udp \
    -p 5080:5080/tcp -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    freeswitch-base:1.10.11

# Wait for startup
sleep 30

# Connect with fs_cli
docker exec -it freeswitch fs_cli
```

#### Verify Installation
```bash
# Check FreeSWITCH status
docker exec freeswitch fs_cli -x "status"

# List loaded modules (should be 100+)
docker exec freeswitch fs_cli -x "show modules" | wc -l

# Check SIP profiles
docker exec freeswitch fs_cli -x "sofia status"

# View logs
docker logs -f freeswitch
```

#### Test Extensions
Pre-configured extensions ready to use:
- **Extension 1000**: Username=1000, Password=1234
- **Extension 1001**: Username=1001, Password=1234

Register SIP clients (Zoiper, Linphone, etc.) and test calling between extensions.

### Use Cases
- ✅ Production-ready FreeSWITCH deployment
- ✅ Testing SIP/WebRTC functionality
- ✅ Base for extending with custom modules
- ✅ Learning FreeSWITCH configuration

### Exposed Ports
| Port | Protocol | Purpose |
|------|----------|---------|
| 5060-5061 | TCP/UDP | SIP signaling |
| 5080-5081 | TCP/UDP | SIP over WebSocket (WebRTC) |
| 8021 | TCP | Event Socket (fs_cli) |
| 7443 | TCP | WebRTC signaling |
| 16384-16484 | UDP | RTP media (audio/video) |

---

## 2. Individual Module Testing

This approach provides several benefits:

## Benefits

1. **Faster Validation** - Build only the required module and dependencies (15-25 min vs 90-150 min)
2. **Individual Testing** - Test each module independently without interference from other modules
3. **Debugging** - Easier to identify module-specific dependency issues
4. **Development** - Quick iteration when developing or modifying a single module
5. **Minimal Footprint** - Smaller Docker images with only necessary dependencies

## Available Modules

### mod_audio_fork

**File**: `Dockerfile.mod_audio_fork`
**Build Script**: `docker-build-mod-audio-fork.sh`
**Base Image**: `srt2011/freeswitch-base:latest` (pre-built production FreeSWITCH)

**Dependencies Built**:
- libwebsockets 4.3.3 (WebSocket connectivity)
- libspeexdsp (speex resampler for audio)

**Build Time**:
- Intel/AMD64: **10-15 minutes** (6x faster than full build!)
- Apple Silicon: **20-30 minutes** (with emulation)

**Usage**:
```bash
# Build the image (with custom tag)
./dockerfiles/docker-build-mod-audio-fork.sh srt2011/freeswitch-mod-audio-fork:latest

# Or use default tag
./dockerfiles/docker-build-mod-audio-fork.sh

# Run FreeSWITCH (inherits base image behavior)
docker run -d --name fs \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  srt2011/freeswitch-mod-audio-fork:latest

# Access fs_cli
docker exec -it fs fs_cli

# Verify mod_audio_fork loaded
docker exec -it fs fs_cli -x 'show modules' | grep audio_fork
api,uuid_audio_fork,mod_audio_fork,/usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so

docker exec -it fs grep -i "audio_fork" /usr/local/freeswitch/log/freeswitch.log
==>
2025-11-17 18:37:15.999527 0.00% [NOTICE] mod_audio_fork.c:300 mod_audio_fork API loading..
2025-11-17 18:37:15.999549 0.00% [NOTICE] lws_glue.cpp:372 mod_audio_fork: audio buffer (in secs):    2 secs
2025-11-17 18:37:15.999551 0.00% [NOTICE] lws_glue.cpp:373 mod_audio_fork: sub-protocol:              audio.drachtio.org
2025-11-17 18:37:15.999552 0.00% [NOTICE] lws_glue.cpp:374 mod_audio_fork: lws service threads:       1
2025-11-17 18:37:15.999623 0.00% [NOTICE] mod_audio_fork.c:324 mod_audio_fork API successfully loaded
2025-11-17 18:37:15.999630 0.00% [CONSOLE] switch_loadable_module.c:1772 Successfully Loaded [mod_audio_fork]
2025-11-17 18:37:15.999639 0.00% [NOTICE] switch_loadable_module.c:389 Adding API Function 'uuid_audio_fork'

docker exec -it fs ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so
        linux-vdso.so.1 (0x00007ffc44973000)
        libwebsockets.so.19 => /usr/local/lib/libwebsockets.so.19 (0x000073838879e000)
        libpthread.so.0 => /lib/x86_64-linux-gnu/libpthread.so.0 (0x0000738388777000)
        libstdc++.so.6 => /usr/lib/x86_64-linux-gnu/libstdc++.so.6 (0x00007383885aa000)
        libgcc_s.so.1 => /lib/x86_64-linux-gnu/libgcc_s.so.1 (0x0000738388590000)
        libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007383883bc000)
        libssl.so.1.1 => /usr/lib/x86_64-linux-gnu/libssl.so.1.1 (0x0000738388329000)
        libcrypto.so.1.1 => /usr/lib/x86_64-linux-gnu/libcrypto.so.1.1 (0x0000738388033000)
        /lib64/ld-linux-x86-64.so.2 (0x00007383888d9000)
        libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x0000738387eef000)
        libdl.so.2 => /lib/x86_64-linux-gnu/libdl.so.2 (0x0000738387ee9000)

docker exec -it freeswitch fs_cli -x 'show calls'                                                     
uuid,direction,created,created_epoch,name,state,cid_name,cid_num,ip_addr,dest,presence_id,presence_data,accountcode,callstate,callee_name,callee_num,callee_direction,call_uuid,hostname,sent_callee_name,sent_callee_num,b_uuid,b_direction,b_created,b_created_epoch,b_name,b_state,b_cid_name,b_cid_num,b_ip_addr,b_dest,b_presence_id,b_presence_data,b_accountcode,b_callstate,b_callee_name,b_callee_num,b_callee_direction,b_sent_callee_name,b_sent_callee_num,call_created_epoch
bba5d840-47d1-4245-8319-deda26f01b95,inbound,2025-11-17 19:09:55,1763406595,sofia/internal/1000@192.168.64.2,CS_EXECUTE,1000,1000,192.168.64.1,1001,1000@192.168.64.2,,1000,ACTIVE,Outbound Call,1001,SEND,bba5d840-47d1-4245-8319-deda26f01b95,docker-desktop,Outbound Call,1001,17f0b761-a8f8-4309-8d9a-224a98f9bfce,outbound,2025-11-17 19:10:06,1763406606,sofia/internal/1001@192.168.64.1:50357,CS_EXCHANGE_MEDIA,Extension 1000,1000,192.168.64.1,1001,1001@192.168.64.2,,,ACTIVE,Outbound Call,1001,SEND,Extension 1000,1000,1763406608

docker exec -it freeswitch fs_cli -x 'uuid_audio_fork bba5d840-47d1-4245-8319-deda26f01b95 start ws://20.244.30.42:8077/stream mono 16k'
+OK Success

python3 fs_ws_dg_01.py 
2025-11-17 19:09:06,327 [WS-REC] server listening on 0.0.0.0:8077
✅ WebSocket audio recorder running ws://0.0.0.0:8077/stream
Features: 🎤 Transcription, 📡 Pusher
🔄 Ready for connections...
2025-11-17 19:12:00,549 [WS-REC] connection open
✅ Deepgram connected - Session: 122_169_28_22_50455_1763406720
🔌 Connection from ('122.169.28.22', 50455)
🎤 Transcription enabled - Session: 122_169_28_22_50455_1763406720
📡 Pusher enabled - Session: 122_169_28_22_50455_1763406720
👤 [Speaker 0] - 🔥 FINAL - No.
👤 [Speaker 0] - ⚡ INTERIM - Hello?
👤 [Speaker 0] - 🔥 FINAL - Hello?
👤 [Speaker 0] - ⚡ INTERIM - Hello?
👤 [Speaker 0] - 🔥 FINAL - Hello?

docker exec -it freeswitch fs_cli -x 'uuid_audio_fork bba5d840-47d1-4245-8319-deda26f01b95 stop'                                        
+OK Success

```

**Features**:
- ✅ Builds on production FreeSWITCH base image
- ✅ Only builds libwebsockets + mod_audio_fork (minimal dependencies)
- ✅ Inherits all base image configuration (SIP extensions, Event Socket, etc.)
- ✅ Automatic static + runtime validation during build
- ✅ Separate C/C++ compilation for proper type handling
- ✅ 182-line Dockerfile (48% smaller than original)
- ✅ Behaves identically to base image with mod_audio_fork added

<details>
<summary><b>Manual Verification for mod_audio_fork</b></summary>

After building or pulling the mod_audio_fork image, verify it manually:


</details>


#### Step 1: Check Module File Exists

```bash
# Verify mod_audio_fork.so exists in the image
docker run --rm freeswitch-mod-audio-fork:latest ls -lh /usr/local/freeswitch/mod/mod_audio_fork.so

# Should show file with size around 200-400 KB
```

#### Step 2: Check Module Dependencies

```bash
# Verify module is linked with libwebsockets
docker run --rm freeswitch-mod-audio-fork:latest ldd /usr/local/freeswitch/mod/mod_audio_fork.so | grep -i websockets

# Should show: libwebsockets.so.19 => /usr/local/lib/libwebsockets.so.19
```

Check for missing dependencies:
```bash
# Run full ldd check
docker run --rm freeswitch-mod-audio-fork:latest ldd /usr/local/freeswitch/mod/mod_audio_fork.so

# Should show NO "not found" entries
```

#### Step 3: Verify Module Loading

```bash
# Start FreeSWITCH and check module loading
docker run --rm freeswitch-mod-audio-fork:latest bash -c '
    echo "Starting FreeSWITCH in background..."
    /usr/local/freeswitch/bin/freeswitch -nc -nf > /dev/null 2>&1 &
    FS_PID=$!

    echo "Waiting 15 seconds for FreeSWITCH to start..."
    sleep 15

    echo ""
    echo "Checking FreeSWITCH log for mod_audio_fork..."
    grep -i "mod_audio_fork" /usr/local/freeswitch/log/freeswitch.log || echo "❌ mod_audio_fork NOT found in logs"

    kill $FS_PID 2>/dev/null || true
'
```

Expected output should include:
```
[NOTICE] mod_audio_fork.c:300 mod_audio_fork API loading..
[NOTICE] lws_glue.cpp:372 mod_audio_fork: audio buffer (in secs):    2 secs
[NOTICE] lws_glue.cpp:373 mod_audio_fork: sub-protocol:              audio.drachtio.org
[NOTICE] lws_glue.cpp:374 mod_audio_fork: lws service threads:       1
[NOTICE] mod_audio_fork.c:324 mod_audio_fork API successfully loaded
[CONSOLE] switch_loadable_module.c:1772 Successfully Loaded [mod_audio_fork]
[NOTICE] switch_loadable_module.c:389 Adding API Function 'uuid_audio_fork'
```

#### Step 4: Check for Loading Errors

```bash
# Check for errors when loading mod_audio_fork
docker run --rm freeswitch-mod-audio-fork:latest bash -c '
    /usr/local/freeswitch/bin/freeswitch -nc -nf > /dev/null 2>&1 &
    sleep 15
    grep -i "mod_audio_fork" /usr/local/freeswitch/log/freeswitch.log | grep -iE "error|fail"
    if [ $? -eq 0 ]; then
        echo "❌ Errors detected when loading mod_audio_fork"
        exit 1
    else
        echo "✅ No errors detected"
    fi
'
```

#### Verification Summary

If all checks pass:
```
✅ mod_audio_fork is installed and loads successfully!
```

**To start FreeSWITCH with mod_audio_fork**:
```bash
docker run --rm -it freeswitch-mod-audio-fork:latest freeswitch -nc -nf
```

---

### mod_deepgram_transcribe

**File**: `Dockerfile.mod_deepgram_transcribe`
**Build Script**: `docker-build-mod-deepgram-transcribe.sh`
**Base Image**: `srt2011/freeswitch-mod-audio-fork:latest` (includes FreeSWITCH + libwebsockets)

**Dependencies Built**:
- None! (libwebsockets already in base image)

**Build Time**:
- Intel/AMD64: **5-10 minutes** (even faster - no dependencies to build!)
- Apple Silicon: **10-15 minutes** (with emulation)

**Usage**:
```bash
# Build the image (with custom tag)
./dockerfiles/docker-build-mod-deepgram-transcribe.sh srt2011/freeswitch-mod-deepgram-transcribe:latest

# Or use default tag
./dockerfiles/docker-build-mod-deepgram-transcribe.sh

# Run FreeSWITCH with Deepgram API key
docker run -d --name fs \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -e DEEPGRAM_API_KEY=your-api-key \
  srt2011/freeswitch-mod-deepgram-transcribe:latest

# Access fs_cli
docker exec -it fs fs_cli

# Verify both modules loaded (gets mod_audio_fork as bonus!)
docker exec -it fs fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram'
```

**Features**:
- ✅ Builds on mod_audio_fork image (inherits libwebsockets)
- ✅ Real-time streaming transcription via Deepgram API
- ✅ Speaker diarization to identify different speakers
- ✅ Keyword boosting for improved recognition
- ✅ Named Entity Recognition (NER)
- ✅ Profanity filtering and redaction (PCI, SSN)
- ✅ Multiple models (general, phonecall, meeting, voicemail, etc.)
- ✅ Model tiers (base, enhanced, nova, nova-2)
- ✅ Interim and final transcription results
- ✅ Includes mod_audio_fork from base image
- ✅ Automatic static + runtime validation during build

<details>
<summary><b>Manual Verification for mod_deepgram_transcribe</b></summary>

After building or pulling the mod_deepgram_transcribe image, verify it manually:


</details>


#### Step 1: Check Module File Exists

```bash
# Verify mod_deepgram_transcribe.so exists
docker run --rm freeswitch-mod-deepgram-transcribe:latest ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so

# Should show file with size around 200-500 KB
```

#### Step 2: Check Module Dependencies

```bash
# Verify module is linked with libwebsockets
docker run --rm freeswitch-mod-deepgram-transcribe:latest ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so | grep -i websockets

# Should show: libwebsockets.so.19 => /usr/local/lib/libwebsockets.so.19
```

Check for missing dependencies:
```bash
# Run full ldd check
docker run --rm freeswitch-mod-deepgram-transcribe:latest ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so

# Should show NO "not found" entries
```

#### Step 3: Verify Module Loading

```bash
# Start FreeSWITCH and check module loading
docker run --rm freeswitch-mod-deepgram-transcribe:latest bash -c '
    echo "Starting FreeSWITCH in background..."
    /usr/local/freeswitch/bin/freeswitch -nc -nf > /dev/null 2>&1 &
    FS_PID=$!

    echo "Waiting 15 seconds for FreeSWITCH to start..."
    sleep 15

    echo ""
    echo "Checking FreeSWITCH log for mod_deepgram_transcribe..."
    grep -i "mod_deepgram_transcribe" /usr/local/freeswitch/log/freeswitch.log || echo "❌ mod_deepgram_transcribe NOT found in logs"

    kill $FS_PID 2>/dev/null || true
'
```

Expected output should include:
```
[NOTICE] mod_deepgram_transcribe.c:XXX Successfully Loaded [mod_deepgram_transcribe]
[NOTICE] switch_loadable_module.c:389 Adding API Function 'uuid_deepgram_transcribe'
```

#### Step 4: Verify Both Modules (mod_audio_fork + mod_deepgram_transcribe)

```bash
# Start container and check both modules
docker run -d --name fs-test freeswitch-mod-deepgram-transcribe:latest
sleep 30

# Check both modules are loaded
docker exec fs-test fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram'

# Should show both:
# api,uuid_audio_fork,mod_audio_fork,...
# api,uuid_deepgram_transcribe,mod_deepgram_transcribe,...

# Cleanup
docker rm -f fs-test
```

#### Verification Summary

If all checks pass:
```
✅ mod_deepgram_transcribe is installed and loads successfully!
✅ Bonus: mod_audio_fork is also available!
```

**To start FreeSWITCH with mod_deepgram_transcribe**:
```bash
docker run --rm -it \
  -e DEEPGRAM_API_KEY=your-api-key \
  freeswitch-mod-deepgram-transcribe:latest \
  freeswitch -nc -nf
```

**Example API Usage**:
```bash
# In fs_cli or via ESL
uuid_setvar <call-uuid> DEEPGRAM_API_KEY your-api-key
uuid_setvar <call-uuid> DEEPGRAM_SPEECH_MODEL phonecall
uuid_setvar <call-uuid> DEEPGRAM_SPEECH_TIER nova
uuid_setvar <call-uuid> DEEPGRAM_SPEECH_DIARIZE true

# Start transcription with interim results
uuid_deepgram_transcribe <call-uuid> start en-US interim

# Stop transcription
uuid_deepgram_transcribe <call-uuid> stop
```

For full API documentation, see: `modules/mod_deepgram_transcribe/README.md`

---

## Configuration Guide for mod_deepgram_transcribe

<details>
<summary><b>Method 1</b></summary>

Set Deepgram API key and default configuration when starting the container:

```bash
docker run -d --name fs \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -e DEEPGRAM_API_KEY=your-deepgram-api-key-here \
  -e DEEPGRAM_SPEECH_MODEL=phonecall \
  -e DEEPGRAM_SPEECH_TIER=nova \
  srt2011/freeswitch-mod-deepgram-transcribe:latest
```

**Note**: Environment variables set at container level apply to ALL calls. For per-call configuration, use dialplan or fs_cli methods below.

---


</details>


<details>
<summary><b>Method 2</b></summary>

Add to `/usr/local/freeswitch/conf/dialplan/default.xml` or create a new file in `/usr/local/freeswitch/conf/dialplan/default/`:


</details>


#### Basic Transcription on All Inbound Calls

```xml
<!-- File: /usr/local/freeswitch/conf/dialplan/default/01_deepgram_auto_transcribe.xml -->
<include>
  <extension name="auto_transcribe_inbound" continue="true">
    <condition field="destination_number" expression="^(1\d{3})$">
      <!-- Set Deepgram API credentials -->
      <action application="set" data="DEEPGRAM_API_KEY=your-api-key-here"/>

      <!-- Configure transcription model -->
      <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
      <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>

      <!-- Enable features -->
      <action application="set" data="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
      <action application="set" data="DEEPGRAM_SPEECH_NUMERALS=true"/>

      <!-- Start transcription with interim results -->
      <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
    </condition>
  </extension>
</include>
```

**Note**: `continue="true"` allows the call to proceed to other extensions after starting transcription.

#### Advanced: Speaker Diarization + NER + Keyword Boosting

```xml
<extension name="sales_call_transcribe" continue="true">
  <condition field="destination_number" expression="^(2\d{3})$">
    <!-- API Key -->
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>

    <!-- Use meeting model with Nova-2 tier for best accuracy -->
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=meeting"/>
    <action application="set" data="DEEPGRAM_SPEECH_TIER=nova-2"/>

    <!-- Enable speaker diarization (identify different speakers) -->
    <action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>

    <!-- Enable Named Entity Recognition -->
    <action application="set" data="DEEPGRAM_SPEECH_NER=true"/>

    <!-- Boost important sales keywords (intensity 1-10) -->
    <action application="set" data="DEEPGRAM_SPEECH_KEYWORDS=pricing:5,discount:4,payment:3,contract:3"/>

    <!-- Enable automatic punctuation and numeral conversion -->
    <action application="set" data="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
    <action application="set" data="DEEPGRAM_SPEECH_NUMERALS=true"/>

    <!-- Get 3 alternative transcription hypotheses -->
    <action application="set" data="DEEPGRAM_SPEECH_ALTERNATIVES=3"/>

    <!-- Start transcription -->
    <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
  </condition>
</extension>
```

#### PCI Compliance: Redact Sensitive Information

```xml
<extension name="payment_call_transcribe" continue="true">
  <condition field="destination_number" expression="^(3\d{3})$">
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=finance"/>
    <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>

    <!-- Redact PCI data, SSN, and all numbers -->
    <action application="set" data="DEEPGRAM_SPEECH_REDACT=pci,ssn,numbers"/>

    <!-- Enable profanity filter -->
    <action application="set" data="DEEPGRAM_SPEECH_PROFANITY_FILTER=true"/>

    <!-- Start transcription -->
    <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
  </condition>
</extension>
```

#### Custom Model for Specific Domain

```xml
<extension name="medical_transcribe" continue="true">
  <condition field="destination_number" expression="^(4\d{3})$">
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>

    <!-- Use medical model -->
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=medical"/>
    <action application="set" data="DEEPGRAM_SPEECH_TIER=enhanced"/>

    <!-- Or use your custom trained model -->
    <!-- <action application="set" data="DEEPGRAM_SPEECH_CUSTOM_MODEL=your-model-id"/> -->

    <!-- Boost medical terminology -->
    <action application="set" data="DEEPGRAM_SPEECH_KEYWORDS=diagnosis:5,prescription:4,symptoms:3"/>

    <!-- Enable NER for medical entities -->
    <action application="set" data="DEEPGRAM_SPEECH_NER=true"/>

    <!-- Start transcription -->
    <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
  </condition>
</extension>
```

#### Search for Keywords in Transcript

```xml
<extension name="compliance_monitoring" continue="true">
  <condition field="destination_number" expression="^(5\d{3})$">
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>

    <!-- Search for compliance keywords in transcript -->
    <action application="set" data="DEEPGRAM_SPEECH_SEARCH=cancel,refund,complaint,lawsuit"/>

    <!-- Tag for tracking/organization -->
    <action application="set" data="DEEPGRAM_SPEECH_TAG=compliance-call"/>

    <!-- Start transcription -->
    <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
  </condition>
</extension>
```

#### Voice Activity Detection (VAD) Configuration

```xml
<extension name="vad_transcribe" continue="true">
  <condition field="destination_number" expression="^(6\d{3})$">
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=voicemail"/>

    <!-- Set endpointing: wait 2000ms of silence before detecting end of speech -->
    <action application="set" data="DEEPGRAM_SPEECH_ENDPOINTING=2000"/>

    <!-- VAD turnoff: wait 1000ms before turning off voice activity detection -->
    <action application="set" data="DEEPGRAM_SPEECH_VAD_TURNOFF=1000"/>

    <!-- Start transcription -->
    <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
  </condition>
</extension>
```

#### Apply Dialplan Changes

After editing dialplan files:

```bash
# Inside container
docker exec -it fs fs_cli -x "reloadxml"

# Or from fs_cli
freeswitch@internal> reloadxml
```

---

<details>
<summary><b>Method 3</b></summary>

Control transcription manually for specific calls using fs_cli:


</details>


#### Start Transcription on Active Call

```bash
# Enter fs_cli
docker exec -it fs fs_cli

# Show active calls to get UUID
freeswitch@internal> show channels

# Set variables for a specific call
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_API_KEY your-api-key
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_SPEECH_MODEL phonecall
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_SPEECH_TIER nova
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_SPEECH_DIARIZE true
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION true

# Start transcription with interim results
freeswitch@internal> uuid_deepgram_transcribe <call-uuid> start en-US interim

# Stop transcription
freeswitch@internal> uuid_deepgram_transcribe <call-uuid> stop
```

#### One-liner from Shell

```bash
# Start transcription
docker exec -it fs fs_cli -x "uuid_setvar <uuid> DEEPGRAM_API_KEY your-key"
docker exec -it fs fs_cli -x "uuid_setvar <uuid> DEEPGRAM_SPEECH_MODEL phonecall"
docker exec -it fs fs_cli -x "uuid_deepgram_transcribe <uuid> start en-US interim"

# Stop transcription
docker exec -it fs fs_cli -x "uuid_deepgram_transcribe <uuid> stop"
```

---

<details>
<summary><b>Method 4</b></summary>

Configure Deepgram settings per user by adding variables to user XML files. This is ideal when you want all calls from specific users/extensions to automatically have transcription capabilities with predefined settings.


</details>


#### Setup

**1. Edit user configuration file** (e.g., for extension 1000):

```bash
# Access container
docker exec -it fs bash

# Edit user file
vi /usr/local/freeswitch/conf/directory/default/1000.xml
```

**2. Add Deepgram variables to the user:**

```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
      <param name="vm-password" value="1000"/>
    </params>
    <variables>
      <!-- Standard user variables -->
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="accountcode" value="1000"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1000"/>
      <variable name="effective_caller_id_number" value="1000"/>

      <!-- Deepgram Transcription Variables -->
      <variable name="DEEPGRAM_API_KEY" value="your-deepgram-api-key"/>
      <variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
      <variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
      <variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
      <variable name="DEEPGRAM_SPEECH_DIARIZE_VERSION" value="latest"/>
      <variable name="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION" value="true"/>
    </variables>
  </user>
</include>
```

**3. Reload FreeSWITCH configuration:**

```bash
docker exec -it fs fs_cli -x 'reloadxml'
```

#### Usage

Once configured, the variables are automatically inherited by all calls from that user:

```bash
# Get active calls
docker exec -it fs fs_cli -x 'show calls'

# Start transcription (mono - caller only)
docker exec -it fs fs_cli -x 'uuid_deepgram_transcribe <uuid> start en-US interim'

# Start transcription (stereo - both parties on separate channels)
docker exec -it fs fs_cli -x 'uuid_deepgram_transcribe <uuid> start en-US interim stereo'

# Stop transcription
docker exec -it fs fs_cli -x 'uuid_deepgram_transcribe <uuid> stop'
```

#### Different Settings Per User/Department

**Sales Team (Extension 1000-1099):**
```xml
<!-- High-end transcription with diarization and NER -->
<variable name="DEEPGRAM_SPEECH_MODEL" value="meeting"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="nova-2"/>
<variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
<variable name="DEEPGRAM_SPEECH_NER" value="true"/>
<variable name="DEEPGRAM_SPEECH_KEYWORDS" value="pricing:5,discount:4,payment:3"/>
```

**Support Team (Extension 2000-2099):**
```xml
<!-- Standard transcription with keyword boosting -->
<variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
<variable name="DEEPGRAM_SPEECH_KEYWORDS" value="issue:3,problem:3,refund:2"/>
```

**Payment Processing (Extension 3000-3099):**
```xml
<!-- Finance model with PCI redaction -->
<variable name="DEEPGRAM_SPEECH_MODEL" value="finance"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="enhanced"/>
<variable name="DEEPGRAM_SPEECH_REDACT" value="pci,ssn,numbers"/>
<variable name="DEEPGRAM_SPEECH_PROFANITY_FILTER" value="true"/>
```

#### Benefits

- **Automatic inheritance**: Variables applied to all calls from that user
- **No per-call configuration**: Just start transcription, settings are already there
- **Centralized management**: One place to configure each user/extension
- **Department-level policies**: Different transcription settings per team
- **Persistent**: Settings survive across calls and container restarts (if directory is mounted)

#### Make Configuration Persistent

To persist user directory changes across container restarts, mount the directory as a volume:

```bash
docker run -d --name fs \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -v $(pwd)/freeswitch-config/directory:/usr/local/freeswitch/conf/directory \
  srt2011/freeswitch-mod-deepgram-transcribe:latest
```

---

### Audio Mixing Modes (Mono vs Stereo)

Deepgram module supports different audio channel configurations:

#### Mode 1: Mono (Default) - Caller Audio Only

**Usage:**
```bash
# Default mode - only captures caller audio
uuid_deepgram_transcribe <uuid> start en-US interim

# Explicitly specify mono
uuid_deepgram_transcribe <uuid> start en-US interim mono
```

**What's sent to Deepgram:**
- ✅ Single channel (mono)
- ✅ Caller audio only (read stream)
- ❌ Callee audio NOT included

**Best for:**
- Transcribing customer/caller side only
- Voicemail transcription
- One-sided call recording compliance

---

#### Mode 2: Stereo - Separate Caller and Callee Channels

**Usage:**
```bash
# Stereo mode - captures both caller and callee on separate channels
uuid_deepgram_transcribe <uuid> start en-US interim stereo
```

**What's sent to Deepgram:**
- ✅ Two channels (stereo)
- ✅ Channel 0: Caller audio (read stream)
- ✅ Channel 1: Callee audio (write stream)
- ✅ Deepgram API: `&multichannel=true&channels=2`

**Best for:**
- Call center quality monitoring
- Agent vs customer analysis
- Distinguishing speaker attribution
- Separate sentiment analysis per participant

**Dialplan Example:**
```xml
<extension name="stereo_transcribe">
  <condition field="destination_number" expression="^(2\d{3})$">
    <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
    <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
    <action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>

    <!-- Start stereo transcription -->
    <action application="uuid_deepgram_transcribe" data="start en-US interim stereo"/>
  </condition>
</extension>
```

---

#### Mixed Mode: NOT Supported

The module does **not** support mixed mode (single channel with both caller and callee mixed together).

**Alternatives:**
- Use **stereo mode** and merge transcripts in your application
- Use **mono mode** with **speaker diarization** (`DEEPGRAM_SPEECH_DIARIZE=true`) if you only need one participant's audio but want to identify speakers

**API Syntax:**
```
uuid_deepgram_transcribe <uuid> [start|stop] <lang-code> [interim] [stereo|mono]
```

**Examples:**
```bash
# Mono (caller only)
uuid_deepgram_transcribe abc123 start en-US interim mono

# Stereo (both parties, separate channels)
uuid_deepgram_transcribe abc123 start en-US interim stereo

# With dialplan (stereo)
<action application="uuid_deepgram_transcribe" data="start en-US interim stereo"/>
```

---

<details>
<summary><b>Method 4</b></summary>

#### Option A: Docker Compose with Environment Variables

Create `docker-compose.yml`:

```yaml
version: '3.8'

services:
  freeswitch:
    image: srt2011/freeswitch-mod-deepgram-transcribe:latest
    container_name: freeswitch-transcribe
    restart: unless-stopped

    # Network ports
    ports:
      - "5060:5060/tcp"
      - "5060:5060/udp"
      - "5080:5080/tcp"
      - "5080:5080/udp"
      - "8021:8021/tcp"
      - "16384-16484:16384-16484/udp"

    # Deepgram configuration
    environment:
      # Required: Deepgram API Key
      DEEPGRAM_API_KEY: "${DEEPGRAM_API_KEY}"

      # Optional: Default transcription settings
      DEEPGRAM_SPEECH_MODEL: "phonecall"
      DEEPGRAM_SPEECH_TIER: "nova"
      DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION: "true"
      DEEPGRAM_SPEECH_NUMERALS: "true"

      # FreeSWITCH settings
      FREESWITCH_LOG_LEVEL: "INFO"

    # Persist data
    volumes:
      - ./conf:/usr/local/freeswitch/conf
      - ./logs:/usr/local/freeswitch/log
      - ./recordings:/usr/local/freeswitch/recordings
      - ./storage:/usr/local/freeswitch/storage

    # Health check
    healthcheck:
      test: ["CMD", "fs_cli", "-x", "status"]
      interval: 30s
      timeout: 10s
      retries: 3
      start_period: 40s

  # Optional: Web UI for monitoring
  freeswitch-ui:
    image: nginx:alpine
    container_name: freeswitch-ui
    restart: unless-stopped
    ports:
      - "8080:80"
    volumes:
      - ./web:/usr/share/nginx/html
```

Create `.env` file:

```bash
# .env file - DO NOT commit to git
DEEPGRAM_API_KEY=your-actual-deepgram-api-key-here
```

Start service:

```bash
# Start in background
docker-compose up -d

# View logs
docker-compose logs -f freeswitch

# Stop service
docker-compose down
```


</details>


#### Option B: Systemd Service (Linux Host)

Create `/etc/systemd/system/freeswitch-deepgram.service`:

```ini
[Unit]
Description=FreeSWITCH with Deepgram Transcription
After=docker.service
Requires=docker.service

[Service]
Type=simple
Restart=always
RestartSec=10s
TimeoutStartSec=0

# Load environment from file
EnvironmentFile=/etc/freeswitch/deepgram.env

# Docker run command
ExecStartPre=-/usr/bin/docker stop freeswitch-transcribe
ExecStartPre=-/usr/bin/docker rm freeswitch-transcribe

ExecStart=/usr/bin/docker run --rm \
  --name freeswitch-transcribe \
  -p 5060:5060/tcp \
  -p 5060:5060/udp \
  -p 5080:5080/tcp \
  -p 5080:5080/udp \
  -p 8021:8021/tcp \
  -p 16384-16484:16384-16484/udp \
  -e DEEPGRAM_API_KEY=${DEEPGRAM_API_KEY} \
  -e DEEPGRAM_SPEECH_MODEL=${DEEPGRAM_SPEECH_MODEL} \
  -e DEEPGRAM_SPEECH_TIER=${DEEPGRAM_SPEECH_TIER} \
  -v /var/lib/freeswitch/conf:/usr/local/freeswitch/conf \
  -v /var/lib/freeswitch/logs:/usr/local/freeswitch/log \
  srt2011/freeswitch-mod-deepgram-transcribe:latest

ExecStop=/usr/bin/docker stop freeswitch-transcribe

[Install]
WantedBy=multi-user.target
```

Create environment file `/etc/freeswitch/deepgram.env`:

```bash
DEEPGRAM_API_KEY=your-deepgram-api-key-here
DEEPGRAM_SPEECH_MODEL=phonecall
DEEPGRAM_SPEECH_TIER=nova
```

Enable and start service:

```bash
# Reload systemd
sudo systemctl daemon-reload

# Enable service (start on boot)
sudo systemctl enable freeswitch-deepgram

# Start service
sudo systemctl start freeswitch-deepgram

# Check status
sudo systemctl status freeswitch-deepgram

# View logs
sudo journalctl -u freeswitch-deepgram -f

# Restart service
sudo systemctl restart freeswitch-deepgram

# Stop service
sudo systemctl stop freeswitch-deepgram
```

---

### Deepgram Feature Configuration Reference

#### All Available Channel Variables

| Variable | Values | Example | Description |
|----------|--------|---------|-------------|
| `DEEPGRAM_API_KEY` | string | `abc123...` | **Required**: Your Deepgram API key |
| `DEEPGRAM_SPEECH_MODEL` | general, meeting, phonecall, voicemail, finance, conversationalai, video, medical, custom | `phonecall` | Model optimized for use case |
| `DEEPGRAM_SPEECH_TIER` | base, enhanced, nova, nova-2 | `nova` | Model tier (accuracy vs speed) |
| `DEEPGRAM_SPEECH_CUSTOM_MODEL` | string | `my-model-id` | Custom trained model ID |
| `DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION` | true/false | `true` | Add punctuation automatically |
| `DEEPGRAM_SPEECH_PROFANITY_FILTER` | true/false | `true` | Filter profanity from output |
| `DEEPGRAM_SPEECH_REDACT` | pci, ssn, numbers (comma-separated) | `pci,ssn` | Redact sensitive information |
| `DEEPGRAM_SPEECH_DIARIZE` | true/false | `true` | Enable speaker identification |
| `DEEPGRAM_SPEECH_DIARIZE_VERSION` | string | `latest` | Diarization model version |
| `DEEPGRAM_SPEECH_NER` | true/false | `true` | Named Entity Recognition |
| `DEEPGRAM_SPEECH_ALTERNATIVES` | 1-10 | `3` | Number of transcript alternatives |
| `DEEPGRAM_SPEECH_NUMERALS` | true/false | `true` | Convert spoken numbers to digits |
| `DEEPGRAM_SPEECH_SEARCH` | comma-separated keywords | `cancel,refund` | Search for keywords |
| `DEEPGRAM_SPEECH_KEYWORDS` | word:intensity pairs | `VoIP:3,SIP:2` | Boost keyword recognition |
| `DEEPGRAM_SPEECH_REPLACE` | find:replace pairs | `um:,uh:` | Replace words in transcript |
| `DEEPGRAM_SPEECH_TAG` | string | `sales-call` | Custom tag for organization |
| `DEEPGRAM_SPEECH_ENDPOINTING` | milliseconds | `2000` | Silence duration to detect end |
| `DEEPGRAM_SPEECH_VAD_TURNOFF` | milliseconds | `1000` | VAD turnoff delay |

#### Language Codes

Common language codes for the `start` command:

- English: `en-US`, `en-GB`, `en-AU`, `en-IN`
- Spanish: `es`, `es-419`
- French: `fr`, `fr-CA`
- German: `de`
- Portuguese: `pt-BR`
- Italian: `it`
- Japanese: `ja`
- Chinese: `zh`, `zh-CN`

For complete list: [Deepgram Language Support](https://developers.deepgram.com/docs/language)

---

### Complete Example: Production Dialplan

Comprehensive dialplan with validation and error handling:

```xml
<!-- File: /usr/local/freeswitch/conf/dialplan/default/deepgram_production.xml -->
<include>
  <!-- Auto-transcribe all inbound calls with validation -->
  <extension name="deepgram_production_transcribe" continue="true">
    <condition field="destination_number" expression="^(1\d{3}|2\d{3}|3\d{3})$">

      <!-- Step 1: Validate API key is set -->
      <action application="set" data="DEEPGRAM_API_KEY=${ENV(DEEPGRAM_API_KEY)}"/>
      <action application="log" data="INFO Deepgram API Key Status: ${DEEPGRAM_API_KEY:+SET:NOT_SET}"/>

      <!-- Step 2: Configure model based on destination -->
      <action application="set" data="DEEPGRAM_SPEECH_MODEL=${cond(${destination_number} =~ /^1/ ? phonecall : ${cond(${destination_number} =~ /^2/ ? meeting : finance)})}"/>

      <!-- Step 3: Set tier based on business hours -->
      <action application="set" data="DEEPGRAM_SPEECH_TIER=${cond(${strftime(%w)} =~ /^[1-5]$/ && ${strftime(%H)} >= 09 && ${strftime(%H)} < 17 ? nova : base)}"/>

      <!-- Step 4: Enable core features -->
      <action application="set" data="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
      <action application="set" data="DEEPGRAM_SPEECH_NUMERALS=true"/>
      <action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>

      <!-- Step 5: Conditional NER for sales calls -->
      <action application="set" data="DEEPGRAM_SPEECH_NER=${cond(${destination_number} =~ /^2/ ? true : false)}"/>

      <!-- Step 6: PCI redaction for payment lines -->
      <action application="set" data="DEEPGRAM_SPEECH_REDACT=${cond(${destination_number} =~ /^3/ ? pci,ssn : none)}"/>

      <!-- Step 7: Tag calls for tracking -->
      <action application="set" data="DEEPGRAM_SPEECH_TAG=prod-${destination_number}-${strftime(%Y%m%d)}"/>

      <!-- Step 8: Start transcription with error handling -->
      <action application="uuid_deepgram_transcribe" data="start en-US interim"/>
      <action application="log" data="NOTICE Deepgram transcription started for ${uuid}"/>

    </condition>
  </extension>

  <!-- Event handler to log transcription results -->
  <extension name="deepgram_event_handler">
    <condition field="${Event-Name}" expression="^CUSTOM$"/>
    <condition field="${Event-Subclass}" expression="^deepgram_transcribe::transcription$">
      <action application="log" data="INFO Transcription: ${Event-Body}"/>
      <!-- Add your custom handling here: write to database, trigger webhook, etc. -->
    </condition>
  </extension>

</include>
```

#### Validation Script

Create validation script to test configuration before deploying:

```bash
#!/bin/bash
# File: validate-deepgram-config.sh

echo "=== Deepgram Configuration Validation ==="

# Check 1: API Key
if docker exec fs sh -c 'test -n "$DEEPGRAM_API_KEY"'; then
  echo "✅ DEEPGRAM_API_KEY is set"
else
  echo "❌ DEEPGRAM_API_KEY is NOT set"
  exit 1
fi

# Check 2: Module loaded
if docker exec fs fs_cli -x "show modules" | grep -q "mod_deepgram_transcribe"; then
  echo "✅ mod_deepgram_transcribe is loaded"
else
  echo "❌ mod_deepgram_transcribe is NOT loaded"
  exit 1
fi

# Check 3: Dialplan syntax
if docker exec fs fs_cli -x "reloadxml" | grep -q "SUCCESS"; then
  echo "✅ Dialplan XML is valid"
else
  echo "❌ Dialplan XML has errors"
  exit 1
fi

# Check 4: Network connectivity to Deepgram
if docker exec fs curl -s -o /dev/null -w "%{http_code}" https://api.deepgram.com | grep -q "200\|401"; then
  echo "✅ Can reach Deepgram API endpoints"
else
  echo "❌ Cannot reach Deepgram API endpoints"
  exit 1
fi

echo ""
echo "=== All validation checks passed! ==="
```

Run validation:

```bash
chmod +x validate-deepgram-config.sh
./validate-deepgram-config.sh
```

---

## mod_azure_transcribe

**File**: `Dockerfile.mod_azure_transcribe`
**Build Script**: `docker-build-mod-azure-transcribe.sh`
**Base Image**: `srt2011/freeswitch-mod-deepgram-transcribe:latest` (includes FreeSWITCH + mod_audio_fork + mod_deepgram_transcribe)

**Dependencies Built**:
- Microsoft Azure Cognitive Services Speech SDK (latest - auto-detected)
- ALSA sound library (libasound2)

**Build Time**:
- Intel/AMD64: **15-20 minutes** (includes Azure SDK download)
- Apple Silicon: **25-35 minutes** (with emulation)

**Usage**:
```bash
# Build the image (with custom tag)
./dockerfiles/docker-build-mod-azure-transcribe.sh srt2011/freeswitch-mod-azure-transcribe:latest

# Or use default tag
./dockerfiles/docker-build-mod-azure-transcribe.sh

# Run FreeSWITCH with Azure credentials
docker run -d --name fs \
  -p 5060:5060/udp \
  -p 8021:8021/tcp \
  -e AZURE_SUBSCRIPTION_KEY=your-azure-subscription-key \
  -e AZURE_REGION=eastus \
  srt2011/freeswitch-mod-azure-transcribe:latest

# Access fs_cli
docker exec -it fs fs_cli

# Verify all three modules loaded
docker exec -it fs fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|azure'
```

**Features**:
- ✅ Builds on mod_deepgram_transcribe image (all modules included)
- ✅ Real-time streaming transcription via Azure Speech Services
- ✅ ConversationTranscriber API for speaker identification
- ✅ AI-based speaker diarization (identifies "Guest-1", "Guest-2", etc.)
- ✅ Word-level timestamps for detailed timing information
- ✅ Sentiment analysis for emotional tone detection
- ✅ Dictation mode for improved punctuation and formatting
- ✅ Profanity filtering (masked, removed, raw modes)
- ✅ Detailed output with N-best alternatives and confidence scores
- ✅ Signal-to-noise ratio (SNR) reporting
- ✅ Speech hints for improved domain-specific recognition (mono mode)
- ✅ Configurable timeout settings
- ✅ Supports 50+ languages and dialects
- ✅ Interim and final transcription results
- ✅ Includes mod_audio_fork AND mod_deepgram_transcribe from base image
- ✅ Pre-configured example configuration files (dialplan and user directories)
- ✅ Automatic static + runtime validation during build

**Included Configuration Files**:

The Docker image includes pre-configured FreeSWITCH configuration files ready for testing:

- **`/usr/local/freeswitch/conf/dialplan/default.xml`** - Complete dialplan with Azure transcription examples
- **`/usr/local/freeswitch/conf/directory/default/1000.xml`** - User 1000 configuration
- **`/usr/local/freeswitch/conf/directory/default/1001.xml`** - User 1001 configuration
- **`/usr/local/freeswitch/conf/directory/default/1002.xml`** - User 1002 with Azure-specific variables

These files provide working examples of:
- Audio fork setup for transcription services
- Azure Speech Services integration
- Per-user multi-service configuration patterns
- Ready-to-use dialplan patterns

**Note**: You can override these by mounting your own configuration directory:
```bash
docker run -d \
  -v /path/to/your/conf:/usr/local/freeswitch/conf \
  -e AZURE_SUBSCRIPTION_KEY=your-key \
  -e AZURE_REGION=eastus \
  freeswitch-mod-azure-transcribe:latest
```

<details>
<summary><b>Manual Verification for mod_azure_transcribe</b></summary>

After building or pulling the mod_azure_transcribe image, verify it manually:


</details>


#### Step 1: Check Module File Exists

```bash
# Verify mod_azure_transcribe.so exists
docker run --rm freeswitch-mod-azure-transcribe:latest ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so

# Should show file with size around 100-200 KB
```

#### Step 2: Check Azure SDK Libraries

```bash
# Verify Azure Speech SDK is installed
docker run --rm freeswitch-mod-azure-transcribe:latest ls -lh /usr/local/lib/MicrosoftSpeechSDK/

# Should show libMicrosoft.CognitiveServices.Speech.core.so
```

#### Step 3: Check Module Dependencies

```bash
# Verify module is linked with Azure SDK
docker run --rm freeswitch-mod-azure-transcribe:latest ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so | grep -i microsoft

# Should show: libMicrosoft.CognitiveServices.Speech.core.so => /usr/local/lib/MicrosoftSpeechSDK/...
```

Check for missing dependencies:
```bash
# Run full ldd check
docker run --rm freeswitch-mod-azure-transcribe:latest ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so

# Should show NO "not found" entries
```

#### Step 4: Verify Module Loading

```bash
# Start FreeSWITCH and check module loading
docker run --rm freeswitch-mod-azure-transcribe:latest bash -c '
    echo "Starting FreeSWITCH in background..."
    /usr/local/freeswitch/bin/freeswitch -nc -nf > /dev/null 2>&1 &
    FS_PID=$!

    echo "Waiting 15 seconds for FreeSWITCH to start..."
    sleep 15

    echo ""
    echo "Checking FreeSWITCH log for mod_azure_transcribe..."
    grep -i "mod_azure_transcribe" /usr/local/freeswitch/log/freeswitch.log || echo "❌ mod_azure_transcribe NOT found in logs"

    kill $FS_PID 2>/dev/null || true
'
```

Expected output should include:
```
[NOTICE] mod_azure_transcribe.c:XXX Successfully Loaded [mod_azure_transcribe]
[CONSOLE] switch_loadable_module.c:XXX Adding API Function 'azure_transcribe'
```

#### Step 5: Verify All Three Modules

```bash
# Start container and check all modules
docker run -d --name fs-test freeswitch-mod-azure-transcribe:latest
sleep 30

# Check all three modules are loaded
docker exec fs-test fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|azure'

# Should show all three:
# api,uuid_audio_fork,mod_audio_fork,...
# api,uuid_deepgram_transcribe,mod_deepgram_transcribe,...
# api,azure_transcribe,mod_azure_transcribe,...

# Cleanup
docker rm -f fs-test
```

#### Verification Summary

If all checks pass:
```
✅ mod_azure_transcribe is installed and loads successfully!
✅ Bonus: mod_audio_fork is also available!
✅ Bonus: mod_deepgram_transcribe is also available!
```

**To start FreeSWITCH with mod_azure_transcribe**:
```bash
docker run --rm -it \
  -e AZURE_SUBSCRIPTION_KEY=your-subscription-key \
  -e AZURE_REGION=eastus \
  freeswitch-mod-azure-transcribe:latest \
  freeswitch -nc -nf
```

**Example API Usage**:
```bash
# In fs_cli or via ESL

# Basic configuration
uuid_setvar <call-uuid> AZURE_SUBSCRIPTION_KEY your-subscription-key
uuid_setvar <call-uuid> AZURE_REGION eastus
uuid_setvar <call-uuid> AZURE_USE_OUTPUT_FORMAT_DETAILED true
uuid_setvar <call-uuid> AZURE_PROFANITY_OPTION masked

# Advanced features
uuid_setvar <call-uuid> AZURE_WORD_LEVEL_TIMESTAMPS true
uuid_setvar <call-uuid> AZURE_SENTIMENT_ANALYSIS true
uuid_setvar <call-uuid> AZURE_DICTATION_MODE true

# AI-based speaker diarization (uses ConversationTranscriber)
# Note: Azure uses AI to identify speakers (Guest-1, Guest-2), not channel separation
uuid_setvar <call-uuid> AZURE_DIARIZE_INTERIM_RESULTS true
uuid_setvar <call-uuid> AZURE_DIARIZATION_SPEAKER_COUNT 2

# Start transcription with interim results (mono)
uuid_azure_transcribe <call-uuid> start en-US interim

# Start transcription with ConversationTranscriber (stereo mode for speaker identification)
uuid_azure_transcribe <call-uuid> start en-US interim stereo

# Stop transcription
uuid_azure_transcribe <call-uuid> stop
```

For full API documentation, see: `modules/mod_azure_transcribe/README.md`

---

## 5. mod_aws_transcribe

**File**: `Dockerfile.mod_aws_transcribe`
**Build Script**: `docker-build-mod-aws-transcribe.sh`
**Base Image**: `srt2011/freeswitch-base:latest` (FreeSWITCH 1.10.11)

**Dependencies Built**:
- AWS SDK C++ v1.11.345 (core and transcribestreaming)
- AWS C Common, Event Stream, and Checksums libraries

**Build Time**:
- Intel/AMD64: **25-35 minutes** (includes AWS SDK C++ build from source)
- Apple Silicon: **40-50 minutes** (with emulation)

**Usage**:
```bash
# Build the image (with custom tag and AWS SDK version)
./dockerfiles/docker-build-mod-aws-transcribe.sh \\
  srt2011/freeswitch-mod-aws-transcribe:latest \\
  1.11.345

# Or use defaults
./dockerfiles/docker-build-mod-aws-transcribe.sh

# Run FreeSWITCH with AWS credentials
docker run -d --name fs \\
  -p 5060:5060/udp \\
  -p 8021:8021/tcp \\
  -e AWS_ACCESS_KEY_ID=your-aws-access-key \\
  -e AWS_SECRET_ACCESS_KEY=your-aws-secret-key \\
  -e AWS_REGION=us-east-1 \\
  srt2011/freeswitch-mod-aws-transcribe:latest

# Access fs_cli
docker exec -it fs fs_cli

# Verify module loaded
docker exec -it fs fs_cli -x 'show modules' | grep aws_transcribe
```

**Features**:
- ✅ Real-time streaming transcription via AWS Transcribe Streaming API
- ✅ **Two speaker identification methods:**
  - **Speaker Diarization** (AI-based): Detects up to 10 speakers (spk_0, spk_1...), 85-95% accuracy
  - **Channel Identification** (telephony-optimized): 100% accurate agent/customer separation via stereo channels
- ✅ Support for 30+ languages and language identification
- ✅ Interim and final transcription results
- ✅ Custom vocabulary support for domain-specific terminology
- ✅ Vocabulary filtering for profanity or sensitive words
- ✅ Medical and custom language models
- ✅ Word-level timestamps and confidence scores
- ✅ Built on stable freeswitch-base image
- ✅ Automatic static + runtime validation during build
- ✅ Pre-configured example configuration files

**Included Configuration Files**:

The Docker image includes pre-configured FreeSWITCH configuration files ready for testing:

- **`/usr/local/freeswitch/conf/dialplan/default.xml`** - Complete dialplan with examples
- **`/usr/local/freeswitch/conf/directory/default/1000.xml`** - User 1000 configuration
- **`/usr/local/freeswitch/conf/directory/default/1001.xml`** - User 1001 configuration
- **`/usr/local/freeswitch/conf/directory/default/1002.xml`** - User 1002 configuration

**Authentication Methods**:

1. **Environment variables** (recommended for Docker):
```bash
docker run -d \\
  -e AWS_ACCESS_KEY_ID=your-key \\
  -e AWS_SECRET_ACCESS_KEY=your-secret \\
  -e AWS_REGION=us-east-1 \\
  freeswitch-mod-aws-transcribe:latest
```

2. **Channel variables** (per-call):
```javascript
// Via drachtio-fsmrf
await ep.set({
  AWS_ACCESS_KEY_ID: 'your-key',
  AWS_SECRET_ACCESS_KEY: 'your-secret',
  AWS_REGION: 'us-east-1'
});
ep.api('aws_transcribe', `${ep.uuid} start en-US interim`);
```

3. **IAM instance role** (when running on EC2):
   - No credentials needed, uses EC2 instance metadata

<details>
<summary><b>Manual Verification for mod_aws_transcribe</b></summary>

After building or pulling the mod_aws_transcribe image, verify it manually:

#### Step 1: Check Module File Exists

```bash
# Verify mod_aws_transcribe.so exists
docker run --rm freeswitch-mod-aws-transcribe:latest \\
  ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so

# Should show file with size around 200-400 KB
```

#### Step 2: Check AWS SDK Libraries

```bash
# Verify AWS SDK libraries are installed
docker run --rm freeswitch-mod-aws-transcribe:latest \\
  ls -lh /usr/local/lib/libaws-cpp-sdk-*.so

# Should list:
# - libaws-cpp-sdk-transcribestreaming.so
# - libaws-cpp-sdk-core.so
# - libaws-c-event-stream.so
# - libaws-checksums.so
# - libaws-c-common.so
```

#### Step 3: Check Module Dependencies

```bash
# Check module dependencies are satisfied
docker run --rm freeswitch-mod-aws-transcribe:latest \\
  ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so

# Should show all AWS libraries linked correctly with no "not found" errors
```

#### Step 4: Runtime Test

```bash
# Start FreeSWITCH
docker run -d --name fs-aws-test \\
  -p 5060:5060/udp \\
  -p 8021:8021/tcp \\
  -e AWS_ACCESS_KEY_ID=test \\
  -e AWS_SECRET_ACCESS_KEY=test \\
  -e AWS_REGION=us-east-1 \\
  freeswitch-mod-aws-transcribe:latest

# Wait for startup
sleep 30

# Verify module loaded
docker exec fs-aws-test fs_cli -x "show modules" | grep aws_transcribe

# Expected output:
# api,aws_transcribe,mod_aws_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so

# Cleanup
docker rm -f fs-aws-test
```

</details>

### API Commands

```bash
# Start transcription (mono mode)
aws_transcribe <uuid> start en-US interim

# Start with speaker diarization (AI-based, detects multiple speakers)
uuid_setvar <uuid> AWS_SHOW_SPEAKER_LABEL true
aws_transcribe <uuid> start en-US interim

# Start with channel identification (stereo, perfect agent/customer separation)
uuid_setvar <uuid> AWS_ENABLE_CHANNEL_IDENTIFICATION true
uuid_setvar <uuid> AWS_NUMBER_OF_CHANNELS 2
aws_transcribe <uuid> start en-US interim

# Start with custom vocabulary
uuid_setvar <uuid> AWS_VOCABULARY_NAME my-custom-vocab
aws_transcribe <uuid> start en-US interim

# Start with vocabulary filter (profanity filtering)
uuid_setvar <uuid> AWS_VOCABULARY_FILTER_NAME profanity-filter
uuid_setvar <uuid> AWS_VOCABULARY_FILTER_METHOD mask
aws_transcribe <uuid> start en-US interim

# Stop transcription
aws_transcribe <uuid> stop
```

**Speaker Identification:** mod_aws_transcribe supports two methods:
- **Speaker Diarization** (`AWS_SHOW_SPEAKER_LABEL`): AI detects speakers (spk_0, spk_1...), works with mono audio
- **Channel Identification** (`AWS_ENABLE_CHANNEL_IDENTIFICATION`): Uses stereo channels (ch_0=agent, ch_1=customer), 100% accurate

See `modules/mod_aws_transcribe/README.md` for detailed speaker identification guide with cost comparison and use cases.

### Build Issues and Solutions

During development, we encountered and resolved several critical build issues:

#### 1. cJSON Header Conflict ⚠️ CRITICAL

**Problem:** Duplicate symbol errors during linking
```
duplicate symbol '_cJSON_CreateObject' in:
    mod_aws_transcribe.o
    libaws-cpp-sdk-core.so (via bundled cJSON)
```

**Root Cause:**
- AWS SDK C++ v1.11.345 bundles its own cJSON in `/usr/local/include/aws/core/external/cjson/cJSON.h`
- mod_aws_transcribe uses FreeSWITCH's system cJSON library
- Without header guards, both symbols conflict during linking

**Solution Applied** (Dockerfile lines 101-123):
```dockerfile
# Add header guards to AWS SDK's bundled cJSON
RUN sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' \
        /usr/local/include/aws/core/external/cjson/cJSON.h \
    && echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h
```

This fix wraps AWS SDK's cJSON with additional header guards to prevent conflicts.

#### 2. Missing libspeexdsp Dependency

**Problem:** Compilation error
```
fatal error: speex/speex_resampler.h: No such file or directory
```

**Root Cause:**
- mod_aws_transcribe uses `speex_resampler` for automatic audio resampling (8kHz → 16kHz)
- Required for converting various codec sample rates to AWS Transcribe's 16kHz requirement
- `libspeexdsp-dev` was missing from builder stage

**Solution Applied** (Dockerfile line 61):
```dockerfile
# Builder stage dependencies
RUN apt-get install -y libspeexdsp-dev  # Provides speex/speex_resampler.h

# Runtime stage dependencies
RUN apt-get install -y libspeexdsp1     # Provides libspeexdsp.so.1
```

#### 3. Missing AWS Common Runtime (CRT) Libraries

**Problem:** Runtime dependency errors
```
libaws-crt-cpp.so => not found
libs2n.so.1 => not found
```

**Root Cause:**
- AWS SDK 1.11.345 depends on AWS Common Runtime libraries
- Wildcard pattern `libaws-c-*.so*` didn't match `libaws-crt-cpp` or `libs2n`
- Libraries were built but not copied to runtime stage

**Solution Applied** (Dockerfile lines 234, 236):
```dockerfile
# Copy additional AWS CRT libraries to runtime stage
COPY --from=builder /usr/local/lib/libaws-crt-cpp.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libs2n.so* /usr/local/lib/
```

**Why This Happened:**
- `libaws-c-*.so*` matches: `libaws-c-common`, `libaws-c-event-stream`, etc.
- Does NOT match: `libaws-crt-cpp` (has "crt" in middle, not "c-" prefix)
- Does NOT match: `libs2n` (doesn't start with "libaws")

#### 4. Undefined ARG Variable Warning

**Problem:** Docker build warning
```
UndefinedVar: Usage of undefined variable '$AWS_SDK_CPP_VERSION' (line 337)
```

**Root Cause:**
- ARG variables are scoped to the build stage where they're defined
- Runtime stage used `${AWS_SDK_CPP_VERSION}` in a LABEL but ARG wasn't re-declared

**Solution Applied** (Dockerfile line 218):
```dockerfile
# Runtime Stage
FROM ${BASE_IMAGE} AS runtime

# Re-declare ARG for runtime stage (ARGs don't persist across stages)
ARG AWS_SDK_CPP_VERSION=1.11.345
```

### AWS SDK Version Strategy

**Current Default:** `1.11.345` (tested and stable)

**Verified Compatible Versions:**
- ✅ **1.11.200+** - All versions from 1.11.200 onwards work
- ✅ **1.11.345** - Default in Docker builds (recommended)
- ✅ **1.11.694** - Latest as of 2025-01 (upgrade available)

**To use a different version:**
```bash
# Via build script
./dockerfiles/docker-build-mod-aws-transcribe.sh \
  my-image:latest \
  1.11.694

# Via docker build
docker build \
  --build-arg AWS_SDK_CPP_VERSION=1.11.694 \
  -f dockerfiles/Dockerfile.mod_aws_transcribe \
  -t my-image:latest .
```

**Recommendation:** Use 1.11.345 for stability. Upgrade to newer versions only if you need specific features or bug fixes.

### Complete Dependency List

**Builder Stage:**
- `build-essential` - gcc, g++, make
- `git` - Clone AWS SDK
- `cmake` - Build AWS SDK
- `libcurl4-openssl-dev` - HTTP client for AWS
- `libssl-dev` - TLS/SSL support
- `uuid-dev` - UUID generation
- `zlib1g-dev` - Compression
- `libpulse-dev` - PulseAudio support (AWS SDK)
- `libspeexdsp-dev` - Audio resampling (NEW - added for this module)

**Runtime Stage:**
- `libcurl4` - HTTP client runtime
- `libssl1.1` - TLS/SSL runtime
- `zlib1g` - Compression runtime
- `libpulse0` - PulseAudio runtime
- `libspeexdsp1` - Audio resampling runtime (NEW - added for this module)

**AWS Libraries (copied from builder):**
- `libaws-cpp-sdk-transcribestreaming.so` - AWS Transcribe Streaming API
- `libaws-cpp-sdk-core.so` - AWS SDK core
- `libaws-c-event-stream.so` - Event stream handling
- `libaws-checksums.so` - Data checksums
- `libaws-c-common.so` - Common AWS utilities
- `libaws-crt-cpp.so` - AWS Common Runtime C++ (NEW - explicit copy needed)
- `libs2n.so.1` - AWS s2n TLS library (NEW - explicit copy needed)

### Supported Languages (Examples)

- **English**: en-US, en-GB, en-AU, en-IN
- **Spanish**: es-US, es-ES
- **French**: fr-FR, fr-CA
- **German**: de-DE
- **Portuguese**: pt-BR, pt-PT
- **Japanese**: ja-JP
- **Korean**: ko-KR
- **Chinese**: zh-CN
- **Arabic**: ar-AE, ar-SA
- **Hindi**: hi-IN
- And 20+ more languages...

For the complete list, see: [AWS Transcribe Supported Languages](https://docs.aws.amazon.com/transcribe/latest/dg/supported-languages.html)

For full API documentation, see: `modules/mod_aws_transcribe/README.md`

---

## Build Process

### mod_audio_fork (Base Image Approach - RECOMMENDED)

**Uses pre-built production FreeSWITCH as base** - Much faster!

#### Stage 1: Builder - Install Build Dependencies
From `srt2011/freeswitch-base:latest`:
- Install: git, cmake, build-essential, libssl-dev, libspeexdsp-dev
- Base image already contains FreeSWITCH with all configurations

#### Stage 2: Builder - Build libwebsockets
- Clone and build libwebsockets 4.3.3 from source
- Only dependency needed for mod_audio_fork

#### Stage 3: Builder - Compile mod_audio_fork
- Separate C and C++ compilation:
  - `gcc` for mod_audio_fork.c → mod_audio_fork.o
  - `g++` for lws_glue.cpp → lws_glue.o
  - `g++` for audio_pipe.cpp → audio_pipe.o
- Link all object files with g++ shared

#### Stage 4: Builder - Static Validation
- Check module file exists
- Verify dependencies with ldd
- Validate libwebsockets linkage

#### Stage 5: Builder - Update Configuration
- Add `<load module="mod_audio_fork"/>` to modules.conf.xml

#### Stage 6: Builder - Runtime Validation
- Start FreeSWITCH in background
- Verify mod_audio_fork loads without errors
- **Build fails if validation fails**

#### Stage 7: Runtime Image
From `srt2011/freeswitch-base:latest`:
- Copy libwebsockets library
- Copy mod_audio_fork.so
- Copy updated modules.conf.xml
- **Inherit ENTRYPOINT/CMD from base image**
- Behaves exactly like base + mod_audio_fork

**Total Build Time**: 10-15 minutes (vs 90+ minutes for full build!)

---

### Legacy Approach (Full Build from Source)

**Note**: This approach is still documented for other modules that may need it.

#### Stage 1-3: Build FreeSWITCH from Source
(Full FreeSWITCH build with all dependencies)

#### Stage 4-6: Build Module Dependencies
(Module-specific dependencies)

#### Stage 7-8: Validation and Runtime Image
(Similar to base image approach)

## Validation Points

Each Dockerfile includes multiple validation points:

### 1. Static Build-time Validation (Stage 6)
   - **Point 1**: Module file exists
   - **Point 2**: Check module dependencies with ldd
   - **Point 3**: Verify module-specific library linkage (e.g., libwebsockets)
   - **Point 4**: Check for missing dependencies

### 2. Runtime Validation During Build ⭐ **NEW** (Stage 7)
   - **Point 5**: Verify mod_audio_fork appears in FreeSWITCH logs
   - **Point 6**: Check for module loading errors (error/fail/cannot/unable)
   - **Point 7**: Check for critical FreeSWITCH errors (segfault/core dump/fatal)
   - **Full FreeSWITCH startup log** printed for debugging
   - **Build fails** if any validation point fails!

### 3. Container Runtime Validation Script (`/validate-module.sh`)
   - Available in final container (Stage 8)
   - Checks module file exists
   - Runs ldd to verify dependencies
   - Verifies libwebsockets linkage
   - Checks for missing dependencies
   - Can be run manually after container starts

## Differences from Main Production Dockerfile

The individual module Dockerfiles build **PRODUCTION-IDENTICAL FreeSWITCH** but skip heavy dependencies not needed by the specific module:

### What's IDENTICAL to Production ✅

✅ **All FreeSWITCH Patches Applied**
   - switch_core_media.c.patch
   - switch_rtp.c.patch
   - mod_avmd.c.patch
   - mod_httapi.c.patch

✅ **Production Custom Files Applied**
   - switch_event.c
   - mod_conference.h + conference_api.c
   - Note: configure.ac.extra/Makefile.am.extra skipped (designed for production with extra modules)

✅ **Production Configure Flags**
   - `--enable-tcmalloc=yes` (production performance)
   - `--with-lws=yes` (for mod_audio_fork)
   - `--with-extra=yes` (production features)
   - `--with-aws=no` (only difference - we don't build AWS SDK)

✅ **Production Configuration**
   - Codec preferences applied (PCMU,PCMA,OPUS,G722)
   - Same base packages as production
   - Same FreeSWITCH version (v1.10.11)

✅ **FreeSWITCH Core Dependencies**
   - spandsp (v0d2e6ac)
   - sofia-sip (v1.13.17)
   - libfvad

### What We Skip to Save Time ⚡

❌ **Heavy dependencies not needed by specific module:**
   - gRPC + grpc-googleapis (~1 hour build) - only needed by mod_google_transcribe
   - AWS SDK C++ + aws-c-common (~1-2 hours build) - only needed by mod_aws_transcribe
   - Azure Speech SDK - only needed by mod_azure_transcribe

❌ **Unrelated modules:**
   - Only builds 1 module instead of all 6 transcription modules
   - Faster module.conf with only essential modules

### Why This Approach?

1. **Production-identical testing**: Tests against EXACT production FreeSWITCH build
2. **Faster iteration**: 15-25 min vs 90-150 min builds (saves 2-3 hours)
3. **Same stability**: All production patches ensure production compatibility
4. **Clear dependencies**: See exactly what each module needs
5. **Fail-safe**: If it works here, it works in production

### When to Use Which Dockerfile?

**Individual Module Dockerfiles** (`dockerfiles/Dockerfile.mod_*`):
- Development and testing of a specific module
- Quick validation of module changes
- Debugging module-specific issues
- CI/CD module-level testing

**Main Production Dockerfile** (`Dockerfile`):
- Production deployments
- Full feature set needed
- All 6 modules together
- Custom patches and optimizations required

### Configuration Comparison

| Feature | Individual Module | Main Production |
|---------|------------------|-----------------|
| **FreeSWITCH Source** | Production (all patches applied) ✅ | Production (all patches applied) ✅ |
| **Configure Flags** | Production flags + module-specific | Production flags (all) |
| **Modules Built** | 1 module + essentials (~7 total) | All 6 transcription modules + full suite |
| **Custom Patches** | ALL applied ✅ | ALL applied ✅ |
| **Custom Files** | ALL applied ✅ | ALL applied ✅ |
| **Configuration** | Production codec prefs ✅ | Production codec prefs ✅ |
| **Heavy Dependencies** | Only module-specific (libwebsockets) | All (gRPC, AWS SDK, Azure SDK) |
| **Build Time** | 15-25 min (75% faster) | 90-150 min |
| **Image Size** | ~200-300 MB | ~500-800 MB |
| **Use Case** | Fast testing of production build | Full production deployment |
| **Compatibility** | 100% production-identical | Production build |

## Adding New Modules

To add a new module for individual testing:

1. **Create Dockerfile** (`Dockerfile.mod_<module_name>`):
```dockerfile
# Follow the pattern in Dockerfile.mod_audio_fork
# Adjust dependencies based on module requirements
```

2. **Create Build Script** (`docker-build-mod-<module_name>.sh`):
```bash
#!/bin/bash
# Copy and adapt docker-build-mod-audio-fork.sh
# Update module name and dependencies
```

3. **Define Dependencies**:
   - Read module's README.md
   - Identify required libraries
   - Add to base dependencies stage
   - Build from source if not in apt repositories

4. **Create FreeSWITCH Config**:
   - Minimal modules.conf with only required modules
   - Enable target module in modules.conf.xml
   - Configure any module-specific settings

5. **Add Validation**:
   - Verify module-specific dependencies
   - Check module loads successfully
   - Test basic module functionality if possible

## Directory Structure

```
dockerfiles/
├── README.md                           # This file
├── Dockerfile.mod_audio_fork          # mod_audio_fork individual testing
├── docker-build-mod-audio-fork.sh     # Build script for mod_audio_fork
└── ...                                # Additional modules
```

## Environment Variables

Build scripts read versions from the main `.env` file in the repository root:

- `CMAKE_VERSION` - CMake version to build
- `LIBWEBSOCKETS_VERSION` - libwebsockets version (for mod_audio_fork)
- `FREESWITCH_VERSION` - FreeSWITCH version
- `SPANDSP_VERSION` - spandsp library version
- `SOFIA_VERSION` - sofia-sip library version

Module-specific dependencies are added as needed.

## Comparison: Individual vs Full Build

| Aspect | Individual Module | Full Build |
|--------|------------------|------------|
| Build Time | 15-25 min | 90-150 min |
| Docker Image Size | ~200-300 MB | ~500-800 MB |
| Modules Built | 1 module | 6 modules |
| Dependencies | Minimal | All dependencies |
| Use Case | Development, debugging, testing | Production deployment |
| Startup Time | Fast | Slower |
| Configuration | Minimal | Complete |

## Testing Workflow

1. **Development**:
   ```bash
   # Make changes to module source
   vim modules/mod_audio_fork/mod_audio_fork.c

   # Quick rebuild and test
   ./dockerfiles/docker-build-mod-audio-fork.sh freeswitch-test:dev
   docker run --rm freeswitch-test:dev
   ```

2. **Debugging**:
   ```bash
   # Build with debug symbols (modify Dockerfile to add -g flag)
   ./dockerfiles/docker-build-mod-audio-fork.sh

   # Get shell and inspect
   docker run --rm -it freeswitch-mod-audio-fork:latest bash
   gdb /usr/local/freeswitch/bin/freeswitch
   ```

3. **CI/CD Integration**:
   ```bash
   # Quick validation in CI pipeline
   ./dockerfiles/docker-build-mod-audio-fork.sh ci-test:$BUILD_ID
   docker run --rm ci-test:$BUILD_ID /validate-module.sh
   ```

## Platform Support

### Linux (Native)
- Fast builds
- Full functionality
- Production recommended

### macOS (Intel)
- Native amd64 builds
- Good performance
- Development recommended

### macOS (Apple Silicon)
- Requires emulation (linux/amd64)
- Slower builds (2x time)
- Works via Rosetta
- GitHub Codespaces recommended for faster builds

### GitHub Codespaces
- Linux environment in cloud
- Fast builds
- Recommended for Apple Silicon users
- See main INSTALL.md for setup

## Troubleshooting

### Build Failures

**Missing dependencies**:
```bash
# Check Dockerfile base dependencies stage
# Ensure all required -dev packages are installed
```

**Module not found after build**:
```bash
# Check modules.conf in Dockerfile
# Ensure module is listed: applications/mod_audio_fork
```

**Runtime dependency errors**:
```bash
# Check final stage runtime dependencies
# Run ldd in builder stage to see what's needed
docker run --rm <image> ldd /usr/local/freeswitch/mod/mod_audio_fork.so
```

### Module Loading Failures

**Module loads but crashes**:
```bash
# Check FreeSWITCH logs
docker run --rm -it <image> freeswitch -nc -nf
# Look for error messages related to the module
```

**Missing configuration**:
```bash
# Verify modules.conf.xml exists and is correct
docker run --rm <image> cat /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml
```

---

## Testing and Deployment

Once you've built the FreeSWITCH base image, you can test it in two ways:

### Option 1: Deploy to Docker Hub → Test on MacBook (Recommended)

**Best for**: Full testing with real audio, SIP clients, and production-like environment

```bash
# 1. Push to Docker Hub (in development environment)
./dockerfiles/push-to-dockerhub.sh your-username

# 2. Pull and run on MacBook
./dockerfiles/run-on-macbook.sh your-username
```

**Features**:
- ✅ Full audio testing with real SIP clients
- ✅ Support for all SIP transports (UDP/TCP/TLS)
- ✅ Test with Zoiper, Linphone, or any SIP client
- ✅ Production-like networking
- ✅ Test calling between extensions 1000 and 1001

See **DOCKER_HUB_DEPLOYMENT.md** for detailed step-by-step instructions.

### Option 2: Manual Validation (Recommended for Learning)

**Best for**: Understanding FreeSWITCH components, debugging, learning

#### Step 1: Start FreeSWITCH Container

```bash
# Start with all ports mapped
docker run -d \
    --name freeswitch-test \
    --platform linux/amd64 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    freeswitch-base:1.10.11
```

#### Step 2: Wait for FreeSWITCH to Start

```bash
# Wait 30 seconds for startup
sleep 30
```

#### Step 3: Verify Container is Running

```bash
# Check container status
docker ps | grep freeswitch-test

# Should show STATUS as "Up X seconds"
```

If container is not running:
```bash
# Check logs for errors
docker logs freeswitch-test
```

#### Step 4: Check FreeSWITCH Process

```bash
# Verify FreeSWITCH process is running
docker exec freeswitch-test pgrep -f freeswitch

# Should return a process ID (e.g., 1)
```

#### Step 5: Check FreeSWITCH Log File

```bash
# Verify log file exists
docker exec freeswitch-test test -f /usr/local/freeswitch/log/freeswitch.log && echo "✅ Log file exists"

# Check for startup errors (ignore NORMAL_CLEARING and switch_odbc.c)
docker exec freeswitch-test grep -i "error" /usr/local/freeswitch/log/freeswitch.log | grep -v "NORMAL_CLEARING" | grep -v "switch_odbc.c"

# If no output or only expected errors, you're good
```

#### Step 6: Test fs_cli Connectivity

```bash
# Connect with fs_cli and get status
docker exec freeswitch-test /usr/local/freeswitch/bin/fs_cli -x "status"

# Should show FreeSWITCH version, uptime, and session info
```

If connection fails:
```bash
# Check event socket configuration
docker exec freeswitch-test cat /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml
```

#### Step 7: Check Loaded Modules

```bash
# Count loaded modules
docker exec freeswitch-test /usr/local/freeswitch/bin/fs_cli -x "show modules" | grep -c "^mod_"

# Should show 50+ modules (production builds have 100+)
```

List first 20 modules:
```bash
docker exec freeswitch-test /usr/local/freeswitch/bin/fs_cli -x "show modules" | head -20
```

#### Step 8: Check SIP Profiles

```bash
# Verify SIP profiles are running
docker exec freeswitch-test /usr/local/freeswitch/bin/fs_cli -x "sofia status"

# Should show "internal" and "external" profiles as RUNNING
```

#### Step 9: Verify Extensions 1000 and 1001

```bash
# Check extension 1000 configuration
docker exec freeswitch-test test -f /usr/local/freeswitch/conf/directory/default/1000.xml && echo "✅ Extension 1000 exists"

# Check extension 1001 configuration
docker exec freeswitch-test test -f /usr/local/freeswitch/conf/directory/default/1001.xml && echo "✅ Extension 1001 exists"
```

#### Step 10: Test System Utilities

```bash
# Test ps command
docker exec freeswitch-test ps aux > /dev/null && echo "✅ ps works"

# Test netstat
docker exec freeswitch-test netstat -an > /dev/null && echo "✅ netstat works"

# Test ping
docker exec freeswitch-test ping -c 1 8.8.8.8 > /dev/null && echo "✅ ping works"

# Test vim
docker exec freeswitch-test which vim > /dev/null && echo "✅ vim installed"

# Test curl
docker exec freeswitch-test which curl > /dev/null && echo "✅ curl installed"
```

#### Step 11: Verify Critical Modules

```bash
# Check critical modules for SIP and WebRTC
for module in mod_sofia mod_event_socket mod_conference mod_dptools mod_dialplan_xml mod_opus mod_vp8 mod_h264; do
    echo -n "Checking $module: "
    docker exec freeswitch-test /usr/local/freeswitch/bin/fs_cli -x "show modules" | grep -q "^$module" && echo "✅" || echo "❌"
done
```

#### Validation Summary

If all checks pass:
```
✅ FreeSWITCH base image validation complete!
```

**Next steps**:
1. Register SIP clients to extensions 1000 and 1001
2. Test calling between extensions
3. Test WebRTC connectivity

**Cleanup**:
```bash
# Stop and remove test container
docker stop freeswitch-test
docker rm freeswitch-test
```

**Note**: This manual validation helps you understand each component. For quick health checks in CI/CD, consider scripting these steps.

---

## Resources
---

# Appendices

> **The following appendices contain comprehensive, detailed guides extracted from the original documentation files.**

---

# Appendix A: Complete FreeSWITCH Installation Guide

This section provides comprehensive details on building FreeSWITCH from source, including all dependencies, common errors, and solutions.

# FreeSWITCH 1.10.11 Installation Guide - Build from Source

This document provides a comprehensive guide to building FreeSWITCH 1.10.11 from source in a Docker container, including all dependencies, common errors, and their solutions.

## Table of Contents

- [Overview](#overview)
- [Build Dependencies](#build-dependencies)
- [Runtime Dependencies](#runtime-dependencies)
- [Multi-Stage Build Process](#multi-stage-build-process)
- [Common Build Errors and Solutions](#common-build-errors-and-solutions)
- [Module Configuration](#module-configuration)
- [Build Process](#build-process)
- [Verification](#verification)

---

## Overview

FreeSWITCH 1.10.11 is a production-ready telephony platform released in August 2019. Building from source ensures you get all modules and can customize the build for your specific needs.

**Key Features:**
- Full SIP and WebRTC support
- 100+ modules for telephony, codecs, applications
- Event Socket for external control
- Database support (PostgreSQL, ODBC, SQLite)
- Video support (VP8, VP9, H.264)
- Audio codecs (Opus, G.711, G.722, etc.)
- Lua scripting for dialplan logic

**Build Time:**
- Native x86_64: 30-45 minutes (with 8-16 CPU cores)
- Apple Silicon (emulated): 60-90 minutes

---

## Build Dependencies

### Essential Build Tools

| Package | Purpose | Required For |
|---------|---------|--------------|
| `build-essential` | GCC, G++, make, and other compilation tools | All C/C++ compilation |
| `cmake` | Build system generator | Some modules and dependencies |
| `autoconf` | Generate configure scripts | FreeSWITCH and dependencies |
| `automake` | Makefile generation | FreeSWITCH and dependencies |
| `libtool` | Shared library support | Library creation |
| `libtool-bin` | Libtool executable | **Critical**: Provides `/usr/bin/libtool` binary |
| `pkg-config` | Library dependency resolution | Finding installed libraries |
| `nasm` | x86 assembler | **Critical**: libvpx video codec optimization |
| `git` | Version control | Cloning source repositories |
| `wget` | File downloads | Downloading assets |
| `ca-certificates` | SSL certificates | HTTPS connections |

### FreeSWITCH Core Dependencies

| Package | Purpose | Component |
|---------|---------|-----------|
| `libssl-dev` | OpenSSL development headers | TLS/SSL encryption |
| `libcurl4-openssl-dev` | HTTP client library | API calls, webhooks |
| `libpcre3-dev` | Perl-compatible regex | Pattern matching in dialplan |
| `libspeex1` | Speex codec runtime | Audio codec |
| `libspeexdsp-dev` | Speex DSP library | Echo cancellation, noise suppression |
| `libedit-dev` | Command-line editing | fs_cli interactive features |
| `libtiff-dev` | TIFF image support | Fax (T.38) support |
| `libldns-dev` | DNS resolution | SIP DNS SRV lookups |
| `uuid-dev` | UUID generation | **Critical**: Unique call IDs, UUIDs |

### Audio/Video Codecs

| Package | Purpose | Codec/Feature |
|---------|---------|---------------|
| `libopus-dev` | Opus codec | High-quality VoIP audio (WebRTC) |
| `libsndfile1-dev` | Audio file I/O | WAV, FLAC, OGG file support |
| `libshout3-dev` | Icecast streaming | Audio streaming |
| `libmpg123-dev` | MP3 decoder | MP3 playback |
| `libmp3lame-dev` | MP3 encoder | MP3 recording |
| `libavformat-dev` | FFmpeg container formats | Video container support |
| `libswscale-dev` | FFmpeg video scaling | Video format conversion |

### Database Support

| Package | Purpose | Database |
|---------|---------|----------|
| `libsqlite3-dev` | SQLite development | Default database (call logs, voicemail) |
| `libpq-dev` | PostgreSQL client | PostgreSQL backend |
| `unixodbc-dev` | ODBC driver manager | **Critical**: Generic database connectivity |

### SIP and WebRTC

| Package | Purpose | Protocol |
|---------|---------|----------|
| `libsofia-sip-ua-dev` | SIP stack (packaged version) | SIP protocol (supplementary) |
| `libsrtp2-dev` | Secure RTP | WebRTC media encryption |

**Note**: We build sofia-sip v1.13.17 from source for better compatibility with FreeSWITCH 1.10.11.

### Additional Dependencies

| Package | Purpose | Feature |
|---------|---------|---------|
| `libxml2-dev` | XML parsing | Configuration files, dialplan XML |
| `liblua5.2-dev` | Lua scripting | mod_lua (recommended for dialplan) |
| `libgoogle-perftools-dev` | tcmalloc allocator | Performance optimization |
| `python3` | Python interpreter | Build scripts |
| `python-is-python3` | Python symlink | Legacy script compatibility |
| `zlib1g-dev` | Compression library | Data compression |
| `libjpeg-dev` | JPEG image support | Video snapshots, caller ID photos |

### Dependencies Built from Source

These are built in separate Docker stages before FreeSWITCH:

#### 1. spandsp (v0d2e6ac)

**Purpose**: DSP library for telephony applications

**Features**:
- T.38 fax support
- DTMF tone detection/generation
- Echo cancellation
- Modem emulation

**Why from source**: Debian package version may be outdated or missing features needed by FreeSWITCH 1.10.11

**Build process**:
```dockerfile
RUN git clone https://github.com/freeswitch/spandsp.git \
    && cd spandsp \
    && git checkout 0d2e6ac \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

#### 2. sofia-sip (v1.13.17)

**Purpose**: SIP protocol stack

**Features**:
- SIP message parsing/generation
- Transaction layer
- Dialog management
- SDP (Session Description Protocol)

**Why from source**: FreeSWITCH requires specific version for optimal compatibility

**Build process**:
```dockerfile
RUN git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git \
    && cd sofia-sip \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

---

## Runtime Dependencies

Runtime dependencies are the shared libraries needed to **run** FreeSWITCH (without development headers):

### Core Runtime Libraries

| Package | Build Dependency | Purpose |
|---------|------------------|---------|
| `libssl1.1` | libssl-dev | OpenSSL runtime |
| `libcurl4` | libcurl4-openssl-dev | HTTP client runtime |
| `libpcre3` | libpcre3-dev | Regex runtime |
| `libspeex1` | libspeex1 | Speex codec runtime |
| `libspeexdsp1` | libspeexdsp-dev | Speex DSP runtime |
| `libedit2` | libedit-dev | Command-line editing |
| `libtiff5` | libtiff-dev | TIFF runtime |
| `libldns3` | libldns-dev | DNS runtime |
| `libuuid1` | uuid-dev | **Critical**: UUID runtime library |

### Codec Runtime Libraries

| Package | Build Dependency |
|---------|------------------|
| `libopus0` | libopus-dev |
| `libsndfile1` | libsndfile1-dev |
| `libshout3` | libshout3-dev |
| `libmpg123-0` | libmpg123-dev |
| `libmp3lame0` | libmp3lame-dev |
| `libavformat58` | libavformat-dev |
| `libswscale5` | libswscale-dev |

### Database Runtime Libraries

| Package | Build Dependency |
|---------|------------------|
| `libsqlite3-0` | libsqlite3-dev |
| `libpq5` | libpq-dev |
| `unixodbc` | unixodbc-dev |

### Other Runtime Libraries

| Package | Purpose |
|---------|---------|
| `libsofia-sip-ua0` | SIP stack runtime |
| `libsrtp2-1` | SRTP runtime |
| `libxml2` | XML parsing |
| `liblua5.2-0` | Lua runtime |
| `libtcmalloc-minimal4` | tcmalloc runtime |

### System Utilities

As requested by user:

| Package | Purpose |
|---------|---------|
| `procps` | ps, top, kill commands |
| `net-tools` | netstat, ifconfig |
| `iputils-ping` | ping command |
| `iproute2` | ip command |
| `lsof` | List open files |
| `vim` | Text editor |
| `curl` | HTTP client |
| `wget` | File downloader |
| `ca-certificates` | SSL certificates |

### Process Management

| Package | Purpose |
|---------|---------|
| `supervisor` | Process manager (systemd alternative for Docker) |

---

## Multi-Stage Build Process

The Dockerfile uses a multi-stage build to minimize final image size:

### Stage 1: Builder Base
- Install all build dependencies
- Serves as base for subsequent build stages

### Stage 1.5: Build spandsp
- Clone spandsp repository
- Build and install spandsp v0d2e6ac
- Install to `/usr/local`

### Stage 1.6: Build sofia-sip
- Clone sofia-sip repository
- Build and install sofia-sip v1.13.17
- Install to `/usr/local`

### Stage 2: Build FreeSWITCH
- Copy spandsp and sofia-sip from previous stages
- Clone FreeSWITCH v1.10.11 source
- Run `bootstrap.sh` to generate configure scripts
- Disable optional modules (Python, Java, Perl, mod_verto, etc.)
- Configure with flags:
  - `--prefix=/usr/local/freeswitch`
  - `--enable-core-pgsql-support`
  - `--enable-core-odbc-support`
  - `--enable-tcmalloc`
  - `--without-python`, `--without-python3`, `--without-java`, `--without-perl`
- Compile with `make -j ${BUILD_CPUS}`
- Install with `make install`
- Install sounds and sample configuration

### Stage 3: Runtime Image
- Start from clean Debian base
- Install only runtime dependencies (no build tools)
- Copy spandsp, sofia-sip, and FreeSWITCH from build stages
- Configure extensions 1000 and 1001
- Set up supervisor for process management
- Create entrypoint script

**Image Size Comparison**:
- Build stage: ~3-4 GB (with all build tools and source code)
- Runtime image: ~800 MB - 1 GB (only binaries and runtime libraries)

---

## Common Build Errors and Solutions

Below are all errors encountered during the build process, with explanations and solutions.

### Error 1: libyuv-dev Package Not Found

**Error Message**:
```
E: Unable to locate package libyuv-dev
```

**Explanation**:
- `libyuv` is Google's library for YUV image format conversion (used in video processing)
- Not available in Debian Bullseye repositories
- FreeSWITCH can use alternative libraries for video support

**Solution**:
- Removed `libyuv-dev` from dependency list
- Video support still works via `libavformat-dev` and `libswscale-dev` (FFmpeg libraries)

**Impact**: None - video features still fully functional

---

### Error 2: libtool Executable Not Found

**Error Message**:
```
build-requirements: libtool not found.
You need libtool version 1.5.14 or newer to build FreeSWITCH from source.
```

**Explanation**:
- `libtool` package provides libtool library files
- `libtool-bin` package provides the `/usr/bin/libtool` executable
- FreeSWITCH bootstrap requires the executable, not just the library

**Solution**:
- Added `libtool-bin` package to build dependencies

**Why this happens**: Debian split libtool into two packages for modularity

---

### Error 3: ODBC Support Missing

**Error Message**:
```
configure: error: no usable libodbc; please install unixodbc devel package or equivalent
```

**Explanation**:
- ODBC (Open Database Connectivity) provides generic database interface
- FreeSWITCH uses ODBC for database backends (MySQL, PostgreSQL, SQL Server, etc.)
- Enabled by default in FreeSWITCH configure

**Solution**:
- Added `unixodbc-dev` to build dependencies
- Added `unixodbc` to runtime dependencies

**Why needed**: Allows FreeSWITCH to connect to various databases without database-specific code

---

### Error 4: spandsp Library Missing

**Error Message**:
```
checking for spandsp >= 3.0... configure: error: no usable spandsp; please install spandsp3 devel package or equivalent
```

**Explanation**:
- spandsp provides DSP (Digital Signal Processing) for telephony
- Critical for fax support (T.38), DTMF detection, echo cancellation
- Debian package version may be outdated or incompatible with FreeSWITCH 1.10.11

**Solution**:
- Created separate Docker build stage to compile spandsp v0d2e6ac from source
- Copied compiled libraries to FreeSWITCH build and runtime stages

**Build stage**:
```dockerfile
FROM builder AS spandsp
RUN git clone https://github.com/freeswitch/spandsp.git \
    && cd spandsp \
    && git checkout 0d2e6ac \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

**Why from source**: Ensures exact version compatibility with FreeSWITCH 1.10.11

---

### Error 5: sofia-sip Library Missing

**Error Message** (implied from spandsp pattern):
```
checking for sofia-sip >= 1.13... configure: error: no usable sofia-sip
```

**Explanation**:
- sofia-sip is the SIP protocol stack used by FreeSWITCH
- Handles all SIP message parsing, generation, and protocol logic
- FreeSWITCH requires specific version for compatibility

**Solution**:
- Created separate Docker build stage to compile sofia-sip v1.13.17 from source
- Copied compiled libraries to FreeSWITCH build and runtime stages

**Build stage**:
```dockerfile
FROM builder AS sofia-sip
RUN git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git \
    && cd sofia-sip \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

**Why from source**: Debian package may be too old or have incompatible patches

---

### Error 6: mod_verto Requires libks2/libks

**Error Message**:
```
checking for libks2 >= 2.0.0... checking for libks >= 1.8.2... no
configure: error: You need to either install libks2 or libks or disable mod_verto in modules.conf
```

**Explanation**:
- `mod_verto` implements the Verto protocol (deprecated WebRTC signaling protocol)
- Verto was FreeSWITCH's original WebRTC solution before SIP over WebSocket became standard
- Requires `libks` (Kite Signaling library), which is complex to build
- **Verto is deprecated** - modern WebRTC uses SIP over WebSocket + SRTP

**Solution**:
- Disabled `mod_verto` in `modules.conf`
- Also disabled other optional modules requiring complex dependencies:
  - `mod_skinny` - Cisco SCCP protocol (niche use case)
  - `mod_signalwire` - SignalWire cloud integration (proprietary)
  - `mod_av` - Advanced video (requires additional libraries)

**Code**:
```dockerfile
RUN sed -i 's|^endpoints/mod_verto$|#endpoints/mod_verto|' modules.conf \
    && sed -i 's|^endpoints/mod_skinny$|#endpoints/mod_skinny|' modules.conf \
    && sed -i 's|^applications/mod_signalwire$|#applications/mod_signalwire|' modules.conf \
    && sed -i 's|^applications/mod_av$|#applications/mod_av|' modules.conf
```

**Impact**: None - use standard SIP over WebSocket for WebRTC instead of deprecated Verto

**WebRTC still works via**:
- mod_sofia (SIP over WebSocket on port 5080/5081)
- mod_srtp (SRTP encryption)
- mod_opus, mod_vp8, mod_h264 (WebRTC codecs)

---

### Error 7: Python 2.x Detection Failing

**Error Message**:
```
checking location of site-packages... Traceback (most recent call last):
File "<string>", line 1, in <module>
ImportError: cannot import name 'sysconfig' from 'distutils'
configure: error: Unable to detect python site-packages path
```

**Explanation**:
- FreeSWITCH configure script tries to detect Python for `mod_python`
- Python 3.9+ removed `distutils.sysconfig` (deprecated module)
- We don't need Python modules - better to use Event Socket Library (ESL) for external control

**Solution**:
- Added `--without-python` flag to configure
- Disabled `mod_python` in `modules.conf`

**Code**:
```dockerfile
RUN sed -i 's|^languages/mod_python$|#languages/mod_python|' modules.conf

RUN ./configure \
    --prefix=/usr/local/freeswitch \
    --without-python \
    ...
```

**Why disable Python modules**:
- **Stability**: Embedded interpreters can cause crashes
- **Better alternative**: Use Event Socket Library (ESL) to control FreeSWITCH from external Python scripts
- **Separation**: FreeSWITCH and application code run in separate processes
- **Flexibility**: Can restart application without restarting FreeSWITCH

---

### Error 8: Python 3.x Still Being Detected

**Error Message**:
```
configure: WARNING: python support disabled, building mod_python will fail!
checking for python3... /usr/bin/python3
checking python3 version... 3.9.2
checking for python3 distutils... yes
checking location of python3 site-packages... Traceback (most recent call last):
  File "<string>", line 1, in <module>
ImportError: cannot import name 'sysconfig' from 'distutils'
configure: error: Unable to detect python3 site-packages path
```

**Explanation**:
- `--without-python` only disables Python 2.x checks
- FreeSWITCH configure script separately checks for Python 3.x for `mod_python3`
- Same `distutils.sysconfig` issue with Python 3.9+

**Solution**:
- Added `--without-python3` flag to configure
- Disabled `mod_python3` in `modules.conf`

**Code**:
```dockerfile
RUN sed -i 's|^languages/mod_python3$|#languages/mod_python3|' modules.conf

RUN ./configure \
    --prefix=/usr/local/freeswitch \
    --without-python \
    --without-python3 \
    ...
```

---

### Error 9: libvpx Requires yasm or nasm

**Error Message**:
```
Neither yasm nor nasm have been found. See the prerequisites section in the README for more info.

Configuration failed. This could reflect a misconfiguration of your
toolchains, improper options selected, or another problem.
make: *** [Makefile:4487: libs/libvpx/Makefile] Error 1
```

**Explanation**:
- `libvpx` provides VP8 and VP9 video codecs (critical for WebRTC video)
- Requires assembly optimizations for acceptable performance
- `yasm` or `nasm` assemblers needed to compile assembly code

**Solution**:
- Added `nasm` package to build dependencies

**Code**:
```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build tools
    build-essential \
    ...
    nasm \
    ...
```

**Why nasm**: Modern, actively maintained, supports both x86 and x86_64

**Impact**: Enables high-performance VP8/VP9 video codecs for WebRTC

---

### Error 10: UUID Header Missing

**Error Message**:
```
src/switch_apr.c:90:10: fatal error: uuid/uuid.h: No such file or directory
   90 | #include <uuid/uuid.h>
      |          ^~~~~~~~~~~~~
compilation terminated.
make[1]: *** [Makefile:2310: src/libfreeswitch_la-switch_apr.lo] Error 1
```

**Explanation**:
- FreeSWITCH uses UUIDs (Universally Unique Identifiers) extensively:
  - Unique call IDs
  - Channel UUIDs
  - Session identifiers
- `uuid/uuid.h` provided by `uuid-dev` package
- `libuuid1` provides runtime library

**Solution**:
- Added `uuid-dev` to build dependencies
- Added `libuuid1` to runtime dependencies

**Code**:
```dockerfile
# Build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ...
    uuid-dev \
    ...

# Runtime dependencies (in runtime stage)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ...
    libuuid1 \
    ...
```

**Why critical**: Without UUIDs, FreeSWITCH cannot generate unique call identifiers

---

### Error 11: Configuration Directory Not Created

**Error Message**:
```
cp: target '/usr/local/freeswitch/conf/' is not a directory
make: *** [Makefile:xxx] Error 1
```

**Explanation**:
- After successful compilation and `make install`, configuration directory doesn't exist
- `make install` creates main directories (`/usr/local/freeswitch/bin`, `/usr/local/freeswitch/lib`) but not `conf/`
- The vanilla configuration from source needs to be copied to this directory
- `cp` fails because target directory doesn't exist

**Solution**:
- Create `/usr/local/freeswitch/conf` directory before copying configuration

**Code**:
```dockerfile
# Install sample configuration
RUN mkdir -p /usr/local/freeswitch/conf \
    && cp -r /usr/local/src/freeswitch/conf/vanilla/* /usr/local/freeswitch/conf/ \
    && echo "✅ Sample configuration installed"
```

**Why this happens**:
- `make install` only creates directories for binaries and libraries
- Configuration is meant to be installed via `make samples` or manually
- We manually install vanilla config for better control

**Impact**: Without vanilla configuration, FreeSWITCH won't start (missing vars.xml, dialplan, SIP profiles, etc.)

---

### Error 12: Event Socket Module Not Built (fs_cli Can't Connect)

**Error Message**:
```bash
# fs_cli
[ERROR] fs_cli.c:1699 main() Error Connecting []
Usage: fs_cli [-H <host>] [-P <port>] [-p <secret>]...
```

**Symptoms**:
- `fs_cli` shows "Error Connecting []"
- Port 8021 is not listening: `netstat -an | grep 8021` shows nothing
- Module file missing: `/usr/local/freeswitch/mod/mod_event_socket.so` doesn't exist
- FreeSWITCH runs but Event Socket interface unavailable
- SIP profiles work normally (ports 5060, 5080)

**Explanation**:
- `mod_event_socket` provides the Event Socket Layer (ESL) interface
- Required for `fs_cli` to connect and control FreeSWITCH
- Default FreeSWITCH `modules.conf` may not include it in `event_handlers/` section
- If not in `modules.conf`, the module won't be compiled during `make`
- Without this module, fs_cli has no way to connect to FreeSWITCH

**Solution**:
- Explicitly ensure `mod_event_socket` is enabled in `modules.conf` before building
- Configure Event Socket to bind to IPv4 instead of IPv6

**Code**:
```dockerfile
# 1. Enable module in modules.conf (after bootstrap, before disabling optional modules)
RUN grep -q "^event_handlers/mod_event_socket$" modules.conf || \
    echo "event_handlers/mod_event_socket" >> modules.conf \
    && echo "✅ Ensured critical modules are enabled (mod_event_socket)"

# 2. Configure Event Socket for IPv4 binding (in runtime stage)
RUN cat > /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml <<'EOF'
<configuration name="event_socket.conf" description="Socket Client">
  <settings>
    <param name="nat-map" value="false"/>
    <!-- Bind to all IPv4 interfaces for fs_cli access -->
    <param name="listen-ip" value="0.0.0.0"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
    <!--<param name="apply-inbound-acl" value="loopback.auto"/>-->
    <!--<param name="stop-on-bind-error" value="true"/>-->
  </settings>
</configuration>
EOF
```

**IPv4 vs IPv6 Binding**:
- Default config uses `::` (IPv6 all interfaces)
- We use `0.0.0.0` (all IPv4 interfaces) for Docker compatibility
- This allows fs_cli to connect from both inside container and host machine
- For security, use `127.0.0.1` (localhost only) or enable ACL

**Why this happens**:
- FreeSWITCH source's default `modules.conf` varies by version
- Some versions don't include `mod_event_socket` by default
- The module must be explicitly listed under `event_handlers/` section
- Without it, configure/make skip the module entirely

**Verification**:
```bash
# Check if module exists after build
ls -la /usr/local/freeswitch/mod/mod_event_socket.so

# Check if port 8021 is listening
netstat -an | grep 8021

# Test fs_cli connection
fs_cli -x "status"
```

**Impact**: Without Event Socket:
- ❌ Can't use `fs_cli` for management
- ❌ Can't use Event Socket Library (ESL) for external applications
- ❌ Can't execute API commands remotely
- ✅ SIP calling still works (different module)

---

### Error 13: FreeSWITCH Not Loading Configuration (Module Exists But Won't Load)

**Error Message**:
```bash
# fs_cli still fails even though module exists
docker exec freeswitch ls -la /usr/local/freeswitch/lib/freeswitch/mod/mod_event_socket.so
# Shows: -rw-r--r-- 1 root root ... mod_event_socket.so (FILE EXISTS!)

docker exec freeswitch netstat -an | grep 8021
# Shows: (nothing - port not listening)

docker exec -it freeswitch fs_cli
# [ERROR] fs_cli.c:1699 main() Error Connecting []
```

**Symptoms**:
- `mod_event_socket.so` binary **exists** in `/usr/local/freeswitch/lib/freeswitch/mod/`
- Port 8021 is **not listening**
- `fs_cli` shows "Error Connecting []"
- FreeSWITCH **is running** (SIP works on port 5060)
- No `freeswitch.log` created in `/usr/local/freeswitch/log/`
- FreeSWITCH appears to run with minimal/fallback configuration

**Root Cause**:
FreeSWITCH was started **without specifying configuration paths**. The supervisor command was:
```bash
command=/usr/local/freeswitch/bin/freeswitch -nonat -nc -nf
```

Without `-conf`, `-log`, and `-db` flags, FreeSWITCH uses **default search paths** (`/etc/freeswitch`, `/usr/local/etc/freeswitch`) which don't match our custom prefix `/usr/local/freeswitch`.

Result:
- FreeSWITCH starts but doesn't load `/usr/local/freeswitch/conf/`
- `event_socket.conf.xml` is never loaded
- Module binary exists but is never activated
- No logs are written to the expected location

**Solution**:
Start FreeSWITCH with **explicit configuration paths** in supervisor command:

**Code**:
```dockerfile
# Supervisor configuration with explicit paths
RUN mkdir -p /etc/supervisor/conf.d && cat > /etc/supervisor/conf.d/freeswitch.conf <<'EOF'
[program:freeswitch]
command=/usr/local/freeswitch/bin/freeswitch -nonat -nc -nf \
  -conf /usr/local/freeswitch/conf \
  -log  /usr/local/freeswitch/log \
  -db   /usr/local/freeswitch/db
autostart=true
autorestart=true
startretries=3
user=freeswitch
stdout_logfile=/var/log/supervisor/freeswitch.log
stderr_logfile=/var/log/supervisor/freeswitch_err.log
EOF
```

**Why This Happens**:
- FreeSWITCH has **hardcoded default paths** in its source code
- When built with `--prefix=/usr/local/freeswitch`, binaries go to custom location
- BUT: without explicit `-conf` flag, FreeSWITCH searches default paths first
- This is a common issue with custom FreeSWITCH installations

**FreeSWITCH Default Search Order** (without `-conf`):
1. `/etc/freeswitch/` (doesn't exist)
2. `/usr/local/etc/freeswitch/` (doesn't exist)
3. Fallback to minimal embedded config
4. Never checks `/usr/local/freeswitch/conf/` (our actual config location!)

**Verification After Fix**:
```bash
# Check module loads correctly
docker exec freeswitch bash -c "ls /usr/local/freeswitch/lib/freeswitch/mod/mod_event_socket.so"
# Should exist

# Check port 8021 is listening
docker exec freeswitch netstat -an | grep 8021
# Should show: tcp 0 0 0.0.0.0:8021 0.0.0.0:* LISTEN

# Check logs are being created
docker exec freeswitch ls -la /usr/local/freeswitch/log/freeswitch.log
# Should exist and grow over time

# Test fs_cli connection
docker exec -it freeswitch fs_cli
# Should show FreeSWITCH CLI prompt
```

**Impact**:
- ❌ Without explicit paths: FreeSWITCH runs but with wrong/minimal config
- ❌ Modules exist but don't load
- ❌ No proper logging
- ❌ Event Socket never activates
- ✅ With explicit paths: All modules load correctly, fs_cli works, full functionality

**Best Practice**:
Always start FreeSWITCH in Docker with explicit paths:
- `-conf /usr/local/freeswitch/conf` (configuration directory)
- `-log /usr/local/freeswitch/log` (log directory)
- `-db /usr/local/freeswitch/db` (database directory)

This makes the system **predictable**, **debuggable**, and **production-ready**.

---

### Error 14: Language Bindings (Java, Perl, PHP)

**Not yet encountered, but proactively disabled**

**Why disabled**:
```dockerfile
RUN sed -i 's|^languages/mod_java$|#languages/mod_java|' modules.conf \
    && sed -i 's|^languages/mod_perl$|#languages/mod_perl|' modules.conf \
    && sed -i 's|^languages/mod_php$|#languages/mod_php|' modules.conf

RUN ./configure \
    --without-java \
    --without-perl \
    ...
```

**Reasons to disable**:
- **Java**: Requires JDK, JNI setup, large memory footprint
- **Perl**: Requires Perl dev headers, CPAN modules
- **PHP**: Requires PHP dev headers, unstable with FreeSWITCH

**Best practice**: Use Event Socket Library (ESL) for external control in any language

---

## Module Configuration

### Modules Disabled

The following modules are disabled in `modules.conf` to avoid complex dependencies:

| Module | Category | Reason |
|--------|----------|--------|
| `mod_verto` | Endpoint | Deprecated WebRTC protocol, requires libks |
| `mod_skinny` | Endpoint | Cisco SCCP, niche use case |
| `mod_signalwire` | Application | SignalWire cloud integration, proprietary |
| `mod_av` | Application | Advanced video, requires additional libraries |
| `mod_python` | Language | Python 2.x binding, deprecated |
| `mod_python3` | Language | Python 3.x binding, use ESL instead |
| `mod_java` | Language | Java binding, use ESL instead |
| `mod_perl` | Language | Perl binding, use ESL instead |
| `mod_php` | Language | PHP binding, use ESL instead |

### Critical Modules Enabled

The following critical modules are enabled and verified in testing:

**Endpoints**:
- `mod_sofia` - SIP and SIP over WebSocket (primary signaling)

**Applications**:
- `mod_conference` - Conference bridging
- `mod_dptools` - Dialplan tools
- `mod_commands` - API commands
- `mod_voicemail` - Voicemail system

**Dialplan**:
- `mod_dialplan_xml` - XML dialplan (default)
- `mod_lua` - Lua scripting (recommended)

**Codecs - Audio**:
- `mod_opus` - Opus codec (WebRTC)
- `mod_g711` - G.711 (PCMU/PCMA)
- `mod_g722` - G.722 wideband

**Codecs - Video**:
- `mod_vp8` - VP8 codec (WebRTC)
- `mod_vp9` - VP9 codec (WebRTC)
- `mod_h264` - H.264 codec

**Formats**:
- `mod_sndfile` - Audio file I/O
- `mod_local_stream` - Local audio streaming

**Event Handlers**:
- `mod_event_socket` - Event Socket (for fs_cli and ESL)

**Total Modules**: 100+ modules enabled by default (minus disabled language bindings and optional modules)

---

## Build Process

### Build Script Usage

```bash
./dockerfiles/build-freeswitch-base.sh [image-name]
```

**Default image name**: `freeswitch-base:1.10.11`

**Example**:
```bash
./dockerfiles/build-freeswitch-base.sh freeswitch-base:latest
```

### Build Script Features

1. **Platform Detection**: Automatically detects ARM64 (Apple Silicon) and uses x86_64 emulation
2. **CPU Detection**: Uses all available CPU cores for parallel compilation
3. **Version Management**: Reads spandsp and sofia-sip versions from `.env` file
4. **Progress Display**: Shows estimated build time and configuration

### Build Stages Timing

| Stage | Time (x86_64) | Time (ARM64 emulated) |
|-------|---------------|----------------------|
| Base builder dependencies | 2-3 min | 3-5 min |
| Build spandsp | 1-2 min | 2-4 min |
| Build sofia-sip | 2-3 min | 3-6 min |
| Configure FreeSWITCH | 2-3 min | 3-5 min |
| Compile FreeSWITCH | 20-30 min | 40-60 min |
| Install sounds | 2-3 min | 3-5 min |
| Runtime stage | 2-3 min | 3-5 min |
| **Total** | **30-45 min** | **60-90 min** |

### Build Optimization

The build uses parallel compilation:
- `make -j ${BUILD_CPUS}` - Uses all available CPU cores
- Docker BuildKit for layer caching
- Multi-stage build to minimize final image size

---

## Verification

### Automated Testing

Use the provided test script:

```bash
./dockerfiles/test-freeswitch-base.sh freeswitch-base:1.10.11
```

### Manual Testing

**Start container**:
```bash
docker run -d \
    --name freeswitch-test \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 8021:8021/tcp \
    freeswitch-base:1.10.11
```

**Connect with fs_cli**:
```bash
docker exec -it freeswitch-test fs_cli
```

**Check status**:
```
fs_cli> status
fs_cli> show modules
fs_cli> sofia status
```

**Expected results**:
- FreeSWITCH uptime displayed
- 100+ modules loaded
- SIP profiles running on port 5060/5080

### Extension Testing

Extensions 1000 and 1001 are pre-configured with password `1234`.

**Using softphone** (like Zoiper, Linphone, or Bria):
1. Register extension 1000: `sip:1000@<container-ip>:5060` (password: 1234)
2. Register extension 1001: `sip:1001@<container-ip>:5060` (password: 1234)
3. Call from 1000 to 1001 by dialing `1001`
4. Verify two-way audio

---

## Troubleshooting

### Build Fails at Configure Stage

**Check**:
```bash
# View full build log
docker build -f dockerfiles/Dockerfile.freeswitch-base -t test . 2>&1 | tee build.log

# Search for "error" in log
grep -i "error" build.log | grep -v "NORMAL_CLEARING"
```

**Common causes**:
- Missing development package (check "Common Build Errors" section)
- Wrong package version for Debian release
- Network issues cloning repositories

### Build Fails at Compilation Stage

**Symptoms**: Errors like "No such file or directory" for header files

**Solution**: Add missing `-dev` package to build dependencies and corresponding runtime package

### FreeSWITCH Won't Start

**Check logs**:
```bash
docker logs freeswitch-test
docker exec freeswitch-test cat /usr/local/freeswitch/log/freeswitch.log
```

**Common issues**:
- Missing runtime library (add to runtime dependencies)
- Configuration file syntax error
- Permissions issue (files should be owned by `freeswitch:freeswitch`)

### fs_cli Cannot Connect

**Check**:
```bash
# Verify event socket configuration
docker exec freeswitch-test cat /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml

# Verify port is listening
docker exec freeswitch-test netstat -an | grep 8021
```

**Default event socket**: Port 8021, password "ClueCon"

### SIP Clients Cannot Register

**Check**:
```bash
# Verify SIP profile is running
docker exec freeswitch-test fs_cli -x "sofia status"

# Check for binding errors
docker exec freeswitch-test cat /usr/local/freeswitch/log/freeswitch.log | grep -i "bind"
```

**Common issues**:
- Port 5060 already in use on host
- Firewall blocking UDP 5060
- NAT configuration needed for external clients

---

## Performance Optimization

### Build Time Optimization

**Use more CPU cores**:
```bash
# Manually set CPU cores
docker build --build-arg BUILD_CPUS=32 ...
```

**Enable BuildKit caching**:
```bash
export DOCKER_BUILDKIT=1
```

### Runtime Optimization

**tcmalloc enabled**: FreeSWITCH is configured with `--enable-tcmalloc` for better memory allocation performance

**Supervisor**: Uses supervisor instead of running FreeSWITCH as PID 1 for better signal handling

---

## Additional Resources

- **FreeSWITCH Official Docs**: https://freeswitch.org/confluence/
- **Installation Guide**: https://freeswitch.org/confluence/display/FREESWITCH/Installation
- **Modules Documentation**: https://freeswitch.org/confluence/display/FREESWITCH/Modules
- **Event Socket Library**: https://freeswitch.org/confluence/display/FREESWITCH/Event+Socket+Library
- **WebRTC Guide**: https://freeswitch.org/confluence/display/FREESWITCH/WebRTC

---

## Summary

Building FreeSWITCH 1.10.11 from source requires careful attention to dependencies. This guide documents all dependencies discovered through the build process and provides solutions to common errors.

**Key takeaways**:
1. **Multi-stage builds** minimize final image size (3-4 GB build → 800 MB runtime)
2. **Build from source** for spandsp and sofia-sip ensures version compatibility
3. **Disable language bindings** - use Event Socket Library instead for better stability
4. **Test thoroughly** - use provided test script to verify all components
5. **Document everything** - this guide helps future builds and troubleshooting

**Next steps**: After successful build, add custom modules one by one using the module testing approach in `dockerfiles/Dockerfile.mod-test`.

---


# Appendix B: Complete MacBook Testing Guide

This section provides detailed instructions for running and testing FreeSWITCH images on MacBook with SIP clients.

# Running FreeSWITCH with Transcription Modules on MacBook

This guide shows how to quickly run FreeSWITCH Docker images on your MacBook using the `run-on-macbook.sh` script.

---

## Quick Start

### Prerequisites

1. **Docker Desktop** installed and running
2. **MacBook** (Intel or Apple Silicon)
3. **API Keys** (optional, for transcription features):
   - Deepgram API Key: https://console.deepgram.com/
   - Azure Speech Services Key: https://portal.azure.com/

---

## Available Images

### 1. Base Image (No Transcription)

```bash
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-base:latest
```

**Includes:**
- ✅ FreeSWITCH 1.10.11
- ✅ Extensions 1000 & 1001 (password: 1234)
- ✅ Echo test, voicemail, conference

**Size:** ~850 MB
**Use Case:** Basic SIP calling, testing, learning FreeSWITCH

---

### 2. Audio Fork Module

```bash
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-mod-audio-fork:latest
```

**Includes:**
- ✅ Everything from base image
- ✅ mod_audio_fork (audio streaming via WebSocket)
- ✅ libwebsockets 4.3.3

**Size:** ~900 MB
**Use Case:** Real-time audio streaming, custom audio processing

---

### 3. Deepgram Transcription

```bash
# Without API key (configure later)
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-mod-deepgram-transcribe:latest

# With API key (ready to use)
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-deepgram-transcribe:latest \
  YOUR_DEEPGRAM_API_KEY
```

**Includes:**
- ✅ Everything from audio fork image
- ✅ mod_deepgram_transcribe
- ✅ Speaker diarization, NER, keyword boosting

**Size:** ~950 MB
**Use Case:** Real-time Deepgram speech-to-text transcription

---

### 4. Azure Transcription (Includes ALL Modules!)

```bash
# Without API keys
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-mod-azure-transcribe:latest

# With Azure only
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-azure-transcribe:latest \
  "" \
  YOUR_AZURE_SUBSCRIPTION_KEY \
  eastus

# With both Deepgram AND Azure
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-azure-transcribe:latest \
  YOUR_DEEPGRAM_API_KEY \
  YOUR_AZURE_SUBSCRIPTION_KEY \
  eastus
```

**Includes:**
- ✅ Everything from deepgram image
- ✅ mod_azure_transcribe
- ✅ Microsoft Azure Speech SDK 1.47.0
- ✅ **ALL THREE TRANSCRIPTION MODULES**
- ✅ **Pre-configured example configuration files** (dialplan, user directories 1000-1002)

**Size:** ~1.2 GB
**Use Case:** Azure speech-to-text, or using both Deepgram and Azure

**Note:** The Azure image includes ready-to-use configuration files:
- Complete dialplan with Azure transcription examples
- User directories (1000, 1001, 1002) with example Azure configuration
- No manual configuration needed for basic testing

---

## Script Usage

### Basic Syntax

```bash
./dockerfiles/run-on-macbook.sh <IMAGE> [DEEPGRAM_KEY] [AZURE_KEY] [AZURE_REGION]
```

### Parameters

| Parameter | Required | Default | Description |
|-----------|----------|---------|-------------|
| `IMAGE` | ✅ Yes | - | Docker Hub image name |
| `DEEPGRAM_KEY` | ❌ No | - | Deepgram API key |
| `AZURE_KEY` | ❌ No | - | Azure subscription key |
| `AZURE_REGION` | ❌ No | `eastus` | Azure region |

---

## Testing After Launch

### 1. Verify Container Running

```bash
docker ps | grep freeswitch
```

**Expected:** Container status "Up X minutes (healthy)"

### 2. Check FreeSWITCH Status

```bash
docker exec -it freeswitch fs_cli -x "status"
```

**Expected:** Shows FreeSWITCH version, uptime, active sessions

### 3. Verify Loaded Modules

```bash
docker exec -it freeswitch fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|azure'
```

**Expected (for azure image):**
```
api,uuid_audio_fork,mod_audio_fork,...
api,uuid_deepgram_transcribe,mod_deepgram_transcribe,...
api,uuid_azure_transcribe,mod_azure_transcribe,...
```

---

## SIP Client Setup

### Install a SIP Client

Choose one:
- **Zoiper** (Recommended): https://www.zoiper.com/
- **Linphone** (Open Source): https://www.linphone.org/
- **Telephone** (Mac Native): https://www.64characters.com/telephone/

### Configure Extension 1000

| Setting | Value |
|---------|-------|
| **Username** | 1000 |
| **Password** | 1234 |
| **Domain/Host** | localhost |
| **Port** | 5060 |
| **Transport** | UDP |

### Configure Extension 1001

Same settings but with username `1001`.

**Tip:** Use two different SIP clients (e.g., Zoiper for 1000, Linphone for 1001) to test calling between extensions.

---

## Making Calls

### Test Basic Calling

1. **Register both extensions** (1000 and 1001)
2. **From 1000**: Dial `1001`
3. **Answer on 1001**
4. **Verify two-way audio**

### Test Echo Service

From any extension, dial `9196`:
- Speak into microphone
- Should hear your voice echoed back
- Confirms audio path is working

### Test Conference

From both extensions, dial `3000`:
- Both join the same conference room
- Test multi-party audio

---

## Using Transcription Features

### Deepgram Transcription

#### Option 1: Pre-configured (if API key passed to script)

Get active call UUID:
```bash
docker exec -it freeswitch fs_cli
freeswitch@internal> show channels
```

Start transcription (mono - caller only):
```bash
freeswitch@internal> uuid_deepgram_transcribe <call-uuid> start en-US interim
```

Start transcription (stereo - both caller and callee on separate channels):
```bash
freeswitch@internal> uuid_deepgram_transcribe <call-uuid> start en-US interim stereo
```

#### Option 2: Configure per-call

```bash
docker exec -it freeswitch fs_cli
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_API_KEY your-api-key
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_SPEECH_MODEL phonecall
freeswitch@internal> uuid_setvar <call-uuid> DEEPGRAM_SPEECH_TIER nova
freeswitch@internal> uuid_deepgram_transcribe <call-uuid> start en-US interim
```

Stop transcription:
```bash
freeswitch@internal> uuid_deepgram_transcribe <call-uuid> stop
```

#### Option 3: User Directory Configuration (Persistent)

Configure Deepgram settings per user so they're automatically applied to all calls.

**1. Access the container:**
```bash
docker exec -it freeswitch bash
```

**2. Edit user configuration** (e.g., for extension 1000):
```bash
vi /usr/local/freeswitch/conf/directory/default/1000.xml
```

**3. Add Deepgram variables:**
```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <!-- Standard variables -->
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1000"/>

      <!-- Deepgram Transcription Variables -->
      <variable name="DEEPGRAM_API_KEY" value="your-deepgram-api-key"/>
      <variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
      <variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
      <variable name="DEEPGRAM_SPEECH_DIARIZE" value="true"/>
      <variable name="DEEPGRAM_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION" value="true"/>
    </variables>
  </user>
</include>
```

**4. Reload configuration:**
```bash
docker exec -it freeswitch fs_cli -x 'reloadxml'
```

**5. Start transcription on any call from that user:**
```bash
# Get call UUID
docker exec -it freeswitch fs_cli -x 'show calls'

# Start transcription (mono)
docker exec -it freeswitch fs_cli -x 'uuid_deepgram_transcribe <uuid> start en-US interim'

# Or start transcription (stereo - both parties)
docker exec -it freeswitch fs_cli -x 'uuid_deepgram_transcribe <uuid> start en-US interim stereo'
```

**Benefits:**
- Variables automatically applied to all calls from that user
- No need to set per-call or pass to script
- Persistent across container restarts if directory is mounted as volume
- Different settings per user/department

---

### Azure Transcription

#### Option 1: Pre-configured (if API key passed to script)

```bash
docker exec -it freeswitch fs_cli
freeswitch@internal> show channels  # Get call UUID
freeswitch@internal> uuid_azure_transcribe <call-uuid> start en-US interim
```

#### Option 2: Configure per-call

```bash
docker exec -it freeswitch fs_cli
freeswitch@internal> uuid_setvar <call-uuid> AZURE_SUBSCRIPTION_KEY your-key
freeswitch@internal> uuid_setvar <call-uuid> AZURE_REGION eastus
freeswitch@internal> uuid_setvar <call-uuid> AZURE_USE_OUTPUT_FORMAT_DETAILED true
freeswitch@internal> uuid_azure_transcribe <call-uuid> start en-US interim
```

Stop transcription:
```bash
freeswitch@internal> uuid_azure_transcribe <call-uuid> stop
```

---

## Advanced Features

### Deepgram Features

#### Stereo Mode (Both Caller and Callee)
```bash
# Transcribe both parties on separate channels
# Channel 0: Caller, Channel 1: Callee
uuid_deepgram_transcribe <uuid> start en-US interim stereo
```

Use stereo mode for:
- Call center recordings with separate agent/customer channels
- Quality monitoring and compliance
- Better speaker separation than diarization

#### Speaker Diarization
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_DIARIZE true
uuid_deepgram_transcribe <uuid> start en-US interim
```

Use diarization in mono mode to identify different speakers in a single audio channel.

#### Keyword Boosting
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_KEYWORDS "payment:5,refund:4,account:3"
uuid_deepgram_transcribe <uuid> start en-US interim
```

#### PCI Redaction
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_REDACT "pci,ssn,numbers"
uuid_deepgram_transcribe <uuid> start en-US interim
```

#### Named Entity Recognition
```bash
uuid_setvar <uuid> DEEPGRAM_SPEECH_NER true
uuid_deepgram_transcribe <uuid> start en-US interim
```

---

### Azure Features

#### ConversationTranscriber Mode
```bash
# Start transcription using ConversationTranscriber (stereo mode)
# Uses AI-based speaker identification (Guest-1, Guest-2, etc.)
uuid_azure_transcribe <uuid> start en-US interim stereo
```

**Important**: Azure's streaming SDK uses AI-based speaker diarization to identify speakers, not true channel separation. The "Channel" field in results indicates audio source, but speaker identification (e.g., "Guest-1") is done via AI analysis.

#### Speaker Diarization
```bash
# Enable AI-based speaker diarization with ConversationTranscriber
uuid_setvar <uuid> AZURE_DIARIZE_INTERIM_RESULTS true
uuid_setvar <uuid> AZURE_DIARIZATION_SPEAKER_COUNT 2
uuid_setvar <uuid> AZURE_DIARIZATION_MIN_SPEAKER_COUNT 1
uuid_setvar <uuid> AZURE_DIARIZATION_MAX_SPEAKER_COUNT 2
uuid_azure_transcribe <uuid> start en-US interim stereo
```

**Note**: Speaker diarization is a preview feature. For production use or advanced capabilities, you may need to request access by emailing `diarizationrequest@microsoft.com`. See [module README](../modules/mod_azure_transcribe/README.md#advanced-features) for details.

#### Word-Level Timestamps
```bash
# Get detailed timing information for each word
uuid_setvar <uuid> AZURE_WORD_LEVEL_TIMESTAMPS true
uuid_setvar <uuid> AZURE_USE_OUTPUT_FORMAT_DETAILED true
uuid_azure_transcribe <uuid> start en-US interim
```

#### Sentiment Analysis
```bash
# Enable sentiment analysis for emotional tone detection
uuid_setvar <uuid> AZURE_SENTIMENT_ANALYSIS true
uuid_azure_transcribe <uuid> start en-US interim
```

#### Dictation Mode
```bash
# Enable dictation mode for better punctuation and formatting
uuid_setvar <uuid> AZURE_DICTATION_MODE true
uuid_azure_transcribe <uuid> start en-US interim
```

#### Detailed Output with N-best
```bash
uuid_setvar <uuid> AZURE_USE_OUTPUT_FORMAT_DETAILED true
uuid_azure_transcribe <uuid> start en-US interim
```

#### Profanity Filtering
```bash
uuid_setvar <uuid> AZURE_PROFANITY_OPTION masked
uuid_azure_transcribe <uuid> start en-US interim
```

Options: `masked`, `removed`, `raw`

#### Speech Hints (Mono Mode Only)
```bash
uuid_setvar <uuid> AZURE_SPEECH_HINTS "account,balance,payment"
uuid_azure_transcribe <uuid> start en-US interim
```

#### SNR Reporting
```bash
uuid_setvar <uuid> AZURE_REQUEST_SNR true
uuid_azure_transcribe <uuid> start en-US interim
```

---

## Useful Commands

### Container Management

```bash
# View logs
docker logs -f freeswitch

# Access fs_cli
docker exec -it freeswitch fs_cli

# Restart container
docker restart freeswitch

# Stop container
docker stop freeswitch

# Remove container
docker rm -f freeswitch
```

### FreeSWITCH Commands

```bash
# Show active calls
docker exec -it freeswitch fs_cli -x "show calls"

# Show channels
docker exec -it freeswitch fs_cli -x "show channels"

# Show SIP registrations
docker exec -it freeswitch fs_cli -x "sofia status profile internal reg"

# Reload XML configuration
docker exec -it freeswitch fs_cli -x "reloadxml"

# Show loaded modules
docker exec -it freeswitch fs_cli -x "show modules"
```

---

## Troubleshooting

### Container Won't Start

**Check Docker is running:**
```bash
docker info
```

**Check logs:**
```bash
docker logs freeswitch
```

**Remove old container:**
```bash
docker rm -f freeswitch
```

---

### Extension Won't Register

**Check SIP profile:**
```bash
docker exec -it freeswitch fs_cli -x "sofia status profile internal"
```

**Check port is listening:**
```bash
docker exec -it freeswitch netstat -tuln | grep 5060
```

**Check firewall on MacBook:**
- System Preferences → Security & Privacy → Firewall
- Allow Docker to accept incoming connections

---

### No Audio During Calls

**Check RTP ports:**
```bash
docker port freeswitch | grep 16384
```

**For external devices:**
- Get MacBook IP: `ifconfig | grep "inet "`
- Use MacBook IP in SIP client instead of localhost

---

### Transcription Not Working

**Deepgram:**
```bash
# Check API key is set
docker exec -it freeswitch bash -c 'echo $DEEPGRAM_API_KEY'

# Check module loaded
docker exec -it freeswitch fs_cli -x "show modules" | grep deepgram

# Check FreeSWITCH logs
docker logs freeswitch | grep -i deepgram
```

**Azure:**
```bash
# Check API key is set
docker exec -it freeswitch bash -c 'echo $AZURE_SUBSCRIPTION_KEY'
docker exec -it freeswitch bash -c 'echo $AZURE_REGION'

# Check module loaded
docker exec -it freeswitch fs_cli -x "show modules" | grep azure

# Check Azure SDK libraries
docker exec -it freeswitch ls -la /usr/local/lib/MicrosoftSpeechSDK/

# Check FreeSWITCH logs
docker logs freeswitch | grep -i azure
```

---

### fs_cli Won't Connect

**Check event socket:**
```bash
docker exec -it freeswitch netstat -tuln | grep 8021
```

**Try with password:**
```bash
docker exec -it freeswitch fs_cli -H localhost -P ClueCon
```

---

## Performance Tips

### Apple Silicon (M1/M2/M3) Macs

The images run via Rosetta 2 emulation (linux/amd64):
- ✅ Full compatibility
- ⚠️ Slightly slower than native ARM
- ⚠️ Higher CPU usage during transcription

**Tip:** Close other CPU-intensive apps for best transcription performance.

### Intel Macs

Native performance, no emulation needed.

---

## Cleaning Up

### Remove Container

```bash
docker stop freeswitch
docker rm freeswitch
```

### Remove Image (to free space)

```bash
# Remove specific image
docker rmi srt2011/freeswitch-mod-azure-transcribe:latest

# Remove all FreeSWITCH images
docker images | grep freeswitch | awk '{print $3}' | xargs docker rmi
```

### Reclaim Docker Space

```bash
docker system prune -a
```

---

## Cost Considerations

### Deepgram

- Pay-as-you-go pricing
- Free tier available: https://console.deepgram.com/
- Nova models cost more than base models

### Azure Speech Services

- Pay-as-you-go pricing
- Free tier: 5 hours/month
- Detailed pricing: https://azure.microsoft.com/en-us/pricing/details/cognitive-services/speech-services/

**Tip:** Use environment variables to avoid accidentally leaving transcription running:
```bash
# Stop all active transcriptions
docker exec -it freeswitch fs_cli -x "show channels" | grep uuid | awk '{print $1}' | while read uuid; do
  docker exec -it freeswitch fs_cli -x "uuid_deepgram_transcribe $uuid stop"
  docker exec -it freeswitch fs_cli -x "uuid_azure_transcribe $uuid stop"
done
```

---

## Examples by Use Case

### Use Case 1: Basic SIP Testing

```bash
# Use base image (smallest, fastest)
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-base:latest
```

### Use Case 2: Audio Streaming Development

```bash
# Use audio fork module
./dockerfiles/run-on-macbook.sh srt2011/freeswitch-mod-audio-fork:latest
```

### Use Case 3: Deepgram Transcription Development

```bash
# Use Deepgram image with API key
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-deepgram-transcribe:latest \
  $DEEPGRAM_API_KEY
```

### Use Case 4: Azure Transcription Development

```bash
# Use Azure image with API key
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-azure-transcribe:latest \
  "" \
  $AZURE_SUBSCRIPTION_KEY \
  eastus
```

### Use Case 5: Multi-Provider Transcription Testing

```bash
# Use Azure image (has all modules) with both API keys
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-azure-transcribe:latest \
  $DEEPGRAM_API_KEY \
  $AZURE_SUBSCRIPTION_KEY \
  eastus
```

---

## Next Steps

### Production Deployment

For production use, see:
- **Docker Compose**: `dockerfiles/README.md` (Configuration Guide section)
- **Systemd Service**: `dockerfiles/README.md` (Method 4: FreeSWITCH Service Configuration)
- **Dialplan Configuration**: `dockerfiles/README.md` (Method 2: Dialplan Configuration)

### API Documentation

- **Deepgram**: `modules/mod_deepgram_transcribe/README.md`
- **Azure**: `modules/mod_azure_transcribe/README.md`
- **Audio Fork**: `modules/mod_audio_fork/README.md`

### Build From Source

If you need to customize:
- **Build instructions**: `dockerfiles/README.md`
- **Dockerfile**: `dockerfiles/Dockerfile.mod_azure_transcribe`

---

## Support

### Documentation

- Main README: `README.md`
- Docker builds: `dockerfiles/README.md`
- Deployment guide: `dockerfiles/DOCKER_HUB_DEPLOYMENT.md`

### Resources

- FreeSWITCH Wiki: https://freeswitch.org/confluence/
- Deepgram Docs: https://developers.deepgram.com/
- Azure Speech Docs: https://docs.microsoft.com/en-us/azure/cognitive-services/speech-service/

---

**Happy transcribing! 🎙️ → 📝**

---


# Appendix C: Docker Hub Deployment Guide

This section provides step-by-step instructions for pushing images to Docker Hub and testing them.

# Docker Hub Deployment and MacBook Testing Guide

This guide walks you through pushing the FreeSWITCH base image to Docker Hub and testing it on your MacBook with SIP calling between extensions 1000 and 1001.

---

## Prerequisites

1. **Docker Hub Account**: Sign up at https://hub.docker.com if you don't have one
2. **Docker installed on MacBook**: Download from https://www.docker.com/products/docker-desktop
3. **SIP Client**: Install a softphone on your MacBook (recommended options below)

---

## Part 1: Build and Push to Docker Hub (Development Environment)

### Step 1: Build the Image

```bash
# In your development environment
cd /workspaces/freeswitch_modules

# Build the image (takes 30-45 minutes)
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11
```

Wait for the build to complete. You should see:
```
✅ FreeSWITCH configured
✅ FreeSWITCH compiled
✅ FreeSWITCH installed
✅ Sounds installed
✅ Sample configuration installed
```

### Step 2: Verify Local Image Exists

Before pushing, verify the image was built successfully:

```bash
# Check if image already exists
docker images | grep "freeswitch-base.*1.10.11"

# Get image size
docker images freeswitch-base:1.10.11 --format "{{.Repository}}:{{.Tag}}\t{{.Size}}"
```

If the image is not found, build it first:
```bash
./dockerfiles/build-freeswitch-base.sh freeswitch-base:1.10.11
```

### Step 3: Tag the Image for Docker Hub

Replace `YOUR_DOCKERHUB_USERNAME` with your actual Docker Hub username:

```bash
# Tag the image
docker tag freeswitch-base:1.10.11 YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11

# Also tag as latest
docker tag freeswitch-base:1.10.11 YOUR_DOCKERHUB_USERNAME/freeswitch-base:latest
```

**Example**:
```bash
docker tag freeswitch-base:1.10.11 johndoe/freeswitch-base:1.10.11
docker tag freeswitch-base:1.10.11 johndoe/freeswitch-base:latest
```

### Step 4: Login to Docker Hub

```bash
docker login
```

Enter your Docker Hub username and password when prompted.

**Check if already logged in**:
```bash
docker info 2>/dev/null | grep Username
# If you see a username, you're already logged in
```

**Alternative** (using access token for better security):
```bash
# Create access token at: https://hub.docker.com/settings/security
docker login -u YOUR_DOCKERHUB_USERNAME
# Paste access token when prompted for password
```

### Step 5: Push to Docker Hub

```bash
# Push the versioned tag
docker push YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11

# Push the latest tag
docker push YOUR_DOCKERHUB_USERNAME/freeswitch-base:latest
```

**Note**: This will take 5-15 minutes depending on your upload speed. The image is ~800 MB - 1 GB.

You'll see output like:
```
The push refers to repository [docker.io/YOUR_DOCKERHUB_USERNAME/freeswitch-base]
abc123def456: Pushed
789ghi012jkl: Pushed
...
1.10.11: digest: sha256:xxxxx size: 1234
```

### Step 6: Verify on Docker Hub

Visit https://hub.docker.com/r/YOUR_DOCKERHUB_USERNAME/freeswitch-base to confirm the image is published.

**You should see**:
- Image name: `YOUR_DOCKERHUB_USERNAME/freeswitch-base`
- Tags: `1.10.11` and `latest`
- Image size: ~800 MB - 1 GB
- Push timestamp

### Pull Command for Others

Once published, share this command with your team:
```bash
docker pull YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

**Run on any machine** (including MacBook with Apple Silicon):
```bash
docker run -d \
    --name freeswitch \
    --platform linux/amd64 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

---

## Part 2: Pull and Run on MacBook

### Step 1: Open Terminal on MacBook

```bash
# Verify Docker is running
docker --version

# Should show something like: Docker version 24.x.x, build xxxxx
```

### Step 2: Pull the Image from Docker Hub

```bash
docker pull YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

This will download the image to your MacBook (5-10 minutes depending on internet speed).

### Step 3: Run the FreeSWITCH Container

```bash
docker run -d \
    --name freeswitch \
    --platform linux/amd64 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

**Port Mapping Explanation**:
- `5060` - SIP signaling (TCP and UDP)
- `5080` - SIP over WebSocket (for WebRTC)
- `8021` - Event Socket (for fs_cli)
- `16384-16484` - RTP media (audio/video)

### Step 4: Verify FreeSWITCH is Running

```bash
# Check container is running
docker ps | grep freeswitch

# Check logs
docker logs freeswitch

# Should see FreeSWITCH startup messages
```

### Step 5: Connect with fs_cli

```bash
docker exec -it freeswitch fs_cli

docker exec -it freeswitch fs_cli -x "sofia status"
==>
                     Name          Type                                       Data      State
=================================================================================================
            external-ipv6       profile                   sip:mod_sofia@[::1]:5080      RUNNING (0)
               172.17.0.2         alias                                   internal      ALIASED
                 external       profile            sip:mod_sofia@20.192.21.57:5080      RUNNING (0)
    external::example.com       gateway                    sip:joeuser@example.com      NOREG
            internal-ipv6       profile                   sip:mod_sofia@[::1]:5060      RUNNING (0)
                 internal       profile              sip:mod_sofia@172.17.0.2:5060      RUNNING (0)
=================================================================================================

docker exec -it freeswitch cat  /usr/local/freeswitch/conf/sip_profiles/internal.xml | grep ext-rtp-ip
==>  <param name="ext-rtp-ip" value="$${local_ip_v4}"/>
```

You should see the FreeSWITCH CLI prompt:
```
 _____              ______        _____ _______ _____ _    _
|  ___| __ ___  ___/ ___\ \      / /_ _|_   _/ ____| |  | |
| |_ | '__/ _ \/ _ \___ \\ \ /\ / / | |  | || |    | |  | |
|  _|| | |  __/  __/___) |\ V  V /  | |  | || |____| |__| |
|_|  |_|  \___|\___|____/  \_/\_/  |___| |_| \_____|\____/

+OK
freeswitch@internal>
```

**Test commands**:
```
status              # Show FreeSWITCH status
sofia status        # Show SIP profiles
show modules        # List loaded modules (should be 100+)
show channels       # Show active calls
```

Type `exit` or press Ctrl+D to exit fs_cli.

---

## Part 3: Install SIP Client on MacBook

Choose one of these SIP clients for testing:

### Option A: Zoiper (Recommended - Free)

1. Download: https://www.zoiper.com/en/voip-softphone/download/current
2. Install Zoiper 5 for macOS
3. Launch Zoiper

### Option B: Linphone (Open Source - Free)

1. Download: https://www.linphone.org/releases/macosx/app/Linphone-4.x.x.dmg
2. Install and launch

### Option C: Telephone (Mac Native - Free)

1. Install from App Store or https://www.64characters.com/telephone/
2. Launch Telephone

### Option D: Bria (Professional - Paid)

1. Download: https://www.counterpath.com/bria-solo/
2. Free trial available

---

## Part 4: Configure SIP Clients for Extension 1000

### Get Your MacBook's IP Address

```bash
# Get container IP (use this if testing on same MacBook)
docker inspect freeswitch | grep IPAddress

# Or use localhost
SERVER_IP=localhost
```

### Configure Extension 1000 in Zoiper

**Account Settings**:
- Account name: `Extension 1000`
- SIP Username: `1000`
- SIP Password: `1234`
- SIP Domain/Host: `localhost` (or MacBook IP if testing from another device)
- SIP Port: `5060`
- Transport: `UDP`

**Detailed Steps (Zoiper)**:
1. Open Zoiper
2. Settings → Accounts → Add Account
3. Select "SIP"
4. Enter credentials above
5. Click "Create Account"
6. Status should show "Registered" (green)

### Configure Extension 1001 (for Second Client)

You have two options:

**Option A: Install second SIP client on MacBook**
- Use a different SIP client (e.g., if using Zoiper for 1000, use Linphone for 1001)
- Configure with username `1001`, password `1234`

**Option B: Use mobile phone**
- Install Zoiper/Linphone on your iPhone/Android
- Connect to same WiFi network as MacBook
- Use MacBook's local IP address (find with `ifconfig en0 | grep inet`)
- Configure with username `1001`, password `1234`

---

## Part 5: Test Calling Between Extensions

### From Extension 1000 to 1001

1. **On Extension 1000 client**:
   - Dial: `1001`
   - Press Call button
   - You should hear ringing

2. **On Extension 1001 client**:
   - Incoming call should appear
   - Answer the call

3. **Verify**:
   - Both extensions should hear each other
   - Talk to confirm two-way audio

### From Extension 1001 to 1000

1. **On Extension 1001 client**:
   - Dial: `1000`
   - Press Call button

2. **On Extension 1000 client**:
   - Answer incoming call

3. **Verify two-way audio**

### Monitor Calls in fs_cli

While calls are active:

```bash
docker exec -it freeswitch fs_cli
```

Then run:
```
freeswitch@internal> show channels

uuid,direction,created,created_epoch,name,state,cid_name,cid_num,ip_addr,dest,application,application_data,dialplan,context,read_codec,read_rate,read_bit_rate,write_codec,write_rate,write_bit_rate,secure,hostname,presence_id,presence_data,accountcode,callstate,callee_name,callee_num,callee_direction,call_uuid,sent_callee_name,sent_callee_num,initial_cid_name,initial_cid_num,initial_ip_addr,initial_dest,initial_dialplan,initial_context

1 total.
```

**Other useful monitoring commands**:
```
sofia status profile internal
show calls
uuid_dump <call-uuid>
```

---

## Part 6: Advanced Testing

### Test Echo Service

FreeSWITCH includes a built-in echo test:

1. From any registered extension, dial: `9196`
2. Speak into the microphone
3. You should hear your own voice echoed back (with slight delay)
4. This confirms audio path is working correctly

### Test Conference

1. From extension 1000, dial: `3000` (default conference room)
2. You'll hear "You are the only person in this conference"
3. From extension 1001, dial: `3000`
4. Both extensions should now be in conference bridge
5. Test multi-party audio

### Test Voicemail

1. From extension 1000, dial: `*98`
2. When prompted, enter: `1000#` (mailbox) then `1234#` (password)
3. Follow prompts to record a message

### Check Call Detail Records

```bash
docker exec -it freeswitch fs_cli -x "show calls"
```

---

## Part 7: Troubleshooting

### Extension Won't Register

**Check 1: Container is running**
```bash
docker ps | grep freeswitch
# Should show STATUS as "Up X minutes"
```

**Check 2: SIP profile is running**
```bash
docker exec -it freeswitch fs_cli -x "sofia status"
# Should show "internal" profile as RUNNING
```

**Check 3: Port 5060 is accessible**
```bash
# On MacBook
nc -zv localhost 5060
# Should show: Connection to localhost port 5060 [tcp/sip] succeeded!
```

**Check 4: Check SIP registration in fs_cli**
```bash
docker exec -it freeswitch fs_cli -x "sofia status profile internal reg"
# Should show registered users
```

### No Audio During Calls

**Issue**: Calls connect but no audio in one or both directions

**Solution 1: Check RTP ports**
```bash
# Make sure RTP ports are mapped
docker port freeswitch
# Should show 16384-16484/udp
```

**Solution 2: Firewall on MacBook**
- Go to System Preferences → Security & Privacy → Firewall
- Allow Docker to accept incoming connections

**Solution 3: NAT configuration**

If testing from external network, add to FreeSWITCH vars.xml:
```bash
docker exec -it freeswitch vim /usr/local/freeswitch/conf/vars.xml
```

Find and update:
```xml
<X-PRE-PROCESS cmd="set" data="external_rtp_ip=YOUR_PUBLIC_IP"/>
<X-PRE-PROCESS cmd="set" data="external_sip_ip=YOUR_PUBLIC_IP"/>
```

Then reload:
```bash
docker exec -it freeswitch fs_cli -x "reloadxml"
```

### Call Immediately Hangs Up

**Check dialplan**:
```bash
docker exec -it freeswitch fs_cli -x "xml_locate dialplan"
```

Verify extension 1001 exists in default dialplan.

### Container Keeps Restarting

**Check logs**:
```bash
docker logs freeswitch
```

Look for errors like:
- Configuration syntax errors
- Missing files
- Permission issues

### Can't Access fs_cli

**Check Event Socket is running**:
```bash
docker exec -it freeswitch netstat -an | grep 8021
# Should show: tcp  0  0 127.0.0.1:8021  0.0.0.0:*  LISTEN
```

---

## Part 8: Docker Commands Reference

### Container Management

```bash
# Start container
docker start freeswitch

# Stop container
docker stop freeswitch

# Restart container
docker restart freeswitch

# Remove container
docker rm -f freeswitch

# View logs (follow mode)
docker logs -f freeswitch

# View last 100 lines
docker logs --tail 100 freeswitch

# Execute command in container
docker exec -it freeswitch <command>

# Open bash shell in container
docker exec -it freeswitch bash
```

### Image Management

```bash
# List images
docker images | grep freeswitch

# Remove image
docker rmi YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11

# Pull latest version
docker pull YOUR_DOCKERHUB_USERNAME/freeswitch-base:latest

# Check image size
docker images YOUR_DOCKERHUB_USERNAME/freeswitch-base --format "{{.Repository}}:{{.Tag}}\t{{.Size}}"
```

### Network Debugging

```bash
# Check container IP
docker inspect freeswitch | grep IPAddress

# Check port mappings
docker port freeswitch

# Monitor network traffic (requires tcpdump in container)
docker exec -it freeswitch tcpdump -i any port 5060 -n
```

---

## Part 9: Production Considerations

### Persist Configuration and Data

```bash
# Create volumes for persistence
docker volume create freeswitch-conf
docker volume create freeswitch-logs
docker volume create freeswitch-data

# Run with volumes
docker run -d \
    --name freeswitch \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    -v freeswitch-conf:/usr/local/freeswitch/conf \
    -v freeswitch-logs:/usr/local/freeswitch/log \
    -v freeswitch-data:/usr/local/freeswitch/storage \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

### Resource Limits

```bash
# Run with resource constraints
docker run -d \
    --name freeswitch \
    --memory="2g" \
    --cpus="2" \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

### Environment Variables

```bash
# Override default settings
docker run -d \
    --name freeswitch \
    -e FREESWITCH_LOG_LEVEL=DEBUG \
    -e FREESWITCH_RTP_START=10000 \
    -e FREESWITCH_RTP_END=20000 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    YOUR_DOCKERHUB_USERNAME/freeswitch-base:1.10.11
```

---

## Summary Checklist

### Development Environment
- [ ] Build image: `./dockerfiles/build-freeswitch-base.sh`
- [ ] Tag for Docker Hub: `docker tag freeswitch-base:1.10.11 USER/freeswitch-base:1.10.11`
- [ ] Login to Docker Hub: `docker login`
- [ ] Push to Docker Hub: `docker push USER/freeswitch-base:1.10.11`
- [ ] Verify on hub.docker.com

### MacBook
- [ ] Pull image: `docker pull USER/freeswitch-base:1.10.11`
- [ ] Run container with port mappings
- [ ] Verify with `docker ps` and `docker logs`
- [ ] Test fs_cli access

### SIP Testing
- [ ] Install SIP client(s) on MacBook
- [ ] Register extension 1000 (username: 1000, password: 1234)
- [ ] Register extension 1001 (username: 1001, password: 1234)
- [ ] Test call from 1000 to 1001
- [ ] Test call from 1001 to 1000
- [ ] Verify two-way audio
- [ ] Test echo service (dial 9196)
- [ ] Test conference (dial 3000)

### Monitoring
- [ ] Monitor calls in fs_cli: `show channels`
- [ ] Check SIP registrations: `sofia status profile internal reg`
- [ ] View call logs in FreeSWITCH logs

---

## Next Steps

Once calling is working between extensions 1000 and 1001:

1. **Add more extensions** - Create additional SIP users
2. **Configure WebRTC** - Test browser-based calling
3. **Add custom modules** - Integrate your mod_audio_fork and other modules
4. **External calling** - Configure SIP trunks for PSTN calls
5. **Dialplan customization** - Add IVR, call routing, etc.

---

## Support Resources

- **FreeSWITCH Wiki**: https://freeswitch.org/confluence/
- **Docker Hub**: https://hub.docker.com/r/YOUR_DOCKERHUB_USERNAME/freeswitch-base
- **Project Repo**: Your GitHub repository
- **Installation Guide**: See [Appendix A](#appendix-a-complete-freeswitch-installation-guide) for build details

---

**Happy testing!** 🎉📞
