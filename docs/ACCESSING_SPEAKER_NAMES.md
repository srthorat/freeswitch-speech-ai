# Accessing Speaker Names from Dialplan

This guide shows how to access speaker names and identifiers from FreeSWITCH transcription events in your application.

## What's Exported from Dialplan

The updated dialplan exports these channel variables:

| Variable | Description | Example Value |
|----------|-------------|---------------|
| `caller_name` | Name of the calling party | "John Doe", "Extension 1002" |
| `caller_number` | Number of the calling party | "1002" |
| `callee_name` | Name of the called party | "Customer Service" |
| `callee_number` | Number of the called party | "1003" |

These are set in the dialplan using:
```xml
<action application="export" data="nolocal:caller_name=${effective_caller_id_name}"/>
<action application="export" data="nolocal:caller_number=${caller_id_number}"/>
<action application="export" data="nolocal:callee_name=${callee_id_name}"/>
<action application="export" data="nolocal:callee_number=${destination_number}"/>
```

---

## Method 1: Access via FreeSWITCH Event Headers

When transcription events fire, these variables are automatically included in the event headers.

### Example: Listening to Events via ESL (Event Socket Library)

```javascript
const { Connection } = require('modesl');

const conn = new Connection('127.0.0.1', 8021, 'ClueCon', () => {
  conn.subscribe(['aws_transcribe::transcription'], () => {
    console.log('Subscribed to AWS transcription events');
  });
});

conn.on('esl::event::aws_transcribe::transcription', (event) => {
  // Get speaker names from event headers
  const callerName = event.getHeader('caller_name');
  const callerNumber = event.getHeader('caller_number');
  const calleeName = event.getHeader('callee_name');
  const calleeNumber = event.getHeader('callee_number');

  // Get transcription result
  const body = event.getBody();
  const transcript = JSON.parse(body);

  // Map channel_id to speaker name
  const channelId = transcript[0].channel_id;
  const speakerName = channelId === 'ch_0' ? callerName : calleeName;

  console.log(`${speakerName}: ${transcript[0].alternatives[0].transcript}`);
  // Output: "John Doe: Hello, how can I help you?"
});
```

---

## Method 2: Access via drachtio-fsmrf

