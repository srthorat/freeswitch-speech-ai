#!/bin/bash
# ============================================================================
# Build Script for FreeSWITCH Speech AI - All Modules
# ============================================================================
# This script builds a single Docker image containing all three modules:
#   - mod_audio_fork
#   - mod_aws_transcribe
#   - mod_deepgram_transcribe
#
# Usage:
#   ./dockerfiles/build-all-modules.sh
#
# Options:
#   --cpus N          Number of CPUs to use for build (default: 4)
#   --no-cache        Build without using cache
#   --aws-version V   AWS SDK C++ version (default: 1.11.345)
#   --tag TAG         Docker image tag (default: freeswitch-speech-ai:latest)
# ============================================================================

set -e

# Default values
BUILD_CPUS=4
AWS_SDK_VERSION="1.11.345"
IMAGE_TAG="freeswitch-speech-ai:latest"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Check if image exists for cache, use it if available
if docker image inspect freeswitch-speech-ai:latest >/dev/null 2>&1; then
    USE_CACHE="--cache-from freeswitch-speech-ai:latest"
    echo "ℹ️  Using cache from existing image: freeswitch-speech-ai:latest"
else
    USE_CACHE=""
    echo "ℹ️  No existing image found - building from scratch (this will take 30-40 minutes)"
fi

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --cpus)
            BUILD_CPUS="$2"
            shift 2
            ;;
        --no-cache)
            USE_CACHE="--no-cache"
            shift
            ;;
        --aws-version)
            AWS_SDK_VERSION="$2"
            shift 2
            ;;
        --tag)
            IMAGE_TAG="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--cpus N] [--no-cache] [--aws-version V] [--tag TAG]"
            exit 1
            ;;
    esac
done

echo "============================================="
echo "FreeSWITCH Speech AI - Build All Modules"
echo "============================================="
echo "Image Tag: ${IMAGE_TAG}"
echo "Build CPUs: ${BUILD_CPUS}"
echo "AWS SDK Version: ${AWS_SDK_VERSION}"
if [ -z "$USE_CACHE" ]; then
    echo "Cache: Disabled (building from scratch)"
elif [ "$USE_CACHE" = "--no-cache" ]; then
    echo "Cache: Disabled (--no-cache flag)"
else
    echo "Cache: Enabled (using ${USE_CACHE})"
fi
echo "============================================="
echo ""

# Change to project root
cd "$PROJECT_ROOT"

# Build the image
echo "Starting Docker build..."
echo ""

docker build \
    -f dockerfiles/Dockerfile.all-modules \
    -t "${IMAGE_TAG}" \
    --build-arg BUILD_CPUS="${BUILD_CPUS}" \
    --build-arg AWS_SDK_CPP_VERSION="${AWS_SDK_VERSION}" \
    ${USE_CACHE} \
    .

echo ""
echo "============================================="
echo "✅ Build Complete!"
echo "============================================="
echo ""
echo "Image: ${IMAGE_TAG}"
echo ""
echo "Modules included:"
echo "  ✅ mod_audio_fork"
echo "  ✅ mod_aws_transcribe"
echo "  ✅ mod_deepgram_transcribe"
echo ""
echo "============================================="
echo "Manual Verification Steps"
echo "============================================="
echo ""
echo "1. Start the container:"
echo "   docker run -d --name freeswitch -p 5060:5060/udp -p 8021:8021 ${IMAGE_TAG}"
echo ""
echo "2. Wait for FreeSWITCH to start (30 seconds):"
echo "   sleep 30"
echo ""
echo "3. Verify all modules are loaded:"
echo "   docker exec freeswitch fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|aws'"
echo ""
echo "Expected output:"
echo "   api,uuid_audio_fork,mod_audio_fork,/usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so"
echo "   api,uuid_aws_transcribe,mod_aws_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so"
echo "   api,uuid_deepgram_transcribe,mod_deepgram_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so"
echo ""
echo "4. Check for any module errors in logs:"
echo "   docker exec freeswitch fs_cli -x 'console loglevel 7'"
echo "   docker logs freeswitch | grep -iE 'audio_fork|deepgram|aws_transcribe' | grep -iE 'error|fail|unable'"
echo ""
echo "5. Stop and remove test container:"
echo "   docker stop freeswitch && docker rm freeswitch"
echo ""
echo "============================================="
echo "Quick Start"
echo "============================================="
echo ""
echo "To run with AWS credentials:"
echo "  docker run -d --name freeswitch \\"
echo "    -p 5060:5060/udp -p 8021:8021 \\"
echo "    -e AWS_ACCESS_KEY_ID=AKIA**************** \\"
echo "    -e AWS_SECRET_ACCESS_KEY=**************************************** \\"
echo "    -e AWS_REGION=us-east-1 \\"
echo "    ${IMAGE_TAG}"
echo ""
echo "To run with Deepgram API key:"
echo "  docker run -d --name freeswitch \\"
echo "    -p 5060:5060/udp -p 8021:8021 \\"
echo "    -e DEEPGRAM_API_KEY=your-api-key \\"
echo "    ${IMAGE_TAG}"
echo ""
echo "For MacBook setup, use:"
echo "  ./dockerfiles/run-on-macbook.sh ${IMAGE_TAG}"
echo ""
echo "============================================="
