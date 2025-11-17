#!/bin/bash
# ============================================================================
# Docker Build Script for mod_audio_fork
# ============================================================================
# This script builds a Docker image with mod_audio_fork on top of
# srt2011/freeswitch-base:latest (production FreeSWITCH with all configs)
#
# Only builds: libwebsockets + mod_audio_fork (10-15 min vs 90+ min)
#
# Usage:
#   ./dockerfiles/docker-build-mod-audio-fork.sh [IMAGE_NAME]
#
# Example:
#   ./dockerfiles/docker-build-mod-audio-fork.sh srt2011/freeswitch-mod-audio-fork:latest
#   ./dockerfiles/docker-build-mod-audio-fork.sh  # Uses default name
# ============================================================================

set -e

# Default image name
IMAGE_NAME=${1:-freeswitch-mod-audio-fork:latest}
BASE_IMAGE="srt2011/freeswitch-base:latest"

echo "============================================="
echo "mod_audio_fork Docker Build Script"
echo "============================================="
echo ""
echo "Base Image: ${BASE_IMAGE}"
echo "Target Image: ${IMAGE_NAME}"
echo ""

# Detect platform
PLATFORM="linux/amd64"
if [[ "$(uname -m)" == "arm64" ]] || [[ "$(uname -m)" == "aarch64" ]]; then
    echo "🔍 Detected ARM64 architecture (Apple Silicon or ARM Linux)"
    echo "📋 Note: Building for linux/amd64 with emulation"
    echo "⚠️  This will be slower on Apple Silicon Macs"
    echo ""
fi

# Read libwebsockets version from .env (or use default)
LIBWEBSOCKETS_VERSION="4.3.3"
if [ -f ".env" ]; then
    LWS_FROM_ENV=$(grep libwebsocketsVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
    if [ -n "$LWS_FROM_ENV" ]; then
        LIBWEBSOCKETS_VERSION="$LWS_FROM_ENV"
    fi
fi

# Get number of CPUs for build
BUILD_CPUS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")

echo "✅ Build configuration:"
echo "   libwebsockets Version: ${LIBWEBSOCKETS_VERSION}"
echo "   Build CPUs:            ${BUILD_CPUS}"
echo "   Platform:              ${PLATFORM}"
echo ""

# Check if mod_audio_fork module exists
if [ ! -d "modules/mod_audio_fork" ]; then
    echo "❌ ERROR: modules/mod_audio_fork directory not found"
    echo "Please ensure you're running from the repository root"
    exit 1
fi

echo "✅ Found mod_audio_fork module"
echo ""

# Confirm build
echo "============================================="
echo "Ready to build Docker image for mod_audio_fork"
echo "============================================="
echo ""
echo "What will be built:"
echo "  1. Pull base image: ${BASE_IMAGE}"
echo "  2. Build libwebsockets ${LIBWEBSOCKETS_VERSION}"
echo "  3. Compile mod_audio_fork module"
echo "  4. Validate module (static + runtime)"
echo "  5. Create runtime image with validation script"
echo ""
echo "Estimated build time:"
echo "  - Intel/AMD64: 10-15 minutes"
echo "  - Apple Silicon (with emulation): 20-30 minutes"
echo ""
echo "Note: Base image already contains:"
echo "  - FreeSWITCH 1.10.11 fully configured"
echo "  - SIP extensions (1000, 1001) ready"
echo "  - Event Socket configured (port 8021)"
echo ""

read -p "Press Enter to continue or Ctrl+C to cancel..."
echo ""

# Pull base image first
echo "============================================="
echo "Step 1: Pulling base image..."
echo "============================================="
docker pull "$BASE_IMAGE"
echo ""

# Build Docker image with all build arguments
echo "============================================="
echo "Step 2: Building mod_audio_fork image..."
echo "============================================="
echo ""

# Record start time
START_TIME=$(date +%s)

docker build \
    --platform "$PLATFORM" \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --build-arg LIBWEBSOCKETS_VERSION="$LIBWEBSOCKETS_VERSION" \
    --build-arg BUILD_CPUS="$BUILD_CPUS" \
    -f dockerfiles/Dockerfile.mod_audio_fork \
    -t "$IMAGE_NAME" \
    .

# Record end time
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
MINUTES=$((DURATION / 60))
SECONDS=$((DURATION % 60))

echo ""
echo "============================================="
echo "✅ Build completed successfully!"
echo "============================================="
echo ""
echo "Build time: ${MINUTES}m ${SECONDS}s"
echo "Image name: $IMAGE_NAME"
echo ""
echo "============================================="
echo "Next Steps - Quick Start:"
echo "============================================="
echo ""
echo "1. Validate module installation:"
echo "   docker run --rm ${IMAGE_NAME}"
echo ""
echo "2. Start FreeSWITCH with ports:"
echo "   docker run -d --name fs \\"
echo "     -p 5060:5060/udp \\"
echo "     -p 8021:8021/tcp \\"
echo "     ${IMAGE_NAME} freeswitch -nc -nf"
echo ""
echo "3. Access fs_cli:"
echo "   docker exec -it fs fs_cli"
echo ""
echo "4. Verify mod_audio_fork loaded:"
echo "   docker exec -it fs fs_cli -x 'show modules' | grep audio_fork"
echo ""
echo "============================================="
echo "Testing mod_audio_fork:"
echo "============================================="
echo ""
echo "mod_audio_fork requires a WebSocket server to receive audio."
echo ""
echo "Example API usage (in fs_cli):"
echo "  uuid_audio_fork <call-uuid> start ws://server:port mono 8k {}"
echo "  uuid_audio_fork <call-uuid> send_text {\"event\":\"dtmf\"}"
echo "  uuid_audio_fork <call-uuid> stop"
echo ""
echo "Full API documentation:"
echo "  modules/mod_audio_fork/README.md"
echo ""
echo "============================================="
echo "SIP Testing (Extensions Ready):"
echo "============================================="
echo ""
echo "The base image includes configured SIP extensions:"
echo "  Extension: 1000, Password: 1234"
echo "  Extension: 1001, Password: 1234"
echo ""
echo "Test calls between extensions:"
echo "  1. Register extension 1000 and 1001 in your SIP client"
echo "  2. Call from 1000 to 1001 (dial: 1001)"
echo "  3. Use uuid_audio_fork to stream audio to WebSocket"
echo ""
echo "============================================="
echo "Push to Docker Hub (Optional):"
echo "============================================="
echo ""
echo "Tag and push to your Docker Hub account:"
echo "  docker tag ${IMAGE_NAME} <username>/freeswitch-mod-audio-fork:latest"
echo "  docker push <username>/freeswitch-mod-audio-fork:latest"
echo ""
echo "============================================="
