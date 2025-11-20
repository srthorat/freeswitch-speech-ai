# FreeSWITCH Stereo Channel Assignment Guide

## Table of Contents
1. [Overview](#overview)
2. [FreeSWITCH Channel Assignment Rules](#freeswitch-channel-assignment-rules)
3. [Call Scenarios](#call-scenarios)
4. [The Problem: Inconsistent Assignment](#the-problem-inconsistent-assignment)
5. [Solutions](#solutions)
6. [Practical Implementation](#practical-implementation)
7. [Testing and Verification](#testing-and-verification)
8. [Troubleshooting](#troubleshooting)

---

## Overview

When using FreeSWITCH transcription modules in stereo mode, understanding channel assignment is **critical** for:
- Accurate speaker identification
- Consistent analytics and reporting
- Proper agent/customer labeling
- Downstream processing and AI training

This guide explains how FreeSWITCH assigns audio streams to left/right channels and how to ensure consistent assignment across different call scenarios.

---

## FreeSWITCH Channel Assignment Rules

### The "God's Rule"

FreeSWITCH has a **guaranteed** channel assignment when using `SMBF_STEREO` flag:

```
DEFAULT (without SMBF_STEREO_SWAP):
├─ Channel 0 (Left)  = WRITE_STREAM = Local party (session owner)
└─ Channel 1 (Right) = READ_STREAM  = Remote party (other end)

WITH SMBF_STEREO_SWAP:
├─ Channel 0 (Left)  = READ_STREAM  = Remote party
└─ Channel 1 (Right) = WRITE_STREAM = Local party
```

### Stream Terminology

| FreeSWITCH Term | Meaning | Audio Direction |
|-----------------|---------|-----------------|
| **READ_STREAM** | Audio **read from** the channel | Remote party → FreeSWITCH |
| **WRITE_STREAM** | Audio **written to** the channel | FreeSWITCH → Remote party (or local party speaking) |

**Important:** Channel assignment is **relative to the session** where the media bug is attached, not necessarily who initiated the call.

---

## Call Scenarios

### Scenario 1: Agent Calls Customer (Outbound)

**Call Flow:**
```
Agent (internal extension 1001) → Dials → Customer (external number +18005551234)
```

**Media bug attached to Agent's session (a-leg):**

```
Default Assignment (no SWAP):
├─ Channel 0 (Left)  = Agent (WRITE_STREAM - local to agent's session)
└─ Channel 1 (Right) = Customer (READ_STREAM - remote to agent's session)
```

**Why this happens:**
- The dialplan executes on the agent's leg
- Agent is "local" to this session
- Customer is "remote" to this session

---

### Scenario 2: Customer Calls Agent (Inbound)

**Call Flow:**
```
Customer (external number +15551234567) → Dials → Agent (extension 1001)
```

**Media bug attached to Customer's session (a-leg):**

```
Default Assignment (no SWAP):
├─ Channel 0 (Left)  = Customer (WRITE_STREAM - local to customer's session)
└─ Channel 1 (Right) = Agent (READ_STREAM - remote to customer's session)
```

**Why this happens:**
- The dialplan executes on the customer's leg (first leg to enter)
- Customer is "local" to this session
- Agent is "remote" to this session

**⚠️ Problem: Channels are REVERSED compared to outbound calls!**

---

## The Problem: Inconsistent Assignment

### Without Channel Management

| Call Direction | Channel 0 (Left) | Channel 1 (Right) | Consistent? |
|----------------|------------------|-------------------|-------------|
| Agent → Customer | Agent | Customer | ✓ |
| Customer → Agent | Customer | Agent | ✗ **REVERSED!** |

### Impact on Analytics

```json
// Outbound call transcript
{
  "channel_id": "ch_0",
  "speaker": "Agent",
  "transcript": "Thank you for calling, how can I help?"
}

// Inbound call transcript (INCONSISTENT!)
{
  "channel_id": "ch_0",
  "speaker": "Customer",  // ← Should be Agent for consistency!
  "transcript": "Hi, I need help with my account"
}
```

**This breaks:**
- Agent performance analytics
- Customer sentiment analysis
- Automated quality assurance
- Training data labeling

---

## Solutions

### Solution 1: Use RECORD_STEREO_SWAP for Inbound Calls ⭐ RECOMMENDED

Swap channels for inbound calls to match outbound call assignment.

**Dialplan Configuration:**

```xml
<!-- INBOUND: Customer calls Agent (swap channels) -->
<extension name="inbound_with_transcription">
  <condition field="destination_number" expression="^(100[0-9])$">
    <action application="log" data="INFO Inbound call - swapping stereo channels for consistency"/>

    <!-- ⭐ KEY: Swap channels so agent is always on left -->
    <action application="set" data="RECORD_STEREO_SWAP=true"/>

    <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
    <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
    <action application="bridge" data="user/$1"/>
  </condition>
</extension>

<!-- OUTBOUND: Agent calls Customer (no swap) -->
<extension name="outbound_with_transcription">
  <condition field="destination_number" expression="^(1800\d+|\\+1\d{10})$">
    <action application="log" data="INFO Outbound call - default stereo assignment"/>

    <!-- No SWAP needed - default assignment is correct -->
    <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
    <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
    <action application="bridge" data="sofia/external/$1@gateway"/>
  </condition>
</extension>
```

**Result: Consistent Assignment ✓**

| Call Direction | SWAP? | Channel 0 (Left) | Channel 1 (Right) |
|----------------|-------|------------------|-------------------|
| Agent → Customer | No | Agent | Customer |
| Customer → Agent | **Yes** | Agent | Customer |

---

### Solution 2: Always Attach to Agent's Session

Attach the media bug to the agent's session (b-leg) for inbound calls.

**Dialplan Configuration:**

```xml
<!-- INBOUND: Attach to agent's leg after bridge -->
<extension name="inbound_attach_to_agent">
  <condition field="destination_number" expression="^(100[0-9])$">
    <action application="log" data="INFO Inbound call - will attach to agent's session"/>

    <!-- Wait for bridge, then attach to b-leg (agent) -->
    <action application="export" data="nolocal:api_on_bridge=uuid_aws_transcribe ${bridge_uuid} start en-US interim stereo"/>
    <action application="bridge" data="user/$1"/>
  </condition>
</extension>

<!-- OUTBOUND: Attach to agent's leg (a-leg) -->
<extension name="outbound_attach_to_agent">
  <condition field="destination_number" expression="^(1800\d+)$">
    <action application="log" data="INFO Outbound call - attaching to agent's session"/>

    <!-- Agent initiates, attach to a-leg (agent) -->
    <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
    <action application="bridge" data="sofia/external/$1@gateway"/>
  </condition>
</extension>
```

**Pros:**
- Bug always attached from agent's perspective
- Natural channel assignment

**Cons:**
- Small delay waiting for bridge
- Misses pre-bridge audio (IVR, announcements)

---

### Solution Comparison

| Solution | Pros | Cons | Recommended For |
|----------|------|------|-----------------|
| **SWAP for inbound** | ✓ Simple<br>✓ Immediate<br>✓ Captures pre-bridge audio | Requires conditional logic | **Call centers** (most common) |
| **Attach to b-leg** | ✓ Natural assignment<br>✓ No swap logic | ✗ Misses pre-bridge audio<br>✗ Requires bridge | Peer-to-peer calls |

---

## Practical Implementation

### Complete Call Center Dialplan

```xml
<include>
  <!--
    STEREO CHANNEL ASSIGNMENT STRATEGY:
    - Agent is ALWAYS on Channel 0 (Left)
    - Customer is ALWAYS on Channel 1 (Right)
    - This is achieved by swapping channels for INBOUND calls
  -->

  <!-- INBOUND: Customer → Agent -->
  <extension name="inbound_transcribe" continue="true">
    <condition field="${user_data(${destination_number}@${domain_name} var enable_aws_transcribe)}" expression="^true$">
      <condition field="destination_number" expression="^(100[0-9])$">
        <action application="log" data="INFO [AWS] Inbound call to agent ${destination_number} - enabling transcription with channel swap"/>

        <!-- Mark call direction for analytics -->
        <action application="set" data="CALL_DIRECTION=inbound"/>
        <action application="set" data="CALL_AGENT=${destination_number}"/>
        <action application="set" data="CALL_CUSTOMER=${caller_id_number}"/>

        <!-- SWAP channels: Customer→Right, Agent→Left -->
        <action application="set" data="RECORD_STEREO_SWAP=true"/>

        <!-- AWS Transcribe configuration -->
        <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
        <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
        <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
        <action application="set" data="api_hangup_hook=uuid_aws_transcribe ${uuid} stop"/>
      </condition>
    </condition>
  </extension>

  <!-- OUTBOUND: Agent → Customer -->
  <extension name="outbound_transcribe" continue="true">
    <condition field="${user_data(${caller_id_number}@${domain_name} var enable_aws_transcribe)}" expression="^true$">
      <condition field="destination_number" expression="^(1800\d+|\\+1\d{10})$">
        <action application="log" data="INFO [AWS] Outbound call from agent ${caller_id_number} - enabling transcription (default assignment)"/>

        <!-- Mark call direction for analytics -->
        <action application="set" data="CALL_DIRECTION=outbound"/>
        <action application="set" data="CALL_AGENT=${caller_id_number}"/>
        <action application="set" data="CALL_CUSTOMER=${destination_number}"/>

        <!-- NO SWAP: Default assignment is correct (Agent→Left, Customer→Right) -->

        <!-- AWS Transcribe configuration -->
        <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
        <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
        <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
        <action application="set" data="api_hangup_hook=uuid_aws_transcribe ${uuid} stop"/>
      </condition>
    </condition>
  </extension>
</include>
```

### User Directory Configuration

Enable transcription per user:

```xml
<!-- /usr/local/freeswitch/conf/directory/default/1003.xml -->
<include>
  <user id="1003">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Agent 1003"/>
      <variable name="effective_caller_id_number" value="1003"/>

      <!-- Enable AWS Transcribe for this agent -->
      <variable name="enable_aws_transcribe" value="true"/>
    </variables>
  </user>
</include>
```

---

## Testing and Verification

### Method 1: Check Transcription Output

Make test calls and verify channel assignment:

**Outbound Test:**
```bash
# From fs_cli
originate user/1003 18005551234

# Expected result in transcription:
# ch_0: "Hello, this is agent speaking" (Agent)
# ch_1: "Thank you for calling" (Customer IVR)
```

**Inbound Test:**
```bash
# Call from external number to extension 1003

# Expected result in transcription:
# ch_0: "Hello, how can I help?" (Agent)
# ch_1: "Hi, I need support" (Customer)
```

### Method 2: Test SWAP Toggle

Temporarily disable SWAP to see reversal:

```xml
<!-- Comment out SWAP for testing -->
<!-- <action application="set" data="RECORD_STEREO_SWAP=true"/> -->
```

Make inbound call and verify channels are reversed.

### Method 3: Use Logging

Add detailed logging to track assignment:

```xml
<action application="log" data="NOTICE === STEREO CHANNEL ASSIGNMENT DEBUG ==="/>
<action application="log" data="NOTICE Call Direction: ${CALL_DIRECTION}"/>
<action application="log" data="NOTICE Agent: ${CALL_AGENT}"/>
<action application="log" data="NOTICE Customer: ${CALL_CUSTOMER}"/>
<action application="log" data="NOTICE SWAP Enabled: ${RECORD_STEREO_SWAP}"/>
<action application="log" data="NOTICE Session UUID: ${uuid}"/>
<action application="log" data="NOTICE =========================================="/>
```

---

## Troubleshooting

### Problem: Channels Still Reversed

**Symptoms:**
- Agent appears on wrong channel
- Analytics reports are inconsistent

**Possible Causes:**

1. **SWAP not being set:**
   ```bash
   # Check in fs_cli
   uuid_dump <call-uuid>

   # Look for: variable_RECORD_STEREO_SWAP: true
   ```

2. **Wrong dialplan matching:**
   - Verify regex patterns match your number format
   - Check dialplan execution order

3. **Bug attached to wrong leg:**
   - If using `api_on_bridge`, ensure `${bridge_uuid}` is correct

**Solution:**
```xml
<!-- Add explicit verification -->
<action application="info"/>  <!-- Dumps all variables to log -->
<action application="log" data="ERR RECORD_STEREO_SWAP is: ${RECORD_STEREO_SWAP}"/>
```

---

### Problem: Transcription Not Starting

**Symptoms:**
- No transcription events
- No API connection logs

**Check:**

1. **Module loaded:**
   ```bash
   # In fs_cli
   module_exists mod_aws_transcribe
   ```

2. **Credentials set:**
   ```bash
   # Check environment variables
   uuid_dump <uuid> | grep AWS
   ```

3. **API called:**
   ```bash
   # Watch logs
   console loglevel debug

   # Should see:
   # "mod_aws_transcribe: starting transcription"
   ```

---

### Problem: Only One Channel Transcribed

**Symptoms:**
- Only `ch_0` or `ch_1` appears in results
- Missing one participant's audio

**Possible Causes:**

1. **Stereo flag not set:**
   ```xml
   <!-- WRONG: Missing "stereo" parameter -->
   <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim"/>

   <!-- CORRECT: -->
   <action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
   ```

2. **Channel identification not enabled:**
   ```xml
   <action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
   <action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
   ```

---

## Vendor-Specific Notes

### AWS Transcribe

**Channel Identification:**
```xml
<action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
<action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
```

**Output Format:**
```json
{
  "channel_id": "ch_0",
  "alternatives": [{
    "transcript": "Hello this is the agent speaking"
  }]
}
```

---

### Deepgram

**Multichannel Mode:**
```bash
# Automatically enabled with "stereo" parameter
uuid_deepgram_transcribe <uuid> start en-US nova-2 stereo
```

**Output Format:**
```json
{
  "channel_index": [0],
  "transcript": "Hello this is the agent speaking"
}
```

---

### Azure Speech Services

**Conversation Transcriber:**
- Automatically uses 2-channel mode in stereo
- Provides speaker identification via diarization

**Output Format:**
```json
{
  "speaker": "Guest-1",
  "text": "Hello this is the agent speaking"
}
```

**Note:** Azure's speaker IDs don't directly map to channels, but with consistent assignment:
- Guest-1 typically = Channel 0 = Agent
- Guest-2 typically = Channel 1 = Customer

---

### Google Cloud Speech-to-Text

**Separate Recognition Per Channel:**
```xml
<action application="set" data="GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL=1"/>
```

**Output Format:**
```json
{
  "channelTag": 1,
  "alternatives": [{
    "transcript": "Hello this is the agent speaking"
  }]
}
```

---

## Best Practices

1. **Always document your channel assignment strategy** in dialplan comments
2. **Use consistent variable naming** (`CALL_DIRECTION`, `CALL_AGENT`, `CALL_CUSTOMER`)
3. **Test both inbound and outbound scenarios** before production
4. **Add logging** for debugging channel assignment issues
5. **Include channel info in metadata** sent to transcription services
6. **Monitor analytics** to catch assignment issues early

---

## Reference: FreeSWITCH Flags

| Flag | Value | Description |
|------|-------|-------------|
| `SMBF_READ_STREAM` | `(1 << 0)` | Capture audio from remote party (incoming audio) |
| `SMBF_WRITE_STREAM` | `(1 << 1)` | Capture audio to remote party (outgoing audio) |
| `SMBF_STEREO` | `(1 << 5)` | Enable stereo recording (2 channels) |
| `SMBF_STEREO_SWAP` | `(1 << 11)` | Swap left/right channels |

**Source:** FreeSWITCH `src/include/switch_types.h`

---

## Additional Resources

- [FreeSWITCH Media Bug API Documentation](https://freeswitch.org/confluence/display/FREESWITCH/Media+Bug+API)
- [FreeSWITCH Channel Variables](https://freeswitch.org/confluence/display/FREESWITCH/Channel+Variables)
- [mod_aws_transcribe README](../modules/mod_aws_transcribe/README.md)
- [mod_deepgram_transcribe README](../modules/mod_deepgram_transcribe/README.md)
- [mod_azure_transcribe README](../modules/mod_azure_transcribe/README.md)
- [mod_google_transcribe README](../modules/mod_google_transcribe/README.md)

---

## Changelog

| Date | Change |
|------|--------|
| 2025-11-20 | Initial documentation created |

---

## Contributing

Found an issue or have improvements? Please submit a pull request or open an issue on GitHub.

## License

This documentation is part of the freeswitch-speech-ai project and follows the same license terms.
