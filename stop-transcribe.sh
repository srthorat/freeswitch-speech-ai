#!/bin/bash
# Stop Transcription Script

if [ -z "$1" ]; then
    echo "Usage: $0 <UUID> [v1|v2]"
    echo ""
    echo "Example:"
    echo "  $0 12345678-1234-1234-1234-123456789abc v2"
    exit 1
fi

UUID=$1
VERSION=${2:-v2}  # Default to v2

if [ "$VERSION" = "v1" ]; then
    echo "Stopping v1 API transcription..."
    fs_cli -x "uuid_google_transcribe $UUID stop"
else
    echo "Stopping v2 API transcription..."
    fs_cli -x "uuid_google_transcribe2 $UUID stop"
fi

echo "✅ Transcription stopped for UUID: $UUID"
