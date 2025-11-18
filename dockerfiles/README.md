# FreeSWITCH Docker Images

This directory contains Dockerfiles for building FreeSWITCH images with different configurations:

1. **Base Image** - Complete FreeSWITCH installation with all standard modules
2. **Individual Module Images** - Minimal FreeSWITCH with specific modules for testing

---

## 🚀 Quick Start: Running on MacBook

**Want to quickly test on your MacBook?** See the comprehensive guide:

📘 **[RUN_ON_MACBOOK.md](RUN_ON_MACBOOK.md)** - Complete MacBook setup guide with examples

```bash
# Example: Run with Deepgram transcription
./dockerfiles/run-on-macbook.sh \
  srt2011/freeswitch-mod-deepgram-transcribe:latest \
  YOUR_DEEPGRAM_API_KEY
```

---

## 1. FreeSWITCH Base Image (Recommended Starting Point)

**Files**:
- Dockerfile: `Dockerfile.freeswitch-base`
- Build Script: `build-freeswitch-base.sh`
- Detailed Install Guide: `FREESWITCH_INSTALL.md`
- Deployment Guide: `DOCKER_HUB_DEPLOYMENT.md`

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

### Manual Verification for mod_audio_fork

After building or pulling the mod_audio_fork image, verify it manually:

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

### Manual Verification for mod_deepgram_transcribe

After building or pulling the mod_deepgram_transcribe image, verify it manually:

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

### Method 1: Environment Variables (Container-Wide)

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

### Method 2: Dialplan Configuration (Automatic Transcription)

Add to `/usr/local/freeswitch/conf/dialplan/default.xml` or create a new file in `/usr/local/freeswitch/conf/dialplan/default/`:

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

### Method 3: fs_cli Commands (Manual Per-Call Control)

Control transcription manually for specific calls using fs_cli:

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

### Method 4: FreeSWITCH Service Configuration (Supervised/Systemd)

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
- ✅ Profanity filtering (masked, removed, raw modes)
- ✅ Detailed output with N-best alternatives and confidence scores
- ✅ Signal-to-noise ratio (SNR) reporting
- ✅ Speech hints for improved domain-specific recognition
- ✅ Configurable timeout settings
- ✅ Supports 50+ languages and dialects
- ✅ Interim and final transcription results
- ✅ Includes mod_audio_fork AND mod_deepgram_transcribe from base image
- ✅ Automatic static + runtime validation during build

### Manual Verification for mod_azure_transcribe

After building or pulling the mod_azure_transcribe image, verify it manually:

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
uuid_setvar <call-uuid> AZURE_SUBSCRIPTION_KEY your-subscription-key
uuid_setvar <call-uuid> AZURE_REGION eastus
uuid_setvar <call-uuid> AZURE_USE_OUTPUT_FORMAT_DETAILED true
uuid_setvar <call-uuid> AZURE_PROFANITY_OPTION masked

# Start transcription with interim results
azure_transcribe <call-uuid> start en-US interim

# Stop transcription
azure_transcribe <call-uuid> stop
```

For full API documentation, see: `modules/mod_azure_transcribe/README.md`

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

- [FreeSWITCH Documentation](https://freeswitch.org/confluence/)
- [Installation Guide](FREESWITCH_INSTALL.md) - Complete dependency guide with all errors documented
- [Docker Hub Deployment](DOCKER_HUB_DEPLOYMENT.md) - Deploy and test on MacBook with SIP clients
- [Main Repository README](../README.md)
- [Individual Module READMEs](../modules/)
