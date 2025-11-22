/**
 * Comprehensive Transcription Parser
 *
 * Handles all three transcription services with their actual JSON formats:
 * 1. Deepgram - channel_index with words array
 * 2. AWS - channel_id with items array (speaker diarization)
 * 3. Azure - Channel with SpeakerId
 */

const { Connection } = require('modesl');

class ComprehensiveTranscriptionParser {
  constructor() {
    this.activeCalls = new Map();
    this.conn = new Connection('127.0.0.1', 8021, 'ClueCon', () => {
      console.log('Connected to FreeSWITCH Event Socket');

      this.conn.subscribe([
        'CHANNEL_ANSWER',
        'CHANNEL_HANGUP',
        'aws_transcribe::transcription',
        'deepgram_transcribe::transcription',
        'azure_transcribe::transcription'
      ], () => {
        console.log('Subscribed to all transcription events');
      });
    });

    this.setupEventHandlers();
  }

  setupEventHandlers() {
    // Store call info on answer
    this.conn.on('esl::event::CHANNEL_ANSWER', (event) => {
      const uuid = event.getHeader('Unique-ID');

      this.activeCalls.set(uuid, {
        caller: {
          name: event.getHeader('caller_name') || event.getHeader('Caller-Caller-ID-Name'),
          number: event.getHeader('caller_number') || event.getHeader('Caller-Caller-ID-Number')
        },
        callee: {
          name: event.getHeader('callee_name') || event.getHeader('Caller-Callee-ID-Name'),
          number: event.getHeader('callee_number') || event.getHeader('Caller-Destination-Number')
        }
      });

      console.log(`\n📞 Call started [${uuid}]:`);
      console.log(`   Caller: ${this.activeCalls.get(uuid).caller.name} (${this.activeCalls.get(uuid).caller.number})`);
      console.log(`   Callee: ${this.activeCalls.get(uuid).callee.name} (${this.activeCalls.get(uuid).callee.number})\n`);
    });

    // Cleanup on hangup
    this.conn.on('esl::event::CHANNEL_HANGUP', (event) => {
      const uuid = event.getHeader('Unique-ID');
      this.activeCalls.delete(uuid);
      console.log(`\n📞 Call ended [${uuid}]\n`);
    });

    // AWS Transcribe handler
    this.conn.on('esl::event::aws_transcribe::transcription', (event) => {
      this.handleAWSTranscription(event);
    });

    // Deepgram handler
    this.conn.on('esl::event::deepgram_transcribe::transcription', (event) => {
      this.handleDeepgramTranscription(event);
    });

    // Azure handler
    this.conn.on('esl::event::azure_transcribe::transcription', (event) => {
      this.handleAzureTranscription(event);
    });
  }

  /**
   * AWS Transcribe Format:
   * [{
   *   "is_final": true,
   *   "channel_id": "ch_1",
   *   "result_id": "...",
   *   "alternatives": [{
   *     "transcript": "I said, hey brother",
   *     "items": [{
   *       "content": "I",
   *       "speaker_label": "0",
   *       "start_time": 12.137,
   *       "end_time": 12.287
   *     }]
   *   }]
   * }]
   */
  handleAWSTranscription(event) {
    const uuid = event.getHeader('Unique-ID');
    const callInfo = this.activeCalls.get(uuid);

    if (!callInfo) return;

    try {
      const body = event.getBody();
      const transcript = JSON.parse(body);

      // AWS returns an array
      for (const result of transcript) {
        const channelId = result.channel_id; // "ch_0" or "ch_1"
        const isFinal = result.is_final;
        const alternatives = result.alternatives || [];

        if (alternatives.length === 0) continue;

        const alt = alternatives[0];
        const text = alt.transcript;

        // Map channel to speaker name
        let speaker = this.mapChannelToSpeaker(channelId, 'aws', callInfo);

        // If speaker diarization is enabled, extract speaker labels from items
        const items = alt.items || [];
        const speakers = this.extractAWSSpeakers(items);

        if (speakers.length > 0) {
          // With speaker diarization
          console.log(`[AWS] ${speaker.name} (${speaker.number}) - ${speakers.length} speakers detected:`);

          let currentSpeaker = null;
          let currentText = '';

          for (const item of items) {
            if (item.type === 'pronunciation' && item.speaker_label !== undefined) {
              const speakerLabel = item.speaker_label;

              if (currentSpeaker !== speakerLabel) {
                if (currentText) {
                  console.log(`  Speaker-${currentSpeaker}: ${currentText.trim()} ${isFinal ? '✓' : '~'}`);
                }
                currentSpeaker = speakerLabel;
                currentText = item.content + ' ';
              } else {
                currentText += item.content + ' ';
              }
            } else if (item.type === 'punctuation') {
              currentText += item.content;
            }
          }

          if (currentText) {
            console.log(`  Speaker-${currentSpeaker}: ${currentText.trim()} ${isFinal ? '✓' : '~'}`);
          }

        } else {
          // Without speaker diarization - just channel
          console.log(`[AWS] ${speaker.name} (${speaker.number}): ${text} ${isFinal ? '✓' : '~'}`);
        }
      }

    } catch (err) {
      console.error('[AWS] Parse error:', err.message);
    }
  }

