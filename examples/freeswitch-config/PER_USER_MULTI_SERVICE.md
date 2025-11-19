# Per-User Multi-Service Configuration

This guide shows how to enable different services (Audio Fork, Deepgram, Azure) on a **per-user basis** using flags in user directory files.

## Overview

Enable different services for different users:
- **User 1000**: Audio Fork (WebSocket streaming) only
- **User 1001**: Deepgram transcription only
- **User 1003**: Azure transcription only

**Note:** Users can have multiple flags enabled (e.g., both `enable_audio_fork` and `enable_deepgram`). Fallback logic for service selection will be explored in future releases.

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
          <action application="set" data="DEEPGRAM_API_KEY=your-deepgram-api-key"/>
          <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
          <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
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
          <action application="set" data="AZURE_SUBSCRIPTION_KEY=your-azure-key"/>
          <action application="set" data="AZURE_REGION=eastus"/>
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
      <!-- All audio fork settings (WebSocket URL, mix type) are configured in dialplan -->
      <variable name="enable_audio_fork" value="true"/>
    </variables>
  </user>
</include>
```

**Note:** User file contains ONLY the flag. All service settings (WebSocket URL, mix type, sampling rate) are centralized in dialplan.

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
      <!-- All Deepgram settings (API key, model, tier) are configured in dialplan -->
      <variable name="enable_deepgram" value="true"/>
    </variables>
  </user>
</include>
```

**Note:** User file contains ONLY the flag. All Deepgram settings (API key, model, tier) are centralized in dialplan.

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
      <!-- All Azure settings (subscription key, region) are configured in dialplan -->
      <variable name="enable_azure" value="true"/>
    </variables>
  </user>
</include>
```

**Note:** User file contains ONLY the flag. All Azure settings (subscription key, region) are centralized in dialplan.

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

**Note:** Users can enable multiple services by setting multiple flags. However, running multiple transcription services simultaneously (Deepgram + Azure) is generally not recommended.

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
<!-- User file contains ONLY the flag -->
<variable name="enable_google" value="true"/>
```

### 3. Add service configuration to dialplan

```xml
<!-- Set service configuration in dialplan (centralized) -->
<action application="set" data="GOOGLE_APPLICATION_CREDENTIALS=/path/to/creds.json"/>
```

That's it! The pattern is completely extensible. User files contain only flags, all settings in dialplan.

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

All Deepgram configuration is centralized in dialplan:

**In dialplan (default.xml):**
```xml
<action application="set" data="DEEPGRAM_API_KEY=your-deepgram-api-key"/>
<action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
<action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
<action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
```

To change API key or settings, edit the values in dialplan. No need to modify user files!

### Azure Settings

All Azure configuration is centralized in dialplan:

**In dialplan (default.xml):**
```xml
<action application="set" data="AZURE_SUBSCRIPTION_KEY=your-azure-key"/>
<action application="set" data="AZURE_REGION=eastus"/>
<action application="set" data="api_on_answer=azure_transcribe ${uuid} start en-US interim"/>
```

To change subscription key or region, edit the values in dialplan. No need to modify user files!

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
1. Keys are configured in dialplan (default.xml) using `<action application="set">`
2. These settings apply to all users with the service flag enabled
3. Alternative: Use environment variables instead (module checks environment vars if channel vars not set)
4. To verify keys are being set during calls, check FreeSWITCH logs for the service startup messages

## Benefits

✅ **Flexible per-user control** - Each user can have different services (via flags)
✅ **Centralized configuration** - All service settings (API keys, URLs) in ONE file (dialplan)
✅ **Clean user files** - User files contain ONLY flags, no sensitive data
✅ **Easy management** - Change API key once in dialplan, applies to all users
✅ **Extensible** - Easy to add new services (just copy the pattern)
✅ **No code changes** - Pure XML configuration
✅ **Mix and match** - Users can have multiple services enabled
✅ **Scalable** - Works with thousands of users
✅ **Secure** - No API keys scattered across multiple user files

## Related Documentation

- [Audio Fork README](../../modules/mod_audio_fork/README.md)
- [Deepgram README](../../modules/mod_deepgram_transcribe/README.md)
- [Azure README](../../modules/mod_azure_transcribe/README.md)
- [Complete Dialplan Example](dialplan/default.xml.complete-example)
- [User Directory Examples](directory/README.md)
