# Dialplan Configuration: Channel Variables for Pusher Integration

## Overview

This document explains how to properly configure FreeSWITCH dialplan to set **all required channel variables** for Pusher integration. These variables are used by `mod_aws_transcribe` and `mod_deepgram_transcribe` to create speaker-identified transcriptions.

## Required Channel Variables

The Pusher integration code (`send_to_pusher` function) requires these channel variables:

| Variable | Source | Usage | Example |
|----------|--------|-------|---------|
| `caller_id_name` | User directory or SIP | Speaker name for Channel 0 (caller) | "John Doe" |
| `caller_id_number` | FreeSWITCH automatic | Speaker number for Channel 0 | "1000" |
| `callee_id_name` | User directory or manual | Speaker name for Channel 1 (callee) | "Jane Smith" |
| `destination_number` | FreeSWITCH automatic | Speaker number for Channel 1 | "1001" |
| `sip_call_id` | FreeSWITCH automatic | Pusher channel naming | "abc123@192.168.1.10" |

### Code Reference

From `mod_deepgram_transcribe.c` and `mod_aws_transcribe.c` (lines 81-88):

```c
// Get caller/callee metadata from channel variables for speaker mapping
switch_channel_t *chan = switch_core_session_get_channel(session);
const char* caller_name = switch_channel_get_variable(chan, "caller_id_name");
const char* caller_number = switch_channel_get_variable(chan, "caller_id_number");
const char* callee_name = switch_channel_get_variable(chan, "callee_id_name");
if (!callee_name) callee_name = switch_channel_get_variable(chan, "effective_callee_id_name");
const char* callee_number = switch_channel_get_variable(chan, "destination_number");
if (!callee_number) callee_number = switch_channel_get_variable(chan, "callee_id_number");
```

## Call Scenarios

### 1. Internal Extension-to-Extension Calls

**Scenario:** Extension 1000 calls Extension 1001

**Variables:**
- `caller_id_name`: Get from caller's user directory → "John Doe"
- `caller_id_number`: Automatic → "1000"
- `callee_id_name`: Get from callee's user directory → "Jane Smith"
- `destination_number`: Automatic → "1001"

**Dialplan Configuration:**

```xml
<extension name="deepgram_conditional" continue="true">
  <condition field="${user_data(${caller_id_number}@${domain_name} var enable_deepgram)}" expression="^true$">
    <condition field="destination_number" expression="^(.+)$">

      <!-- Get caller name from user directory -->
      <action application="set" data="caller_id_name=${user_data(${caller_id_number}@${domain_name} var effective_caller_id_name)}"/>
      <action application="set" data="caller_id_name=${caller_id_name:-(Unknown)}"/>

      <!-- Get callee name from user directory -->
      <action application="set" data="callee_id_name=${user_data($1@${domain_name} var effective_caller_id_name)}"/>
      <action application="set" data="callee_id_name=${callee_id_name:-$1}"/>

      <!-- Start transcription -->
      <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
      <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/>
    </condition>
  </condition>
</extension>
```

**Result in Pusher:**
```json
{
  "type": "final",
  "speaker_id": "John Doe(1000)",
  "text": "Hello Jane",
  "timestamp": "2025-11-22T20:00:00Z"
}
{
  "type": "final",
  "speaker_id": "Jane Smith(1001)",
  "text": "Hi John, how are you?",
  "timestamp": "2025-11-22T20:00:05Z"
}
```

---

### 2. External Outbound Calls (Extension → External Number)

**Scenario:** Extension 1000 calls +15551234567

**Variables:**
- `caller_id_name`: Get from caller's user directory → "John Doe"
- `caller_id_number`: Automatic → "1000"
- `callee_id_name`: Set manually or lookup from CRM → "External +15551234567"
- `destination_number`: Automatic → "+15551234567"

**Dialplan Configuration:**