  /**
   * Deepgram Format:
   * {
   *   "type": "Results",
   *   "channel_index": [1, 2],
   *   "duration": 1.23,
   *   "start": 41.72,
   *   "is_final": true,
   *   "speech_final": true,
   *   "channel": {
   *     "alternatives": [{
   *       "transcript": "just going on",
   *       "confidence": 0.96,
   *       "words": [{
   *         "word": "just",
   *         "start": 41.72,
   *         "end": 42.2,
   *         "confidence": 0.89,
   *         "speaker": 0
   *       }]
   *     }]
   *   }
   * }
   */
  handleDeepgramTranscription(event) {
    const uuid = event.getHeader('Unique-ID');
    const callInfo = this.activeCalls.get(uuid);

    if (!callInfo) return;

    try {
      const body = event.getBody();
      const transcript = JSON.parse(body);

      // Check if this is a Results type
      if (transcript.type !== 'Results') return;

      const channelIndex = transcript.channel_index ? transcript.channel_index[0] : null;
      const isFinal = transcript.is_final;
      const channel = transcript.channel || {};
      const alternatives = channel.alternatives || [];

      if (alternatives.length === 0) return;

      const alt = alternatives[0];
      const text = alt.transcript;
      const words = alt.words || [];

      // Map channel to speaker name
      let speaker = this.mapChannelToSpeaker(channelIndex, 'deepgram', callInfo);

      // Check if speaker diarization is enabled
      const hasSpeakerInfo = words.some(w => w.speaker !== undefined);

      if (hasSpeakerInfo && words.length > 0) {
        // With speaker diarization - group by speaker
        console.log(`[Deepgram] ${speaker.name} (${speaker.number}) - speakers detected:`);

        let currentSpeaker = null;
        let currentText = '';

        for (const word of words) {
          if (word.speaker !== currentSpeaker) {
            if (currentText) {
              console.log(`  Speaker-${currentSpeaker}: ${currentText.trim()} ${isFinal ? '✓' : '~'}`);
            }
            currentSpeaker = word.speaker;
            currentText = word.word + ' ';
          } else {
            currentText += word.word + ' ';
          }
        }

        if (currentText) {
          console.log(`  Speaker-${currentSpeaker}: ${currentText.trim()} ${isFinal ? '✓' : '~'}`);
        }

      } else {
        // Without speaker diarization - just channel
        console.log(`[Deepgram] ${speaker.name} (${speaker.number}): ${text} ${isFinal ? '✓' : '~'}`);
      }

    } catch (err) {
      console.error('[Deepgram] Parse error:', err.message);
    }
  }

  /**
   * Azure Format:
   * {
   *   "Type": "ConversationTranscription",
   *   "Id": "552502b2ed704e48940207fbe64ff3fe",
   *   "RecognitionStatus": "Success",
   *   "DisplayText": "Hello.",
   *   "Offset": 241100000,
   *   "Duration": 4400000,
   *   "Channel": 1,
   *   "SpeakerId": "Guest-1"
   * }
   */
  handleAzureTranscription(event) {
    const uuid = event.getHeader('Unique-ID');
    const callInfo = this.activeCalls.get(uuid);

    if (!callInfo) return;

    try {
      const body = event.getBody();
      const transcript = JSON.parse(body);

      const channel = transcript.Channel;
      const text = transcript.DisplayText;
      const speakerId = transcript.SpeakerId;
      const isFinal = transcript.RecognitionStatus === 'Success';

      // Map channel to speaker name
      let speaker = this.mapChannelToSpeaker(channel, 'azure', callInfo);

      if (speakerId) {
        // With speaker diarization
        console.log(`[Azure] ${speaker.name} (${speaker.number}) - ${speakerId}: ${text} ${isFinal ? '✓' : '~'}`);
      } else {
        // Without speaker diarization
        console.log(`[Azure] ${speaker.name} (${speaker.number}): ${text} ${isFinal ? '✓' : '~'}`);
      }

    } catch (err) {
      console.error('[Azure] Parse error:', err.message);
    }
  }

  /**
   * Map channel identifier to speaker (caller or callee)
   */
  mapChannelToSpeaker(channelId, vendor, callInfo) {
    let isCaller = false;

    switch (vendor) {
      case 'aws':
        // AWS: "ch_0" or "ch_1"
        isCaller = (channelId === 'ch_0');
        break;

      case 'deepgram':
        // Deepgram: 0 or 1
        isCaller = (channelId === 0);
        break;

      case 'azure':
        // Azure: 0 or 1
        isCaller = (channelId === 0);
        break;
    }

    return isCaller ? callInfo.caller : callInfo.callee;
  }

  /**
   * Extract unique speakers from AWS items array
   */
  extractAWSSpeakers(items) {
    const speakers = new Set();
    for (const item of items) {
      if (item.speaker_label !== undefined) {
        speakers.add(item.speaker_label);
      }
    }
    return Array.from(speakers);
  }
}

// Start the parser
const parser = new ComprehensiveTranscriptionParser();

// Handle shutdown
process.on('SIGINT', () => {
  console.log('\nShutting down...');
  process.exit(0);
});

/**
 * Example Output:
 *
 * 📞 Call started [bba5d840-47d1-4245-8319-deda26f01b95]:
 *    Caller: John Doe (1002)
 *    Callee: Customer Service (1003)
 *
 * [Deepgram] John Doe (1002) - speakers detected:
 *   Speaker-0: just going on ✓
 *
 * [AWS] Customer Service (1003) - 2 speakers detected:
 *   Speaker-0: I said, hey brother, how are ✓
 *   Speaker-1: you ✓
 *
 * [Azure] John Doe (1002) - Guest-1: Hello, how are you? ✓
 *
 * 📞 Call ended [bba5d840-47d1-4245-8319-deda26f01b95]
 */
