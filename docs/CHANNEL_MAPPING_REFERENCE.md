# Channel-to-Speaker Mapping Reference

Quick reference for mapping channel identifiers to speaker names across AWS, Deepgram, and Azure transcription modules.

---

## Exported Variables (Same for All Modules)

These variables are exported from the dialplan and available in all transcription events:

| Variable | Source | Example Value |
|----------|--------|---------------|
| `caller_name` | `${effective_caller_id_name}` | "John Doe" |
| `caller_number` | `${caller_id_number}` | "1002" |
| `callee_name` | `${callee_id_name}` | "Customer Service" |
| `callee_number` | `${destination_number}` | "1003" |

**Dialplan Configuration:**
```xml
<!-- Same for AWS, Deepgram, and Azure -->
<action application="export" data="nolocal:caller_name=${effective_caller_id_name}"/>
<action application="export" data="nolocal:caller_number=${caller_id_number}"/>
<action application="export" data="nolocal:callee_name=${callee_id_name}"/>
<action application="export" data="nolocal:callee_number=${destination_number}"/>
```

---

## Channel Mapping (Vendor-Specific Formats)

### AWS Transcribe

**JSON Format:**
```json
[
  {
    "is_final": true,
    "channel_id": "ch_0",
    "alternatives": [{
      "transcript": "Hello, how can I help you?"
    }]
  }
]
```

**Mapping Logic:**
```javascript
const channelId = transcript[0].channel_id;

if (channelId === 'ch_0') {
  speaker = { name: caller_name, number: caller_number };
} else if (channelId === 'ch_1') {
  speaker = { name: callee_name, number: callee_number };
}
```

| Channel ID | Speaker | Variable |
|-----------|---------|----------|
| `"ch_0"` | Caller (Agent) | `caller_name` |
| `"ch_1"` | Callee (Customer) | `callee_name` |

---

### Deepgram

**JSON Format:**
```json
{
  "channel_index": [0],
  "is_final": true,
  "channel": {
    "alternatives": [{
      "transcript": "Hello, how can I help you?"
    }]
  }
}
```

**Mapping Logic:**
```javascript
const channelIndex = transcript.channel_index[0];

if (channelIndex === 0) {
  speaker = { name: caller_name, number: caller_number };
} else if (channelIndex === 1) {
  speaker = { name: callee_name, number: callee_number };
}
```

| Channel Index | Speaker | Variable |
|--------------|---------|----------|
| `0` | Caller (Agent) | `caller_name` |
| `1` | Callee (Customer) | `callee_name` |

---

### Azure

**JSON Format:**
```json
{
  "Type": "ConversationTranscription",
  "Channel": 0,
  "DisplayText": "Hello, how can I help you?",
  "RecognitionStatus": "Success"
}
```

**Mapping Logic:**
```javascript
const channel = transcript.Channel;

if (channel === 0) {
  speaker = { name: caller_name, number: caller_number };
} else if (channel === 1) {
  speaker = { name: callee_name, number: callee_number };
}
```

| Channel | Speaker | Variable |
|---------|---------|----------|
| `0` | Caller (Agent) | `caller_name` |
| `1` | Callee (Customer) | `callee_name` |

---

## Unified Mapping Function

```javascript
function mapChannelToSpeaker(transcript, vendor, callInfo) {
  let channelId = null;

  switch (vendor) {
    case 'AWS':
      channelId = transcript[0]?.channel_id;
      return channelId === 'ch_0' ? callInfo.caller : callInfo.callee;

    case 'Deepgram':
      channelId = transcript.channel_index?.[0];
      return channelId === 0 ? callInfo.caller : callInfo.callee;

    case 'Azure':
      channelId = transcript.Channel;
      return channelId === 0 ? callInfo.caller : callInfo.callee;
  }

  return null;
}
```

---

## Complete Example

```javascript
const { Connection } = require('modesl');

const conn = new Connection('127.0.0.1', 8021, 'ClueCon', () => {
  conn.subscribe([
    'aws_transcribe::transcription',
    'deepgram_transcribe::transcription',
    'azure_transcribe::transcription'
  ]);
});

// AWS
conn.on('esl::event::aws_transcribe::transcription', (event) => {
  const transcript = JSON.parse(event.getBody());
  const channelId = transcript[0].channel_id;

  const speaker = channelId === 'ch_0'
    ? event.getHeader('caller_name')
    : event.getHeader('callee_name');

  const text = transcript[0].alternatives[0].transcript;
  console.log(`${speaker}: ${text}`);
});

// Deepgram
conn.on('esl::event::deepgram_transcribe::transcription', (event) => {
  const transcript = JSON.parse(event.getBody());
  const channelIndex = transcript.channel_index[0];

  const speaker = channelIndex === 0
    ? event.getHeader('caller_name')
    : event.getHeader('callee_name');

  const text = transcript.channel.alternatives[0].transcript;
  console.log(`${speaker}: ${text}`);
});

// Azure
conn.on('esl::event::azure_transcribe::transcription', (event) => {
  const transcript = JSON.parse(event.getBody());
  const channel = transcript.Channel;

  const speaker = channel === 0
    ? event.getHeader('caller_name')
    : event.getHeader('callee_name');

  const text = transcript.DisplayText;
  console.log(`${speaker}: ${text}`);
});
```

---

## Channel Assignment Rules

### Default Channel Assignment

| Call Direction | Channel 0 | Channel 1 |
|---------------|-----------|-----------|
| **Outbound** (Agent calls Customer) | Agent (caller) | Customer (callee) |
| **Inbound** (Customer calls Agent) | Customer (caller) | Agent (callee) |

**Problem:** Agent is on different channels depending on call direction!

### With `RECORD_STEREO_SWAP=true` (Recommended)

```xml
<!-- For inbound calls, swap channels so agent is always on ch_0 -->
<action application="set" data="RECORD_STEREO_SWAP=true"/>
```

| Call Direction | Channel 0 | Channel 1 |
|---------------|-----------|-----------|
| **Outbound** (Agent calls Customer) | Agent ✓ | Customer ✓ |
| **Inbound** (Customer calls Agent) | Agent ✓ | Customer ✓ |

**Result:** Agent is consistently on channel 0, customer on channel 1!

See: [docs/QUICK_REFERENCE_STEREO_CHANNELS.md](./QUICK_REFERENCE_STEREO_CHANNELS.md)

---

## Summary

✅ **Same Variables for All Three:**
- `caller_name`, `caller_number`, `callee_name`, `callee_number`

✅ **Same Mapping Logic:**
- Channel 0 (or "ch_0") → `caller_name`
- Channel 1 (or "ch_1") → `callee_name`

❌ **Different JSON Formats:**
- AWS: `channel_id: "ch_0"`
- Deepgram: `channel_index: [0]`
- Azure: `Channel: 0`

**Recommendation:** Use the unified handler from `examples/unified-transcription-handler.js` to handle all three modules with the same code.

---

## Testing

```bash
# Start a call and check variables are exported
fs_cli -x "uuid_getvar <uuid> caller_name"
fs_cli -x "uuid_getvar <uuid> callee_name"

# Start the unified handler
node examples/unified-transcription-handler.js
```

**Expected Output:**
```
Call started [bba5d840-47d1-4245-8319-deda26f01b95]:
  Caller: John Doe (1002)
  Callee: Customer Service (1003)

[AWS] John Doe (1002): Hello, how can I help you? ✓
[Deepgram] Customer Service (1003): I need assistance ✓
[Azure] John Doe (1002): Sure, let me help ✓
```