```xml
<extension name="external_outbound_transcribe" continue="true">
  <condition field="${user_data(${caller_id_number}@${domain_name} var enable_deepgram)}" expression="^true$">
    <!-- Match external numbers: 10-15 digits with optional + prefix -->
    <condition field="destination_number" expression="^(\+?\d{10,15})$">

      <!-- Get caller info from user directory -->
      <action application="set" data="caller_id_name=${user_data(${caller_id_number}@${domain_name} var effective_caller_id_name)}"/>
      <action application="set" data="caller_id_name=${caller_id_name:-(Unknown User)}"/>

      <!-- Set callee info manually or lookup from CRM -->
      <action application="set" data="callee_id_name=External $1"/>

      <!-- Optional: CRM/database lookup -->
      <!-- <action application="set" data="callee_id_name=${odbc(SELECT name FROM contacts WHERE phone='$1')}"/> -->
      <!-- <action application="set" data="callee_id_name=${callee_id_name:-External $1}"/> -->

      <action application="log" data="INFO [EXTERNAL-OUT] ${caller_id_name}(${caller_id_number}) → ${callee_id_name}"/>

      <!-- Start transcription -->
      <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
      <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/>
    </condition>
  </condition>
</extension>
```

**Result in Pusher:**
```json
{
  "type": "final",
  "speaker_id": "John Doe(1000)",
  "text": "Hello, this is John",
  "timestamp": "2025-11-22T20:00:00Z"
}
{
  "type": "final",
  "speaker_id": "External +15551234567",
  "text": "Hi John",
  "timestamp": "2025-11-22T20:00:05Z"
}
```

---

### 3. External Inbound Calls (External Number → Extension)

**Scenario:** +15551234567 calls Extension 1000

**Variables:**
- `caller_id_name`: Already set by SIP INVITE → "Customer Mobile"
- `caller_id_number`: Already set by SIP INVITE → "+15551234567"
- `callee_id_name`: Get from callee's user directory → "John Doe"
- `destination_number`: Automatic → "1000"

**Dialplan Configuration:**

```xml
<extension name="external_inbound_transcribe" continue="true">
  <!-- Match calls to local extensions (1000-1019) -->
  <condition field="destination_number" expression="^(10[01][0-9])$">
    <!-- Check if receiving extension has transcription enabled -->
    <condition field="${user_data($1@${domain_name} var enable_deepgram)}" expression="^true$">

      <!-- Caller info: Already set from SIP INVITE, just ensure not empty -->
      <action application="set" data="caller_id_name=${caller_id_name:-(Unknown External)}"/>

      <!-- Callee info: Get from user directory -->
      <action application="set" data="callee_id_name=${user_data($1@${domain_name} var effective_caller_id_name)}"/>
      <action application="set" data="callee_id_name=${callee_id_name:-Extension $1}"/>

      <action application="log" data="INFO [EXTERNAL-IN] ${caller_id_name}(${caller_id_number}) → ${callee_id_name}($1)"/>

      <!-- Start transcription -->
      <action application="set" data="DEEPGRAM_SPEECH_MODEL=phonecall"/>
      <action application="set" data="DEEPGRAM_SPEECH_TIER=nova"/>
      <action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>
      <action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
      <action application="set" data="api_hangup_hook=uuid_deepgram_transcribe ${uuid} stop"/>

    </condition>
  </condition>
</extension>
```

**Result in Pusher:**
```json
{
  "type": "final",
  "speaker_id": "Customer Mobile(+15551234567)",
  "text": "Hello, I need support",
  "timestamp": "2025-11-22T20:00:00Z"
}
{
  "type": "final",
  "speaker_id": "John Doe(1000)",
  "text": "Sure, how can I help you?",
  "timestamp": "2025-11-22T20:00:05Z"
}
```

---

## User Directory Configuration

Set `effective_caller_id_name` in user directory files to ensure proper speaker names.

**Example: `/usr/local/freeswitch/conf/directory/default/1000.xml`**

```xml
<user id="1000">
  <params>
    <param name="password" value="$${default_password}"/>
  </params>
  <variables>
    <variable name="toll_allow" value="domestic,international,local"/>
    <variable name="accountcode" value="1000"/>
    <variable name="user_context" value="default"/>

    <!-- ✅ REQUIRED FOR PUSHER: Speaker name -->
    <variable name="effective_caller_id_name" value="John Doe"/>
    <variable name="effective_caller_id_number" value="1000"/>

    <!-- Enable transcription services -->
    <variable name="enable_deepgram" value="true"/>
    <!-- <variable name="enable_aws_transcribe" value="true"/> -->
  </variables>
</user>
```

---

## Verification & Testing

### 1. Check Channel Variables During Call

In `fs_cli`, during an active call:

```bash
# Get the UUID of the call
freeswitch@localhost> show channels

# Dump all variables for that channel
freeswitch@localhost> uuid_dump <uuid>

# Or grep for specific variables
freeswitch@localhost> uuid_dump <uuid> | grep caller_id_name
freeswitch@localhost> uuid_dump <uuid> | grep callee_id_name
freeswitch@localhost> uuid_dump <uuid> | grep destination_number
freeswitch@localhost> uuid_dump <uuid> | grep sip_call_id
```

