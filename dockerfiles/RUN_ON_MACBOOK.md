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
