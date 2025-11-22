# Transcription JSON Formats Reference

This document shows the **actual JSON formats** returned by each transcription service with real examples.

---

## 1. Deepgram Format

### Basic Structure (with Speaker Diarization)

```json
{
  "type": "Results",
  "channel_index": [1, 2],
  "duration": 1.2299995,
  "start": 41.72,
  "is_final": true,
  "speech_final": true,
  "channel": {
    "alternatives": [{
      "transcript": "just going on",
      "confidence": 0.96069336,
      "words": [{
        "word": "just",
        "start": 41.72,
        "end": 42.2,
        "confidence": 0.89404297,
        "speaker": 0
      }, {
        "word": "going",
        "start": 42.2,
        "end": 42.52,
        "confidence": 0.99902344,
        "speaker": 0
      }, {
        "word": "on",
        "start": 42.52,
        "end": 42.95,
        "confidence": 0.96069336,
        "speaker": 0
      }]
    }]
  },
  "metadata": {
    "request_id": "2f82afc4-f847-4218-9069-56604796e464",
    "model_info": {
      "name": "phonecall-nova",
      "version": "2023-03-13.31000",
      "arch": "nova"
    },
    "model_uuid": "47cb79bc-c75a-43c9-9cd7-90959e2e2b8c"
  },
  "from_finalize": false
}
```

### Key Fields

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always "Results" for transcription events |
| `channel_index` | array | Channel identifiers [0] or [1] for stereo |
| `is_final` | boolean | True if final result, false if interim |
| `speech_final` | boolean | True when speech segment is complete |
| `channel.alternatives[0].transcript` | string | The transcribed text |
| `channel.alternatives[0].words` | array | Word-level details with timestamps |
| `words[].speaker` | number | Speaker ID (0, 1, 2...) if diarization enabled |

### Parsing Logic

```javascript
const transcript = JSON.parse(event.getBody());

// Get channel (0 or 1)
const channelIndex = transcript.channel_index[0];

// Get text
const text = transcript.channel.alternatives[0].transcript;

// Check for speaker diarization
const words = transcript.channel.alternatives[0].words || [];
const hasSpeakers = words.some(w => w.speaker !== undefined);

// Group by speaker if diarization enabled
if (hasSpeakers) {
  let currentSpeaker = null;
  let currentText = '';

  for (const word of words) {
    if (word.speaker !== currentSpeaker) {
      if (currentText) {
        console.log(`Speaker-${currentSpeaker}: ${currentText.trim()}`);
      }
      currentSpeaker = word.speaker;
      currentText = word.word + ' ';
    } else {
      currentText += word.word + ' ';
    }
  }
}

// Map channel to speaker name
const speaker = (channelIndex === 0) ? caller_name : callee_name;
```

---

## 2. AWS Transcribe Format

### Basic Structure (with Speaker Diarization)

```json
[{
  "is_final": true,
  "channel_id": "ch_1",
  "result_id": "872fc51e-f315-4792-ac16-a3ed60adde69",
  "start_time": 12.127,
  "end_time": 13.487,
  "alternatives": [{
    "transcript": "I said, hey brother, how are you",
    "items": [{
      "content": "I",
      "type": "pronunciation",
      "start_time": 12.137,
      "end_time": 12.287,
      "confidence": 0.9966,
      "speaker_label": "0"
    }, {
      "content": "said",
      "type": "pronunciation",
      "start_time": 12.287,
      "end_time": 12.327,
      "confidence": 0.9964,
      "speaker_label": "0"
    }, {
      "content": ",",
      "type": "punctuation"
    }, {
      "content": "hey",
      "type": "pronunciation",
      "start_time": 12.407,
      "end_time": 12.507,
      "confidence": 0.4634,
      "speaker_label": "0"
    }, {
      "content": "brother",
      "type": "pronunciation",
      "start_time": 12.507,
      "end_time": 12.517,
      "confidence": 0.9843,
      "speaker_label": "0"
    }, {
      "content": ",",
      "type": "punctuation"
    }, {
      "content": "how",
      "type": "pronunciation",
      "start_time": 12.767,
      "end_time": 12.807,
      "confidence": 0.9955,
      "speaker_label": "0"
    }, {
      "content": "are",
      "type": "pronunciation",
      "start_time": 12.807,
      "end_time": 12.817,
      "confidence": 0.9979,
      "speaker_label": "0"
    }, {
      "content": "you",
      "type": "pronunciation",
      "start_time": 12.817,
      "end_time": 13.467,
      "confidence": 0.9983,
      "speaker_label": "1"
    }]
  }]
}]
```

### Key Fields

| Field | Type | Description |
|-------|------|-------------|
| `is_final` | boolean | True if final result, false if interim |
| `channel_id` | string | "ch_0" or "ch_1" for stereo mode |
| `alternatives[0].transcript` | string | The transcribed text |
| `alternatives[0].items` | array | Word/punctuation items with details |
| `items[].content` | string | Word or punctuation content |
| `items[].type` | string | "pronunciation" or "punctuation" |
| `items[].speaker_label` | string | Speaker ID ("0", "1", "2"...) if diarization enabled |

### Parsing Logic