When using [drachtio-fsmrf](https://www.npmjs.com/package/drachtio-fsmrf), you can get channel variables from the endpoint.

```javascript
const Srf = require('drachtio-srf');
const Mrf = require('drachtio-fsmrf');

const srf = new Srf();
const mrf = new Mrf(srf);

srf.invite(async (req, res) => {
  try {
    const ms = await mrf.connect({ ... });
    const ep = await ms.createEndpoint();

    // Set speaker names
    await ep.set({
      caller_name: 'John Doe',
      caller_number: '1002',
      callee_name: 'Customer Service',
      callee_number: '1003'
    });

    // Start AWS transcription
    await ep.set({
      AWS_ENABLE_CHANNEL_IDENTIFICATION: 'true',
      AWS_NUMBER_OF_CHANNELS: '2',
      AWS_REGION: 'us-east-1'
    });
    await ep.api('uuid_aws_transcribe', `${ep.uuid} start en-US interim stereo`);

    // Get channel variables
    const callerName = await ep.getChannelVariable('caller_name');
    const calleeName = await ep.getChannelVariable('callee_name');

    // Listen for transcription events
    ep.on('aws_transcribe::transcription', (evt) => {
      const data = JSON.parse(evt.body);
      const channelId = data[0].channel_id;

      // Map channel to speaker name
      const speakerName = channelId === 'ch_0' ? callerName : calleeName;
      const transcript = data[0].alternatives[0].transcript;

      console.log(`${speakerName}: ${transcript}`);
    });

    // Answer the call
    await ep.bridge(req);

  } catch (err) {
    console.error('Error:', err);
  }
});
```

---

## Method 3: Query Variables via fs_cli

You can also query channel variables directly from FreeSWITCH CLI:

```bash
# Get all channel variables for a call
fs_cli -x "uuid_dump <uuid>"

# Get specific variable
fs_cli -x "uuid_getvar <uuid> caller_name"
fs_cli -x "uuid_getvar <uuid> callee_name"
```

Example output:
```
fs_cli -x "uuid_getvar bba5d840-47d1-4245-8319-deda26f01b95 caller_name"
John Doe
```

---

## Method 4: Complete Example with Speaker Mapping

Here's a complete example showing how to map AWS/Deepgram channel IDs to speaker names:

```javascript
const { Connection } = require('modesl');

// Store call metadata indexed by UUID
const activeCalls = new Map();

class CallHandler {
  constructor() {
    this.conn = new Connection('127.0.0.1', 8021, 'ClueCon', () => {
      // Subscribe to all relevant events
      this.conn.subscribe([
        'CHANNEL_ANSWER',
        'CHANNEL_HANGUP',
        'aws_transcribe::transcription',
        'deepgram_transcribe::transcription'
      ]);
    });

    this.setupEventHandlers();
  }

  setupEventHandlers() {
    // Store speaker info when call is answered
    this.conn.on('esl::event::CHANNEL_ANSWER', (event) => {
      const uuid = event.getHeader('Unique-ID');
      const callerName = event.getHeader('caller_name') || event.getHeader('Caller-Caller-ID-Name');
      const callerNumber = event.getHeader('caller_number') || event.getHeader('Caller-Caller-ID-Number');
      const calleeName = event.getHeader('callee_name') || event.getHeader('Caller-Callee-ID-Name');
      const calleeNumber = event.getHeader('callee_number') || event.getHeader('Caller-Destination-Number');

      activeCalls.set(uuid, {
        caller: { name: callerName, number: callerNumber },
        callee: { name: calleeName, number: calleeNumber }
      });

      console.log(`Call started: ${callerName} (${callerNumber}) → ${calleeName} (${calleeNumber})`);
    });

    // Clean up when call ends
    this.conn.on('esl::event::CHANNEL_HANGUP', (event) => {
      const uuid = event.getHeader('Unique-ID');
      activeCalls.delete(uuid);
    });

    // Handle AWS transcription
    this.conn.on('esl::event::aws_transcribe::transcription', (event) => {
      this.handleTranscription(event, 'aws');
    });

    // Handle Deepgram transcription
    this.conn.on('esl::event::deepgram_transcribe::transcription', (event) => {
      this.handleTranscription(event, 'deepgram');
    });
  }

  handleTranscription(event, vendor) {
    const uuid = event.getHeader('Unique-ID');
    const callInfo = activeCalls.get(uuid);

    if (!callInfo) {
      console.warn(`No call info found for UUID ${uuid}`);
      return;
    }

    const body = event.getBody();
    const transcript = JSON.parse(body);

    let speakerName, speakerNumber, channelId;

    if (vendor === 'aws') {
      // AWS uses channel_id: "ch_0" or "ch_1"
      channelId = transcript[0].channel_id;
      const speaker = channelId === 'ch_0' ? callInfo.caller : callInfo.callee;
      speakerName = speaker.name;
      speakerNumber = speaker.number;

    } else if (vendor === 'deepgram') {
      // Deepgram uses channel_index: [0] or [1]
      const channelIndex = transcript.channel_index[0];
      const speaker = channelIndex === 0 ? callInfo.caller : callInfo.callee;
      speakerName = speaker.name;
      speakerNumber = speaker.number;
    }

    // Extract transcript text
    const text = vendor === 'aws'
      ? transcript[0].alternatives[0].transcript
      : transcript.channel.alternatives[0].transcript;

    const isFinal = vendor === 'aws'
      ? transcript[0].is_final
      : transcript.is_final;

    // Log the transcription with speaker name
    console.log(`[${vendor.toUpperCase()}] ${speakerName} (${speakerNumber}): ${text} ${isFinal ? '✓' : '~'}`);
  }
}

// Start the handler
new CallHandler();
```

**Example Output:**
```
Call started: John Doe (1002) → Customer Service (1003)
[AWS] John Doe (1002): Hello, how can I help you? ✓
[AWS] Customer Service (1003): I need assistance with my order ✓
[AWS] John Doe (1002): Sure, let me look that up ~
[AWS] John Doe (1002): Sure, let me look that up for you ✓
```

---

## Method 5: Using UUID to Query Variables

If you only have the UUID, you can query channel variables directly:

```javascript
const { Connection } = require('modesl');

async function getSpeakerNames(uuid) {
  return new Promise((resolve, reject) => {
    const conn = new Connection('127.0.0.1', 8021, 'ClueCon', () => {
      conn.api(`uuid_getvar ${uuid} caller_name`, (res) => {
        const callerName = res.getBody();

        conn.api(`uuid_getvar ${uuid} callee_name`, (res2) => {
          const calleeName = res2.getBody();

          conn.disconnect();
          resolve({ callerName, calleeName });
        });
      });
    });
  });
}

// Usage
const { callerName, calleeName } = await getSpeakerNames('bba5d840-47d1-4245-8319-deda26f01b95');
console.log(`Caller: ${callerName}, Callee: ${calleeName}`);
```

---

## Channel Mapping Reference

### AWS Transcribe

| Channel ID | Stereo Position | Typical Speaker |
|-----------|----------------|-----------------|
| `ch_0` | LEFT | Agent / Caller |
| `ch_1` | RIGHT | Customer / Callee |

**With `RECORD_STEREO_SWAP=true` (recommended for inbound calls):**
- Ensures agent is always on `ch_0` regardless of call direction

### Deepgram Transcribe

| Channel Index | Stereo Position | Typical Speaker |
|--------------|----------------|-----------------|
| `[0]` | LEFT | Agent / Caller |
| `[1]` | RIGHT | Customer / Callee |

---

## Setting Custom Speaker Names

Update the user directory to customize speaker names:

**File: `/usr/local/freeswitch/conf/directory/default/1002.xml`**
```xml
<variables>
  <!-- Change this to any name you want -->
  <variable name="effective_caller_id_name" value="John Smith"/>
  <variable name="effective_caller_id_number" value="1002"/>

  <variable name="enable_aws_transcribe" value="true"/>
</variables>
```

After editing:
```bash
fs_cli -x 'reloadxml'
```

Now all transcriptions will use "John Smith" instead of "Extension 1002".

---

## Troubleshooting

### Variables Not Available in Events

If channel variables aren't appearing in events, check:

1. **Dialplan has export statements:**
```xml
<action application="export" data="nolocal:caller_name=${effective_caller_id_name}"/>
```

2. **Reload dialplan after changes:**
```bash
fs_cli -x 'reloadxml'
```

3. **Check if variables are set on channel:**
```bash
fs_cli -x "uuid_dump <uuid>" | grep caller_name
```

### Speaker Names Showing as Empty

If names are empty, verify:

1. **User directory has names configured:**
```xml
<variable name="effective_caller_id_name" value="Your Name Here"/>
```

2. **For callee_name, set in dialplan:**
```xml
<action application="set" data="effective_callee_id_name=Customer Service"/>
```

---

## Summary

**Key Points:**

1. ✅ **Dialplan exports 4 variables**: `caller_name`, `caller_number`, `callee_name`, `callee_number`
2. ✅ **Available in all events**: These are automatically included in transcription events
3. ✅ **Map channel IDs to names**: Use `ch_0 = caller` and `ch_1 = callee`
4. ✅ **Set names in user directory**: Update `effective_caller_id_name` for custom names
5. ✅ **Access via ESL or drachtio**: Use `event.getHeader('caller_name')` or `ep.getChannelVariable('caller_name')`

This allows you to show **"John Doe: Hello"** instead of **"ch_0: Hello"** in your transcripts!
