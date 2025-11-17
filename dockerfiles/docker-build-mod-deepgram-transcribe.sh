#!/bin/bash
# ============================================================================
# Docker Build Script for mod_deepgram_transcribe
# ============================================================================
# This script builds a Docker image with mod_deepgram_transcribe on top of
# srt2011/freeswitch-mod-audio-fork:latest (which has FreeSWITCH + libwebsockets)
#
# Only builds: mod_deepgram_transcribe (5-10 min - very fast!)
#
# Usage:
#   ./dockerfiles/docker-build-mod-deepgram-transcribe.sh [IMAGE_NAME]
#
# Example:
#   ./dockerfiles/docker-build-mod-deepgram-transcribe.sh srt2011/freeswitch-mod-deepgram-transcribe:latest
#   ./dockerfiles/docker-build-mod-deepgram-transcribe.sh  # Uses default name
# ============================================================================

set -e

# Default image name
IMAGE_NAME=${1:-freeswitch-mod-deepgram-transcribe:latest}
BASE_IMAGE="srt2011/freeswitch-mod-audio-fork:latest"

echo "============================================="
echo "mod_deepgram_transcribe Docker Build Script"
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

# Get number of CPUs for build
BUILD_CPUS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo "4")

echo "✅ Build configuration:"
echo "   Build CPUs:            ${BUILD_CPUS}"
echo "   Platform:              ${PLATFORM}"
echo ""

# Check if mod_deepgram_transcribe module exists
if [ ! -d "modules/mod_deepgram_transcribe" ]; then
    echo "❌ ERROR: modules/mod_deepgram_transcribe directory not found"
    echo "Please ensure you're running from the repository root"
    exit 1
fi

echo "✅ Found mod_deepgram_transcribe module"
echo ""

# Confirm build
echo "============================================="
echo "Ready to build Docker image"
echo "============================================="
echo ""
echo "What will be built:"
echo "  1. Pull base image: ${BASE_IMAGE}"
echo "  2. Compile mod_deepgram_transcribe module"
echo "  3. Validate module (static + runtime)"
echo "  4. Create runtime image"
echo ""
echo "Estimated build time:"
echo "  - Intel/AMD64: 5-10 minutes"
echo "  - Apple Silicon (with emulation): 10-15 minutes"
echo ""
echo "Note: Base image already contains:"
echo "  - FreeSWITCH 1.10.11 fully configured"
echo "  - libwebsockets 4.3.3 (for Deepgram WebSocket)"
echo "  - SIP extensions (1000, 1001) ready"
echo "  - Event Socket configured (port 8021)"
echo "  - mod_audio_fork (bonus!)"
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
echo "Step 2: Building mod_deepgram_transcribe..."
echo "============================================="
echo ""

# Record start time
START_TIME=$(date +%s)

docker build \
    --platform "$PLATFORM" \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --build-arg BUILD_CPUS="$BUILD_CPUS" \
    -f dockerfiles/Dockerfile.mod_deepgram_transcribe \
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
echo "1. Start FreeSWITCH with ports:"
echo "   docker run -d --name fs \\"
echo "     -p 5060:5060/udp \\"
echo "     -p 8021:8021/tcp \\"
echo "     -e DEEPGRAM_API_KEY=your-api-key \\"
echo "     ${IMAGE_NAME}"
echo ""
echo "2. Access fs_cli:"
echo "   docker exec -it fs fs_cli"
echo ""
echo "3. Verify mod_deepgram_transcribe loaded:"
echo "   docker exec -it fs fs_cli -x 'show modules' | grep deepgram"
echo ""
echo "4. Check both modules are loaded:"
echo "   docker exec -it fs fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram'"
echo ""
echo "============================================="
echo "Testing mod_deepgram_transcribe:"
echo "============================================="
echo ""
echo "API usage requires a Deepgram API key."
echo "Get one at: https://console.deepgram.com/"
echo ""
echo "Example API usage (in fs_cli or dialplan):"
echo "  # Set API key"
echo "  uuid_setvar <call-uuid> DEEPGRAM_API_KEY your-api-key"
echo ""
echo "  # Start transcription (basic)"
echo "  uuid_deepgram_transcribe <call-uuid> start en-US"
echo ""
echo "  # Start with interim results"
echo "  uuid_deepgram_transcribe <call-uuid> start en-US interim"
echo ""
echo "  # Set model and features"
echo "  uuid_setvar <call-uuid> DEEPGRAM_SPEECH_MODEL phonecall"
echo "  uuid_setvar <call-uuid> DEEPGRAM_SPEECH_TIER nova"
echo "  uuid_setvar <call-uuid> DEEPGRAM_SPEECH_DIARIZE true"
echo ""
echo "  # Stop transcription"
echo "  uuid_deepgram_transcribe <call-uuid> stop"
echo ""
echo "Full API documentation:"
echo "  modules/mod_deepgram_transcribe/README.md"
echo ""
echo "============================================="
echo "Available Features:"
echo "============================================="
echo ""
echo "✅ Real-time streaming transcription"
echo "✅ Speaker diarization"
echo "✅ Keyword boosting"
echo "✅ Named Entity Recognition (NER)"
echo "✅ Profanity filtering"
echo "✅ Automatic punctuation"
echo "✅ Redaction (PCI, SSN, etc.)"
echo "✅ Multiple models (general, phonecall, meeting, etc.)"
echo "✅ Interim and final results"
echo ""
echo "============================================="
echo "SIP Testing (Extensions Ready):"
echo "============================================="
echo ""
echo "The base image includes configured SIP extensions:"
echo "  Extension: 1000, Password: 1234"
echo "  Extension: 1001, Password: 1234"
echo ""
echo "Test calls with transcription:"
echo "  1. Register extension 1000 and 1001 in your SIP client"
echo "  2. Call from 1000 to 1001 (dial: 1001)"
echo "  3. Use uuid_deepgram_transcribe to start transcription"
echo "  4. Listen for deepgram_transcribe::transcription events"
echo ""
echo "============================================="
echo "Push to Docker Hub (Optional):"
echo "============================================="
echo ""
echo "Tag and push to your Docker Hub account:"
echo "  docker tag ${IMAGE_NAME} <username>/freeswitch-mod-deepgram-transcribe:latest"
echo "  docker push <username>/freeswitch-mod-deepgram-transcribe:latest"
echo ""
echo "============================================="