```javascript
const transcript = JSON.parse(event.getBody());

// AWS returns an array
for (const result of transcript) {
  const channelId = result.channel_id; // "ch_0" or "ch_1"
  const isFinal = result.is_final;
  const text = result.alternatives[0].transcript;
  const items = result.alternatives[0].items || [];

  // Check for speaker diarization
  const hasSpeakers = items.some(item => item.speaker_label !== undefined);

  if (hasSpeakers) {
    // Group by speaker
    let currentSpeaker = null;
    let currentText = '';

    for (const item of items) {
      if (item.type === 'pronunciation' && item.speaker_label !== undefined) {
        if (currentSpeaker !== item.speaker_label) {
          if (currentText) {
            console.log(`Speaker-${currentSpeaker}: ${currentText.trim()}`);
          }
          currentSpeaker = item.speaker_label;
          currentText = item.content + ' ';
        } else {
          currentText += item.content + ' ';
        }
      } else if (item.type === 'punctuation') {
        currentText += item.content;
      }
    }
  }

  // Map channel to speaker name
  const speaker = (channelId === 'ch_0') ? caller_name : callee_name;
}
```

---

## 3. Azure Format

### Basic Structure (with Speaker Diarization)

```json
{
  "Type": "ConversationTranscription",
  "SpeakerId": "Guest-1",
  "Channel": 0,
  "DisplayText": "Hello, how are you?",
  "Id": "552502b2ed704e48940207fbe64ff3fe",
  "RecognitionStatus": "Success",
  "Offset": 241100000,
  "Duration": 4400000
}
```

### Basic Structure (without Diarization)

```json
{
  "Id": "552502b2ed704e48940207fbe64ff3fe",
  "RecognitionStatus": "Success",
  "DisplayText": "Hello.",
  "Offset": 241100000,
  "Duration": 4400000,
  "Channel": 1
}
```

### Key Fields

| Field | Type | Description |
|-------|------|-------------|
| `Type` | string | "ConversationTranscription" if diarization enabled |
| `Channel` | number | 0 or 1 for stereo mode |
| `DisplayText` | string | The transcribed text |
| `RecognitionStatus` | string | "Success", "NoMatch", "InitialSilenceTimeout", etc. |
| `SpeakerId` | string | Speaker ID (e.g., "Guest-1", "Guest-2") if diarization enabled |
| `Offset` | number | Start time in 100-nanosecond units |
| `Duration` | number | Duration in 100-nanosecond units |

### Parsing Logic

```javascript
const transcript = JSON.parse(event.getBody());

const channel = transcript.Channel; // 0 or 1
const text = transcript.DisplayText;
const speakerId = transcript.SpeakerId; // "Guest-1", "Guest-2", etc.
const isFinal = transcript.RecognitionStatus === 'Success';

// Map channel to speaker name
const speaker = (channel === 0) ? caller_name : callee_name;

// Display with speaker ID if available
if (speakerId) {
  console.log(`${speaker} - ${speakerId}: ${text}`);
} else {
  console.log(`${speaker}: ${text}`);
}
```

---

## Comparison Table

| Feature | Deepgram | AWS | Azure |
|---------|----------|-----|-------|
| **Channel ID Format** | `channel_index: [0]` | `channel_id: "ch_0"` | `Channel: 0` |
| **Text Field** | `channel.alternatives[0].transcript` | `alternatives[0].transcript` | `DisplayText` |
| **Final Flag** | `is_final: true` | `is_final: true` | `RecognitionStatus: "Success"` |
| **Speaker Diarization** | `words[].speaker: 0` | `items[].speaker_label: "0"` | `SpeakerId: "Guest-1"` |
| **Word Timestamps** | `words[]` | `items[]` | Not in basic output |
| **Confidence** | `confidence: 0.96` | `items[].confidence: 0.99` | Not in basic output |
| **Return Type** | Object | Array | Object |

---

## Unified Mapping Function

```javascript
function mapChannelToSpeaker(transcript, vendor, callInfo) {
  let channelId = null;

  switch (vendor) {
    case 'deepgram':
      channelId = transcript.channel_index?.[0];
      return (channelId === 0) ? callInfo.caller : callInfo.callee;

    case 'aws':
      channelId = transcript[0]?.channel_id;
      return (channelId === 'ch_0') ? callInfo.caller : callInfo.callee;

    case 'azure':
      channelId = transcript.Channel;
      return (channelId === 0) ? callInfo.caller : callInfo.callee;
  }

  return null;
}
```

---

## Speaker Name Variables (Same for All Three)

From dialplan:
```xml
<action application="export" data="nolocal:caller_name=${effective_caller_id_name}"/>
<action application="export" data="nolocal:caller_number=${caller_id_number}"/>
<action application="export" data="nolocal:callee_name=${callee_id_name}"/>
<action application="export" data="nolocal:callee_number=${destination_number}"/>
```

Access in application:
```javascript
const callerName = event.getHeader('caller_name');
const callerNumber = event.getHeader('caller_number');
const calleeName = event.getHeader('callee_name');
const calleeNumber = event.getHeader('callee_number');
```

---

## Complete Working Example

See `examples/comprehensive-transcription-parser.js` for a complete implementation that handles all three formats correctly.

---

## Testing

```bash
# Install dependencies
npm install modesl

# Run the parser
node examples/comprehensive-transcription-parser.js

# Make a test call with transcription enabled
# You should see output like:

📞 Call started [uuid]:
   Caller: John Doe (1002)
   Callee: Customer Service (1003)

[Deepgram] John Doe (1002) - speakers detected:
  Speaker-0: just going on ✓

[AWS] Customer Service (1003) - 2 speakers detected:
  Speaker-0: I said, hey brother, how are ✓
  Speaker-1: you ✓

[Azure] John Doe (1002) - Guest-1: Hello, how are you? ✓
```
