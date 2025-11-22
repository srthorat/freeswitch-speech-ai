/**
 * Unified Speaker Name Mapper for AWS, Deepgram, and Azure Transcription
 *
 * This example shows how to map channel IDs to speaker names consistently
 * across all three transcription services using the same variables exported
 * from the FreeSWITCH dialplan.
 */

const { Connection } = require('modesl');

class UnifiedTranscriptionHandler {
  constructor() {
    this.activeCalls = new Map();
    this.conn = new Connection('127.0.0.1', 8021, 'ClueCon', () => {
      console.log('Connected to FreeSWITCH Event Socket');

      // Subscribe to all transcription events
      this.conn.subscribe([
        'CHANNEL_ANSWER',
        'CHANNEL_HANGUP',
        'aws_transcribe::transcription',
        'deepgram_transcribe::transcription',
        'azure_transcribe::transcription'
      ], () => {
        console.log('Subscribed to transcription events');
      });
    });

    this.setupEventHandlers();
  }

  setupEventHandlers() {
    // Store speaker info when call is answered
    this.conn.on('esl::event::CHANNEL_ANSWER', (event) => {
      const uuid = event.getHeader('Unique-ID');

      // Get speaker names and numbers (same for all modules)
      const callInfo = {
        caller: {
          name: event.getHeader('caller_name') || event.getHeader('Caller-Caller-ID-Name'),
          number: event.getHeader('caller_number') || event.getHeader('Caller-Caller-ID-Number')
        },
        callee: {
          name: event.getHeader('callee_name') || event.getHeader('Caller-Callee-ID-Name'),
          number: event.getHeader('callee_number') || event.getHeader('Caller-Destination-Number')
        }
      };

      this.activeCalls.set(uuid, callInfo);

      console.log(`Call started [${uuid}]:`);
      console.log(`  Caller: ${callInfo.caller.name} (${callInfo.caller.number})`);
      console.log(`  Callee: ${callInfo.callee.name} (${callInfo.callee.number})`);
    });

    // Clean up when call ends
    this.conn.on('esl::event::CHANNEL_HANGUP', (event) => {
      const uuid = event.getHeader('Unique-ID');
      this.activeCalls.delete(uuid);
      console.log(`Call ended [${uuid}]`);
    });

    // Handle AWS transcription
    this.conn.on('esl::event::aws_transcribe::transcription', (event) => {
      this.handleTranscription(event, 'AWS');
    });

    // Handle Deepgram transcription
    this.conn.on('esl::event::deepgram_transcribe::transcription', (event) => {
      this.handleTranscription(event, 'Deepgram');
    });

    // Handle Azure transcription
    this.conn.on('esl::event::azure_transcribe::transcription', (event) => {
      this.handleTranscription(event, 'Azure');
    });
  }

  /**
   * Unified transcription handler for all three services
   * Maps channel IDs to speaker names using the same exported variables
   */
  handleTranscription(event, vendor) {
    const uuid = event.getHeader('Unique-ID');
    const callInfo = this.activeCalls.get(uuid);

    if (!callInfo) {
      console.warn(`[${vendor}] No call info found for UUID ${uuid}`);
      return;
    }

    // Get speaker names from call info (same for all vendors)
    const callerName = callInfo.caller.name;
    const callerNumber = callInfo.caller.number;
    const calleeName = callInfo.callee.name;
    const calleeNumber = callInfo.callee.number;

    // Parse transcription JSON
    const body = event.getBody();
    const transcript = JSON.parse(body);

    // Map channel to speaker name (vendor-specific format, same logic)
    const speaker = this.mapChannelToSpeaker(transcript, vendor, callInfo);

    if (!speaker) {
      console.warn(`[${vendor}] Could not determine speaker for transcript`);
      return;
    }

    // Extract transcript text (vendor-specific format)
    const text = this.extractTranscriptText(transcript, vendor);
    const isFinal = this.isTranscriptFinal(transcript, vendor);

    // Display unified output
    console.log(
      `[${vendor}] ${speaker.name} (${speaker.number}): ${text} ${isFinal ? '✓' : '~'}`
    );

    // You can also emit events or send to your application
    this.emitTranscription({
      vendor,
      uuid,
      speaker: speaker.name,
      speakerNumber: speaker.number,
      text,
      isFinal,
      timestamp: new Date().toISOString()
    });
  }

  /**
   * Map channel ID to speaker (caller or callee)
   * Works for AWS, Deepgram, and Azure
   */
  mapChannelToSpeaker(transcript, vendor, callInfo) {
    let channelId = null;

    switch (vendor) {
      case 'AWS':
        // AWS format: {"channel_id": "ch_0"}
        channelId = transcript[0]?.channel_id;
        if (channelId === 'ch_0') return callInfo.caller;
        if (channelId === 'ch_1') return callInfo.callee;
        break;

      case 'Deepgram':
        // Deepgram format: {"channel_index": [0]}
        channelId = transcript.channel_index?.[0];
        if (channelId === 0) return callInfo.caller;
        if (channelId === 1) return callInfo.callee;
        break;

      case 'Azure':
        // Azure format: {"Channel": 0}
        channelId = transcript.Channel;
        if (channelId === 0) return callInfo.caller;
        if (channelId === 1) return callInfo.callee;
        break;
    }

    return null;
  }

  /**
   * Extract transcript text (vendor-specific format)
   */
  extractTranscriptText(transcript, vendor) {
    switch (vendor) {
      case 'AWS':
        return transcript[0]?.alternatives?.[0]?.transcript || '';

      case 'Deepgram':
        return transcript.channel?.alternatives?.[0]?.transcript || '';

      case 'Azure':
        return transcript.DisplayText || '';

      default:
        return '';
    }
  }

  /**
   * Check if transcript is final (vendor-specific format)
   */
  isTranscriptFinal(transcript, vendor) {
    switch (vendor) {
      case 'AWS':
        return transcript[0]?.is_final === true;

      case 'Deepgram':
        return transcript.is_final === true;

      case 'Azure':
        return transcript.RecognitionStatus === 'Success';

      default:
        return false;
    }
  }

  /**
   * Emit transcription event to your application
   */
  emitTranscription(data) {
    // Send to your application (WebSocket, HTTP, etc.)
    // Example: this.websocket.send(JSON.stringify(data));

    // Or store in database
    // Example: this.db.insertTranscription(data);

    // For now, just log structured data
    console.log('Transcription Event:', JSON.stringify(data, null, 2));
  }
}

// Start the handler
const handler = new UnifiedTranscriptionHandler();

// Keep process alive
process.on('SIGINT', () => {
  console.log('Shutting down...');
  process.exit(0);
});

/**
 * Example Output:
 *
 * Call started [bba5d840-47d1-4245-8319-deda26f01b95]:
 *   Caller: John Doe (1002)
 *   Callee: Customer Service (1003)
 *
 * [AWS] John Doe (1002): Hello, how can I help you? ✓
 * [Deepgram] Customer Service (1003): I need assistance with my order ✓
 * [Azure] John Doe (1002): Sure, let me look that up ~
 * [AWS] John Doe (1002): Sure, let me look that up for you ✓
 * [Deepgram] Customer Service (1003): Thank you ✓
 *
 * Call ended [bba5d840-47d1-4245-8319-deda26f01b95]
 */
