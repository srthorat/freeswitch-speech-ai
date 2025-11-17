#!/bin/bash
# ============================================================================
# Run FreeSWITCH Docker Image on MacBook
# ============================================================================
#
# Usage:
#   ./run-on-macbook.sh <docker-image-name>
#
# Examples:
#   ./run-on-macbook.sh srt2011/freeswitch-base:latest
#   ./run-on-macbook.sh srt2011/freeswitch-mod-audio-fork:latest
#   ./run-on-macbook.sh username/freeswitch-base:1.10.11
#
# ============================================================================

set -e

REMOTE_IMAGE=${1}
CONTAINER_NAME="freeswitch"

# Validation
if [ -z "$REMOTE_IMAGE" ]; then
    echo "❌ Error: Docker image name is required"
    echo ""
    echo "Usage: $0 <docker-image-name>"
    echo ""
    echo "Examples:"
    echo "  $0 srt2011/freeswitch-base:latest"
    echo "  $0 srt2011/freeswitch-mod-audio-fork:latest"
    echo "  $0 username/freeswitch-base:1.10.11"
    exit 1
fi

echo "============================================="
echo "FreeSWITCH MacBook Runner"
echo "============================================="
echo ""
echo "Image: $REMOTE_IMAGE"
echo "Container: $CONTAINER_NAME"
echo ""

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    echo "❌ Error: Docker is not running!"
    echo ""
    echo "Please start Docker Desktop and try again."
    exit 1
fi

echo "✅ Docker is running"
echo ""

# Stop and remove existing container if it exists
if docker ps -a | grep -q "$CONTAINER_NAME"; then
    echo "⚠️  Existing container found. Removing..."
    docker stop "$CONTAINER_NAME" 2>/dev/null || true
    docker rm "$CONTAINER_NAME" 2>/dev/null || true
    echo "✅ Old container removed"
    echo ""
fi

# Pull image from Docker Hub
echo "Step 1: Pulling image from Docker Hub..."
echo "This may take 5-10 minutes depending on your internet speed..."
docker pull "$REMOTE_IMAGE"
echo "✅ Image pulled successfully"
echo ""

# Run container
echo "Step 2: Starting FreeSWITCH container..."
docker run -d \
    --name "$CONTAINER_NAME" \
    --platform linux/amd64 \
    --net=host \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 18021:8021/tcp \
    -p 16384-16484:16384-16484/udp \
    "$REMOTE_IMAGE"

echo "✅ Container started"
echo ""

# Wait for FreeSWITCH to start
echo "Step 3: Waiting for FreeSWITCH to start (30 seconds)..."
sleep 30

# Check if container is still running
if ! docker ps | grep -q "$CONTAINER_NAME"; then
    echo "❌ Error: Container stopped unexpectedly!"
    echo ""
    echo "Container logs:"
    docker logs "$CONTAINER_NAME"
    exit 1
fi

echo "✅ Container is running"
echo ""

# Test fs_cli connection
echo "Step 4: Testing fs_cli connection..."
if docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "status" >/dev/null 2>&1; then
    echo "✅ fs_cli connected successfully"
else
    echo "⚠️  Warning: fs_cli connection failed (FreeSWITCH may still be starting)"
fi
echo ""

# Show FreeSWITCH status
echo "============================================="
echo "FreeSWITCH Status"
echo "============================================="
docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "status" || echo "Status command failed"
echo ""

# Show SIP profiles
echo "============================================="
echo "SIP Profiles"
echo "============================================="
docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "sofia status" || echo "Sofia status failed"
echo ""

# Get MacBook IP
echo "============================================="
echo "Network Information"
echo "============================================="
echo "Container IP: $(docker inspect "$CONTAINER_NAME" | grep '"IPAddress"' | head -1 | awk -F'"' '{print $4}')"
echo ""
echo "MacBook IP addresses:"
ifconfig | grep "inet " | grep -v 127.0.0.1 | awk '{print "  - " $2}'
echo ""
echo "For SIP clients on the same MacBook, use: localhost or 127.0.0.1"
echo "For SIP clients on other devices, use one of the MacBook IPs above"
echo ""

echo "============================================="
echo "✅ FreeSWITCH is Ready!"
echo "============================================="
echo ""
echo "Container Name: $CONTAINER_NAME"
echo ""
echo "Extension Credentials:"
echo "  Extension 1000: username=1000, password=1234"
echo "  Extension 1001: username=1001, password=1234"
echo ""
echo "SIP Server: localhost:5060 (UDP)"
echo ""
echo "Useful Commands:"
echo "  - Access fs_cli:       docker exec -it $CONTAINER_NAME fs_cli"
echo "  - View logs:           docker logs -f $CONTAINER_NAME"
echo "  - Stop container:      docker stop $CONTAINER_NAME"
echo "  - Restart container:   docker restart $CONTAINER_NAME"
echo "  - Remove container:    docker rm -f $CONTAINER_NAME"
echo ""
echo "Next Steps:"
echo "  1. Install a SIP client (Zoiper, Linphone, etc.)"
echo "  2. Register extension 1000 and 1001"
echo "  3. Call from 1000 to 1001 (dial: 1001)"
echo "  4. Test echo service (dial: 9196)"
echo ""
echo "For detailed instructions, see:"
echo "  dockerfiles/DOCKER_HUB_DEPLOYMENT.md"
echo ""
