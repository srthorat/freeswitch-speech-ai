#!/bin/bash
# Check FreeSWITCH Process Environment Variables

echo "=== FreeSWITCH Environment Check ==="
echo ""

# Find FreeSWITCH process
FS_PID=$(pgrep -x freeswitch | head -1)

if [ -z "$FS_PID" ]; then
    echo "❌ FreeSWITCH is not running"
    echo ""
    echo "Please start FreeSWITCH first:"
    echo "  systemctl start freeswitch"
    exit 1
fi

echo "✅ FreeSWITCH is running (PID: $FS_PID)"
echo ""

# Check process environment
echo "Checking environment variables for FreeSWITCH process:"
echo "--------------------------------------------------------"

if [ -r "/proc/$FS_PID/environ" ]; then
    echo ""
    echo "GOOGLE_APPLICATION_CREDENTIALS:"
    cat /proc/$FS_PID/environ | tr '\0' '\n' | grep "GOOGLE_APPLICATION_CREDENTIALS" || echo "  ❌ NOT SET"

    echo ""
    echo "GCP_PROJECT_ID:"
    cat /proc/$FS_PID/environ | tr '\0' '\n' | grep "GCP_PROJECT_ID" || echo "  ❌ NOT SET"

    echo ""
    echo "GCP_LOCATION:"
    cat /proc/$FS_PID/environ | tr '\0' '\n' | grep "GCP_LOCATION" || echo "  ❌ NOT SET"

    echo ""
    echo "LD_LIBRARY_PATH:"
    cat /proc/$FS_PID/environ | tr '\0' '\n' | grep "LD_LIBRARY_PATH" || echo "  ❌ NOT SET"
else
    echo "  ⚠️  Cannot read /proc/$FS_PID/environ (need root access)"
    echo ""
    echo "Run with sudo to check process environment:"
    echo "  sudo $0"
fi

echo ""
echo "=== Systemd Service Configuration ==="
echo ""
echo "Checking systemd environment configuration:"
echo "--------------------------------------------"

if [ -f /etc/systemd/system/freeswitch.service.d/freeswitch.conf ]; then
    echo "✅ Drop-in file exists: /etc/systemd/system/freeswitch.service.d/freeswitch.conf"
    echo ""
    cat /etc/systemd/system/freeswitch.service.d/freeswitch.conf
else
    echo "❌ Drop-in file NOT found: /etc/systemd/system/freeswitch.service.d/freeswitch.conf"
fi

echo ""
echo "=== Recommendations ==="
echo ""
echo "If environment variables are NOT set in the FreeSWITCH process:"
echo "1. Edit the systemd drop-in file to ensure variables are set"
echo "2. Reload systemd: systemctl daemon-reload"
echo "3. Restart FreeSWITCH: systemctl restart freeswitch"
echo "4. Run this script again to verify"
echo ""
