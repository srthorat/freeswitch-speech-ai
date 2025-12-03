#!/bin/bash
# Quick test script for Google Transcribe v1 and v2

echo "=== Google Transcribe Test Script ==="
echo ""

# Check if FreeSWITCH is running
if ! pgrep -x freeswitch > /dev/null; then
    echo "❌ FreeSWITCH is not running!"
    echo "Start it with: sudo systemctl start freeswitch"
    exit 1
fi

echo "✅ FreeSWITCH is running"
echo ""

# Check credentials file
if [ ! -f /home/sthorat/st-stt-v2.json ]; then
    echo "❌ Credentials file not found: /home/sthorat/st-stt-v2.json"
    echo "Please upload the file first"
    exit 1
fi

echo "✅ Credentials file exists"
echo ""

# Get active channels
echo "Active channels:"
echo "----------------"
fs_cli -x "show channels" | grep -E "^[0-9a-f]{8}-" | head -5

echo ""
echo "=== Test Commands ==="
echo ""
echo "For v1 API (uuid_google_transcribe):"
echo "  fs_cli -x 'uuid_google_transcribe <UUID> start en-US interim'"
echo ""
echo "For v2 API (uuid_google_transcribe2):"
echo "  fs_cli -x 'uuid_google_transcribe2 <UUID> start en-US interim'"
echo ""
echo "Replace <UUID> with actual call UUID from above"
echo ""
echo "=== Monitor Logs ==="
echo "  sudo tail -f /var/log/freeswitch/freeswitch.log | grep -i google"
echo ""
