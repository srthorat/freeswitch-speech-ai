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
echo "To run the container:"
echo "  docker run -d -p 5060:5060/udp -p 8021:8021 ${IMAGE_TAG}"
echo ""
echo "To test with fs_cli:"
echo "  docker exec -it <container-id> fs_cli"
echo ""
echo "============================================="
