#!/bin/bash
# Quick Stereo Transcription Test Script
# Tests both v1 and v2 APIs with stereo and separate recognition per channel

echo "=== Stereo Transcription Test Script ==="
echo ""

# Check if UUID is provided
if [ -z "$1" ]; then
    echo "Usage: $0 <UUID> [v1|v2]"
    echo ""
    echo "Example:"
    echo "  $0 12345678-1234-1234-1234-123456789abc v2"
    echo ""
    echo "Active calls:"
    fs_cli -x "show channels" | grep -E "^[0-9a-f]{8}-"
    exit 1
fi

UUID=$1
VERSION=${2:-v2}  # Default to v2

echo "Testing with:"
echo "  UUID: $UUID"
echo "  API Version: $VERSION"
echo ""

# Check if FreeSWITCH is running
if ! pgrep -x freeswitch > /dev/null; then
    echo "❌ FreeSWITCH is not running!"
    exit 1
fi

echo "✅ FreeSWITCH is running"
echo ""

# Check credentials file
if [ ! -f /home/sthorat/st-stt-v2.json ]; then
    echo "❌ Credentials file not found: /home/sthorat/st-stt-v2.json"
    exit 1
fi

echo "✅ Credentials file exists"
echo ""

# Set channel variables for stereo with separate recognition
echo "Setting channel variables..."
fs_cli -x "uuid_setvar $UUID GOOGLE_SPEECH_SEPARATE_RECOGNITION_PER_CHANNEL true"
fs_cli -x "uuid_setvar $UUID GOOGLE_SPEECH_MODEL phone_call"
fs_cli -x "uuid_setvar $UUID GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION true"
fs_cli -x "uuid_setvar $UUID GOOGLE_SPEECH_ENABLE_WORD_TIME_OFFSETS true"

echo "✅ Channel variables set"
echo ""

# Start transcription based on version
if [ "$VERSION" = "v1" ]; then
    echo "Starting v1 API stereo transcription (8kHz)..."
    fs_cli -x "uuid_google_transcribe $UUID start en-US interim stereo 8k"
    COMMAND="uuid_google_transcribe"
else
    echo "Starting v2 API stereo transcription (8kHz)..."
    fs_cli -x "uuid_google_transcribe2 $UUID start en-US interim stereo 8k"
    COMMAND="uuid_google_transcribe2"
fi

echo ""
echo "✅ Transcription started!"
echo ""
echo "Configuration:"
echo "  - Mode: Stereo (caller and callee separate)"
echo "  - Separate Recognition: ENABLED"
echo "  - Model: phone_call (optimized for telephony)"
echo "  - Sample Rate: 8kHz"
echo "  - Language: en-US"
echo "  - Interim Results: ENABLED"
echo "  - Punctuation: ENABLED"
echo "  - Word Timestamps: ENABLED"
echo ""
echo "Monitor results:"
echo "  1. Watch logs: sudo tail -f /var/log/freeswitch/freeswitch.log | grep -i google"
echo "  2. Listen for events in fs_cli:"
echo "     /event CUSTOM google_transcribe::transcription"
echo ""
echo "To stop transcription:"
echo "  $COMMAND $UUID stop"
echo ""
echo "Or run:"
echo "  ./stop-transcribe.sh $UUID $VERSION"
echo ""
