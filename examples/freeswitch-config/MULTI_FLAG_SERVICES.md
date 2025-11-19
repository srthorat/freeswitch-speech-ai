# Multi-Flag Per-User Service Configuration

This guide shows how to enable different services (Audio Fork, Deepgram, Azure) on a **per-user basis** using flags.

## Overview

Enable different services for different users:
- **User 1000**: Audio Fork (WebSocket streaming) only
- **User 1001**: Deepgram transcription only
- **User 1003**: Azure transcription only
- **User 1004**: ALL services enabled

## How It Works

1. **User directory files** contain service flags (`enable_audio_fork`, `enable_deepgram`, `enable_azure`)
2. **Dialplan** checks these flags and starts the appropriate services
3. **All services start AFTER call is answered** (using `api_on_answer`)

## Installation

### Step 1: Modify `/usr/local/freeswitch/conf/dialplan/default.xml`

Add the multi-flag extensions **at the top** of the `<context name="default">` section:

```xml
<include>
  <context name="default">

    <!-- MULTI-FLAG SERVICE EXTENSIONS -->

    <!-- EXTENSION 1: Audio Fork (WebSocket Streaming) -->
    <extension name="audio_fork_conditional" continue="true">
      <condition field="${enable_audio_fork}" expression="^true$">
        <condition field="destination_number" expression="^(.+)$">
          <action application="log" data="INFO [AUDIO_FORK] User ${caller_id_number} has audio fork enabled → ${destination_number}"/>
          <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}','service':'audio_fork'}"/>
          <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
        </condition>
      </condition>
    </extension>

    <!-- EXTENSION 2: Deepgram Transcription -->
    <extension name="deepgram_conditional" continue="true">
      <condition field="${enable_deepgram}" expression="^true$">
        <condition field="destination_number" expression="^(.+)$">
          <action application="log" data="INFO [DEEPGRAM] User ${caller_id_number} has Deepgram enabled → ${destination_number}"/>
          <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
          <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/>
        </condition>
      </condition>
    </extension>

    <!-- EXTENSION 3: Azure Transcription -->
    <extension name="azure_conditional" continue="true">
      <condition field="${enable_azure}" expression="^true$">
        <condition field="destination_number" expression="^(.+)$">
          <action application="log" data="INFO [AZURE] User ${caller_id_number} has Azure enabled → ${destination_number}"/>
          <action application="set" data="api_on_answer=azure_transcribe ${uuid} start en-US interim"/>
          <action application="set" data="api_hangup_hook=azure_transcribe ${uuid} stop"/>
        </condition>
      </condition>
    </extension>

    <!-- Your other extensions below -->
    <extension name="unloop">
      ...
    </extension>

  </context>
</include>
```

**Important:**
- Remove any existing global `audio_fork_all_calls` extension
- Add these extensions **before** the `<X-PRE-PROCESS cmd="include"...>` line

### Step 2: Configure User Directory Files

#### User 1000 - Audio Fork Only

**File:** `/usr/local/freeswitch/conf/directory/default/1000.xml`

```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1000"/>
      <variable name="effective_caller_id_number" value="1000"/>

      <!-- Enable ONLY audio fork for this user -->
      <variable name="enable_audio_fork" value="true"/>
    </variables>
  </user>
</include>
```

#### User 1001 - Deepgram Only

**File:** `/usr/local/freeswitch/conf/directory/default/1001.xml`

```xml
<include>
  <user id="1001">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1001"/>
      <variable name="effective_caller_id_number" value="1001"/>

      <!-- Enable ONLY Deepgram for this user -->
      <variable name="enable_deepgram" value="true"/>

      <!-- Deepgram Configuration (optional if using environment vars) -->
      <variable name="DEEPGRAM_API_KEY" value="your-deepgram-api-key"/>
      <variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
      <variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
    </variables>
  </user>
</include>
```

#### User 1003 - Azure Only

**File:** `/usr/local/freeswitch/conf/directory/default/1003.xml`

```xml
<include>
  <user id="1003">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1003"/>
      <variable name="effective_caller_id_number" value="1003"/>

      <!-- Enable ONLY Azure for this user -->
      <variable name="enable_azure" value="true"/>

      <!-- Azure Configuration (optional if using environment vars) -->
      <variable name="AZURE_SUBSCRIPTION_KEY" value="your-azure-key"/>
      <variable name="AZURE_REGION" value="eastus"/>
    </variables>
  </user>
</include>
```

#### User 1004 - ALL Services

**File:** `/usr/local/freeswitch/conf/directory/default/1004.xml`

```xml
<include>
  <user id="1004">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1004"/>
      <variable name="effective_caller_id_number" value="1004"/>

      <!-- Enable ALL services for this user -->
      <variable name="enable_audio_fork" value="true"/>
      <variable name="enable_deepgram" value="true"/>
      <variable name="enable_azure" value="true"/>

      <!-- Service Configuration -->
      <variable name="DEEPGRAM_API_KEY" value="your-deepgram-api-key"/>
      <variable name="AZURE_SUBSCRIPTION_KEY" value="your-azure-key"/>
      <variable name="AZURE_REGION" value="eastus"/>
    </variables>
  </user>
</include>
```

### Step 3: Reload Configuration

```bash
fs_cli -x 'reloadxml'
```

## Testing

### Test User 1000 (Audio Fork Only)

```bash
# User 1000 calls 1001
# Expected logs:
[INFO] [AUDIO_FORK] User 1000 has audio fork enabled → 1001
mod_audio_fork: streaming 16000 sampling to 20.244.30.42:8077/stream

# NO Deepgram or Azure logs
```

