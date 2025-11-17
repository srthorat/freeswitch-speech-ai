#!/bin/bash
set -e

echo "==========================================="
echo "FreeSWITCH Complete Cleanup Script"
echo "==========================================="
echo ""
echo "WARNING: This will completely remove FreeSWITCH from your system!"
echo "Press Ctrl+C within 5 seconds to cancel..."
sleep 5

echo ""
echo "Step 1: Stopping FreeSWITCH processes..."
if pgrep -x "freeswitch" > /dev/null; then
    echo "  Killing FreeSWITCH processes..."
    pkill -9 freeswitch || true
    sleep 2
    echo "  ✓ FreeSWITCH processes stopped"
else
    echo "  ✓ No FreeSWITCH processes running"
fi

echo ""
echo "Step 2: Removing FreeSWITCH installation directory..."
if [ -d "/usr/local/freeswitch" ]; then
    echo "  Removing /usr/local/freeswitch..."
    rm -rf /usr/local/freeswitch
    echo "  ✓ Removed /usr/local/freeswitch"
else
    echo "  ✓ /usr/local/freeswitch not found"
fi

echo ""
echo "Step 3: Removing FreeSWITCH binaries..."
BINARIES="freeswitch fs_cli fs_encode fs_decode freeswitch-config tone2wav"
for bin in $BINARIES; do
    if [ -f "/usr/local/bin/$bin" ]; then
        echo "  Removing /usr/local/bin/$bin"
        rm -f "/usr/local/bin/$bin"
    fi
done
echo "  ✓ FreeSWITCH binaries removed"

echo ""
echo "Step 4: Removing FreeSWITCH libraries..."
echo "  Removing FreeSWITCH shared libraries from /usr/local/lib..."
rm -f /usr/local/lib/libfreeswitch*.so* 2>/dev/null || true
echo "  ✓ FreeSWITCH libraries removed"

echo ""
echo "Step 5: Removing FreeSWITCH include files..."
if [ -d "/usr/local/include/freeswitch" ]; then
    echo "  Removing /usr/local/include/freeswitch..."
    rm -rf /usr/local/include/freeswitch
    echo "  ✓ Removed /usr/local/include/freeswitch"
else
    echo "  ✓ No FreeSWITCH include files found"
fi

echo ""
echo "Step 6: Removing pkg-config files..."
rm -f /usr/local/lib/pkgconfig/freeswitch.pc 2>/dev/null || true
echo "  ✓ pkg-config files removed"

echo ""
echo "Step 7: Removing systemd service (if exists)..."
if [ -f "/etc/systemd/system/freeswitch.service" ]; then
    systemctl stop freeswitch 2>/dev/null || true
    systemctl disable freeswitch 2>/dev/null || true
    rm -f /etc/systemd/system/freeswitch.service
    systemctl daemon-reload
    echo "  ✓ Systemd service removed"
else
    echo "  ✓ No systemd service found"
fi

echo ""
echo "Step 8: Updating library cache..."
ldconfig
echo "  ✓ Library cache updated"

echo ""
echo "Step 9: Checking for remaining FreeSWITCH files..."
REMAINING=$(find /usr/local -name "*freeswitch*" 2>/dev/null || true)
if [ -n "$REMAINING" ]; then
    echo "  WARNING: Found remaining FreeSWITCH files:"
    echo "$REMAINING"
    echo ""
    echo "  Remove them manually if needed:"
    echo "  find /usr/local -name '*freeswitch*' -exec rm -rf {} +"
else
    echo "  ✓ No remaining FreeSWITCH files found"
fi

echo ""
echo "==========================================="
echo "✅ FreeSWITCH CLEANUP COMPLETE"
echo "==========================================="
echo ""
echo "Summary:"
echo "  - FreeSWITCH processes stopped"
echo "  - Installation directory removed"
echo "  - Binaries removed"
echo "  - Libraries removed"
echo "  - Include files removed"
echo "  - System services removed"
echo ""
echo "Your system is now clean of FreeSWITCH."
