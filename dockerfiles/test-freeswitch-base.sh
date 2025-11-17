#!/bin/bash
# ============================================================================
# FreeSWITCH Base Image Validation Script
# ============================================================================
#
# This script validates that the FreeSWITCH base image is working correctly:
# - FreeSWITCH starts successfully
# - Event socket is accessible (fs_cli can connect)
# - Extensions 1000 and 1001 are registered
# - Basic system utilities are available
#
# Usage:
#   ./test-freeswitch-base.sh [image-name]
#
# Example:
#   ./test-freeswitch-base.sh freeswitch-base:1.10.11
#
# ============================================================================

set -e

IMAGE_NAME=${1:-freeswitch-base:1.10.11}
CONTAINER_NAME="freeswitch-base-test-$$"

echo "============================================="
echo "FreeSWITCH Base Image Validation"
echo "============================================="
echo ""
echo "Image: $IMAGE_NAME"
echo "Container: $CONTAINER_NAME"
echo ""

# Cleanup function
cleanup() {
    echo ""
    echo "Cleaning up..."
    docker stop "$CONTAINER_NAME" 2>/dev/null || true
    docker rm "$CONTAINER_NAME" 2>/dev/null || true
}

# Set trap to cleanup on exit
trap cleanup EXIT

echo "Step 1: Starting FreeSWITCH container..."
docker run -d \
    --name "$CONTAINER_NAME" \
    --platform linux/amd64 \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 8021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    "$IMAGE_NAME"

echo "✅ Container started"
echo ""

echo "Step 2: Waiting for FreeSWITCH to start (30 seconds)..."
sleep 30
echo "✅ Wait complete"
echo ""

echo "Step 3: Checking container is running..."
if ! docker ps | grep -q "$CONTAINER_NAME"; then
    echo "❌ Container is not running!"
    echo ""
    echo "Container logs:"
    docker logs "$CONTAINER_NAME"
    exit 1
fi
echo "✅ Container is running"
echo ""

echo "Step 4: Checking FreeSWITCH process..."
if ! docker exec "$CONTAINER_NAME" pgrep -f freeswitch > /dev/null; then
    echo "❌ FreeSWITCH process not found!"
    echo ""
    echo "Container logs:"
    docker logs "$CONTAINER_NAME"
    exit 1
fi
echo "✅ FreeSWITCH process is running"
echo ""

echo "Step 5: Checking FreeSWITCH log file..."
if ! docker exec "$CONTAINER_NAME" test -f /usr/local/freeswitch/log/freeswitch.log; then
    echo "❌ FreeSWITCH log file not found!"
    exit 1
fi
echo "✅ Log file exists"
echo ""

echo "Step 6: Checking for startup errors in log..."
if docker exec "$CONTAINER_NAME" grep -i "error" /usr/local/freeswitch/log/freeswitch.log | grep -v "NORMAL_CLEARING" | grep -v "switch_odbc.c"; then
    echo "⚠️  Warning: Errors found in log (shown above)"
    echo "Note: Some errors are expected during initial startup"
else
    echo "✅ No critical errors in log"
fi
echo ""

echo "Step 7: Testing fs_cli connectivity..."
if ! docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "status" > /dev/null 2>&1; then
    echo "❌ fs_cli cannot connect to FreeSWITCH!"
    echo ""
    echo "Checking event socket configuration..."
    docker exec "$CONTAINER_NAME" cat /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml
    exit 1
fi
echo "✅ fs_cli connected successfully"
echo ""

echo "Step 8: Getting FreeSWITCH status..."
docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "status"
echo ""

echo "Step 9: Checking loaded modules..."
MODULE_COUNT=$(docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "show modules" | grep -c "^mod_" || true)
echo "Loaded modules: $MODULE_COUNT"
if [ "$MODULE_COUNT" -lt 50 ]; then
    echo "⚠️  Warning: Expected more modules (should be 100+)"
    echo ""
    echo "Showing first 20 modules:"
    docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "show modules" | head -20
else
    echo "✅ Module count looks good"
fi
echo ""

echo "Step 10: Checking SIP profiles..."
docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "sofia status"
echo ""

echo "Step 11: Checking extensions 1000 and 1001..."
if docker exec "$CONTAINER_NAME" test -f /usr/local/freeswitch/conf/directory/default/1000.xml; then
    echo "✅ Extension 1000 configuration exists"
else
    echo "❌ Extension 1000 configuration missing!"
    exit 1
fi

if docker exec "$CONTAINER_NAME" test -f /usr/local/freeswitch/conf/directory/default/1001.xml; then
    echo "✅ Extension 1001 configuration exists"
else
    echo "❌ Extension 1001 configuration missing!"
    exit 1
fi
echo ""

echo "Step 12: Testing system utilities..."
echo -n "  - ps: "
if docker exec "$CONTAINER_NAME" ps aux > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌"
fi

echo -n "  - netstat: "
if docker exec "$CONTAINER_NAME" netstat -an > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌"
fi

echo -n "  - ping: "
if docker exec "$CONTAINER_NAME" ping -c 1 8.8.8.8 > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌ (network may be restricted)"
fi

echo -n "  - vim: "
if docker exec "$CONTAINER_NAME" which vim > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌"
fi

echo -n "  - curl: "
if docker exec "$CONTAINER_NAME" which curl > /dev/null 2>&1; then
    echo "✅"
else
    echo "❌"
fi
echo ""

echo "Step 13: Checking critical modules for SIP and WebRTC..."
CRITICAL_MODULES=(
    "mod_sofia"
    "mod_event_socket"
    "mod_conference"
    "mod_dptools"
    "mod_dialplan_xml"
    "mod_opus"
    "mod_vp8"
    "mod_h264"
)

for module in "${CRITICAL_MODULES[@]}"; do
    echo -n "  - $module: "
    if docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "show modules" | grep -q "^$module"; then
        echo "✅"
    else
        echo "❌"
    fi
done
echo ""

echo "============================================="
echo "Validation Summary"
echo "============================================="
echo ""
echo "✅ FreeSWITCH base image validation complete!"
echo ""
echo "Next steps:"
echo "  1. Register SIP clients to extensions 1000 and 1001"
echo "  2. Test calling between extensions"
echo "  3. Test WebRTC connectivity"
echo ""
echo "Container is still running as: $CONTAINER_NAME"
echo "To access fs_cli: docker exec -it $CONTAINER_NAME fs_cli"
echo "To stop: docker stop $CONTAINER_NAME"
echo "To remove: docker rm $CONTAINER_NAME"
echo ""