### Test User 1001 (Deepgram Only)

```bash
# User 1001 calls 1000
# Expected logs:
[INFO] [DEEPGRAM] User 1001 has Deepgram enabled → 1000
# Deepgram transcription events should appear

# NO Audio Fork or Azure logs
```

### Test User 1003 (Azure Only)

```bash
# User 1003 calls 1000
# Expected logs:
[INFO] [AZURE] User 1003 has Azure enabled → 1000
# Azure transcription events should appear

# NO Audio Fork or Deepgram logs
```

### Test User 1004 (ALL Services)

```bash
# User 1004 calls 1000
# Expected logs:
[INFO] [AUDIO_FORK] User 1004 has audio fork enabled → 1000
[INFO] [DEEPGRAM] User 1004 has Deepgram enabled → 1000
[INFO] [AZURE] User 1004 has Azure enabled → 1000

# ALL three services start!
```

## Verification Commands

### Check user flags

```bash
# Check what services user 1000 has enabled
fs_cli -x 'user_data 1000@default var enable_audio_fork'
# Should return: true

fs_cli -x 'user_data 1001@default var enable_deepgram'
# Should return: true

fs_cli -x 'user_data 1003@default var enable_azure'
# Should return: true
```

### Check active services on a call

```bash
# Get active calls
fs_cli -x 'show channels'

# Check what's attached to a call
fs_cli -x 'uuid_buglist <uuid>'
# Should show: audio_fork, deepgram_transcribe, or azure_transcribe
```

## Configuration Matrix

| User | Audio Fork | Deepgram | Azure | Use Case |
|------|-----------|----------|-------|----------|
| 1000 | ✅ | ❌ | ❌ | WebSocket streaming only |
| 1001 | ❌ | ✅ | ❌ | Deepgram transcription only |
| 1003 | ❌ | ❌ | ✅ | Azure transcription only |
| 1004 | ✅ | ✅ | ✅ | All services (testing/demo) |

## Adding More Services

To add a new service (e.g., `enable_google_transcribe`):

### 1. Add extension to dialplan

```xml
<extension name="google_conditional" continue="true">
  <condition field="${enable_google}" expression="^true$">
    <condition field="destination_number" expression="^(.+)$">
      <action application="log" data="INFO [GOOGLE] User ${caller_id_number} has Google enabled"/>
      <action application="set" data="api_on_answer=uuid_google_transcribe ${uuid} start en-US"/>
      <action application="set" data="api_hangup_hook=uuid_google_transcribe ${uuid} stop"/>
    </condition>
  </condition>
</extension>
```

### 2. Add flag to user files

```xml
<variable name="enable_google" value="true"/>
<variable name="GOOGLE_APPLICATION_CREDENTIALS" value="/path/to/creds.json"/>
```

That's it! The pattern is completely extensible.

## Service Configuration

### Audio Fork Settings

Configure WebSocket URL in dialplan (`default.xml`):

```xml
<action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://YOUR_SERVER:PORT/stream stereo 16k ..."/>
```

- **WebSocket URL**: `ws://20.244.30.42:8077/stream`
- **Mix type**: `stereo`, `mono`, or `mixed`
- **Sampling rate**: `16k` or `8k`

### Deepgram Settings

Configure in dialplan or user files:

**In dialplan (global):**
```xml
<action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
```

**In user file (per-user):**
```xml
<variable name="DEEPGRAM_API_KEY" value="your-api-key"/>
<variable name="DEEPGRAM_SPEECH_MODEL" value="phonecall"/>
<variable name="DEEPGRAM_SPEECH_TIER" value="nova"/>
```

### Azure Settings

Configure in dialplan or user files:

**In dialplan (global):**
```xml
<action application="set" data="api_on_answer=azure_transcribe ${uuid} start en-US interim"/>
```

**In user file (per-user):**
```xml
<variable name="AZURE_SUBSCRIPTION_KEY" value="your-key"/>
<variable name="AZURE_REGION" value="eastus"/>
```

## Troubleshooting

### Service not starting for enabled user

1. **Check flag is set:**
   ```bash
   fs_cli -x 'user_data 1000@default var enable_audio_fork'
   ```

2. **Check dialplan loaded:**
   ```bash
   fs_cli -x 'xml_locate dialplan' | grep -i "audio_fork_conditional"
   ```

3. **Enable debug logging:**
   ```bash
   fs_cli -x 'console loglevel 7'
   # Make a call and watch for [AUDIO_FORK], [DEEPGRAM], [AZURE] logs
   ```

### Multiple services conflicting

If running multiple transcription services causes issues:
- Only enable one transcription service per user (Deepgram OR Azure, not both)
- Audio Fork can run alongside transcription services

### API keys not working

**For Deepgram/Azure:**
1. Set keys in user file OR environment variables
2. Module checks user file first, then environment variables
3. Verify keys:
   ```bash
   fs_cli -x 'user_data 1001@default var DEEPGRAM_API_KEY'
   ```

## Benefits

✅ **Flexible per-user control** - Each user can have different services
✅ **Extensible** - Easy to add new services (just copy the pattern)
✅ **No code changes** - Pure XML configuration
✅ **Mix and match** - Users can have multiple services enabled
✅ **Centralized** - Service settings in dialplan, flags in user files
✅ **Scalable** - Works with thousands of users

## Related Documentation

- [Audio Fork README](../../modules/mod_audio_fork/README.md)
- [Deepgram README](../../modules/mod_deepgram_transcribe/README.md)
- [Azure README](../../modules/mod_azure_transcribe/README.md)
- [Single-Flag Configuration](AUDIO_FORK_PER_USER_FLAG.md)