**Expected Output:**
```
variable_caller_id_name: John Doe
variable_caller_id_number: 1000
variable_callee_id_name: Jane Smith
variable_destination_number: 1001
variable_sip_call_id: abc123xyz789@192.168.1.10
```

### 2. Check FreeSWITCH Logs

Enable INFO logging to see speaker information:

```bash
freeswitch@localhost> fsctl loglevel info
```

Look for log entries:
```
[INFO] [DEEPGRAM] John Doe(1000) → Jane Smith(1001) - Starting Deepgram
[INFO] [EXTERNAL-OUT] John Doe(1000) calling external External +15551234567
[INFO] [EXTERNAL-IN] External Customer Mobile(+15551234567) → John Doe(1000)
```

### 3. Monitor Pusher Events

Use Pusher debug console or monitor HTTP requests to verify JSON format:

**Expected Pusher Channel:** `call-abc123xyz789@192.168.1.10`

**Expected Event Name:**
- `transcription-interim` for interim results
- `transcription-final` for final results

**Expected Data:**
```json
{
  "type": "final",
  "speaker_id": "John Doe(1000)",
  "text": "Hello, how are you?",
  "timestamp": "2025-11-22T20:00:00Z"
}
```

---

## Troubleshooting

### ❌ Problem: Pusher shows "Unknown(1000)" instead of "John Doe(1000)"

**Cause:** `caller_id_name` or `callee_id_name` not set in dialplan

**Solution:** Add speaker info lookup in dialplan:
```xml
<action application="set" data="caller_id_name=${user_data(${caller_id_number}@${domain_name} var effective_caller_id_name)}"/>
```

---

### ❌ Problem: External calls show numbers instead of names

**Cause:** External numbers don't have user directory entries

**Solution:** Set descriptive names in dialplan:
```xml
<!-- For outbound -->
<action application="set" data="callee_id_name=External $1"/>

<!-- For inbound -->
<action application="set" data="caller_id_name=${caller_id_name:-(Unknown External)}"/>
```

Or integrate with CRM for name lookups:
```xml
<action application="set" data="callee_id_name=${odbc(SELECT name FROM contacts WHERE phone='$1')}"/>
```

---

### ❌ Problem: All transcripts attributed to same speaker

**Cause:** Stereo mode not enabled, or channel identification disabled

**Solution for AWS:**
```xml
<action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
<action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
```

**Solution for Deepgram:**
```xml
<action application="set" data="DEEPGRAM_SPEECH_DIARIZE=true"/>
```

**Start command must use `stereo`:**
```xml
<action application="set" data="api_on_answer=uuid_deepgram_transcribe ${uuid} start en-US interim stereo"/>
```

---

## Summary

### ✅ Checklist for Pusher Integration

1. **User Directory:**
   - [ ] Set `effective_caller_id_name` for all extensions
   - [ ] Enable transcription: `<variable name="enable_deepgram" value="true"/>`

2. **Dialplan:**
   - [ ] Set `caller_id_name` from user directory
   - [ ] Set `callee_id_name` from user directory or manually
   - [ ] Use stereo mode: `stereo` parameter in start command
   - [ ] Enable channel identification (AWS) or diarization (Deepgram)

3. **Verification:**
   - [ ] Check channel variables during call: `uuid_dump <uuid>`
   - [ ] Verify speaker names in FreeSWITCH logs
   - [ ] Confirm correct JSON format in Pusher

### 📊 Channel Variables Summary

| Call Type | `caller_id_name` | `caller_id_number` | `callee_id_name` | `destination_number` |
|-----------|------------------|-------------------|------------------|----------------------|
| **Internal** | User directory | Auto | User directory | Auto |
| **External Out** | User directory | Auto | Manual/CRM | Auto |
| **External In** | SIP INVITE | SIP INVITE | User directory | Auto |

---

## Related Documentation

- [SPEAKER_DETECTION_COMPARISON.md](./SPEAKER_DETECTION_COMPARISON.md) - Complete speaker detection implementation details
- [PER_USER_MULTI_SERVICE.md](./PER_USER_MULTI_SERVICE.md) - Per-user service configuration
- [USER_DIRECTORY_CONFIG.md](./USER_DIRECTORY_CONFIG.md) - User directory setup guide
