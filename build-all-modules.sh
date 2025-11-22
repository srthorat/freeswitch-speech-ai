#!/bin/bash
# ============================================================================
# Build Script for FreeSWITCH Speech AI - All 5 Modules
# ============================================================================
# This script builds a single Docker image containing all FIVE modules:
#   - mod_audio_fork (WebSocket Streaming)
#   - mod_aws_transcribe (AWS Transcribe)
#   - mod_deepgram_transcribe (Deepgram)
#   - mod_azure_transcribe (Azure Cognitive Services)
#   - mod_google_transcribe (Google Cloud Speech-to-Text)
#
# Usage:
#   ./build-all-modules.sh
#
# Options:
#   --cpus N          Number of CPUs to use for build (default: 4)
#   --no-cache        Build without using cache
#   --aws-version V   AWS SDK C++ version (default: 1.11.345)
#   --grpc-version V  gRPC version (default: 1.64.2)
#   --tag TAG         Docker image tag (default: freeswitch-speech-ai:all-modules)
# ============================================================================

set -e

# Default values
BUILD_CPUS=4
AWS_SDK_VERSION="1.11.345"
GRPC_VERSION="1.64.2"
LIBWEBSOCKETS_VERSION="4.3.3"
IMAGE_TAG="freeswitch-speech-ai:all-modules"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if image exists for cache
if docker image inspect "${IMAGE_TAG}" >/dev/null 2>&1; then
    USE_CACHE="--cache-from ${IMAGE_TAG}"
    echo "ℹ️  Using cache from existing image: ${IMAGE_TAG}"
else
    USE_CACHE=""
    echo "ℹ️  No existing image found - building from scratch (this will take 45-60 minutes)"
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
        --grpc-version)
            GRPC_VERSION="$2"
            shift 2
            ;;
        --tag)
            IMAGE_TAG="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--cpus N] [--no-cache] [--aws-version V] [--grpc-version V] [--tag TAG]"
            exit 1
            ;;
    esac
done

echo "============================================="
echo "FreeSWITCH Speech AI - Build ALL 5 Modules"
echo "============================================="
echo "Image Tag: ${IMAGE_TAG}"
echo "Build CPUs: ${BUILD_CPUS}"
echo "AWS SDK Version: ${AWS_SDK_VERSION}"
echo "gRPC Version: ${GRPC_VERSION}"
echo "libwebsockets Version: ${LIBWEBSOCKETS_VERSION}"
if [ -z "$USE_CACHE" ]; then
    echo "Cache: Disabled (building from scratch)"
elif [ "$USE_CACHE" = "--no-cache" ]; then
    echo "Cache: Disabled (--no-cache flag)"
else
    echo "Cache: Enabled (using ${USE_CACHE})"
fi
echo "============================================="
echo ""

# Build the image
echo "Starting Docker build..."
echo ""

docker build \
    -f Dockerfile \
    -t "${IMAGE_TAG}" \
    --build-arg BUILD_CPUS="${BUILD_CPUS}" \
    --build-arg AWS_SDK_CPP_VERSION="${AWS_SDK_VERSION}" \
    --build-arg GRPC_VERSION="${GRPC_VERSION}" \
    --build-arg LIBWEBSOCKETS_VERSION="${LIBWEBSOCKETS_VERSION}" \
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
echo "  ✅ mod_audio_fork (WebSocket Streaming)"
echo "  ✅ mod_aws_transcribe (AWS Transcribe)"
echo "  ✅ mod_deepgram_transcribe (Deepgram)"
echo "  ✅ mod_azure_transcribe (Azure Cognitive Services)"
echo "  ✅ mod_google_transcribe (Google Cloud Speech-to-Text)"
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
echo "   docker exec freeswitch fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|aws|azure|google'"
echo ""
echo "Expected output:"
echo "   api,uuid_audio_fork,mod_audio_fork,/usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so"
echo "   api,uuid_aws_transcribe,mod_aws_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so"
echo "   api,uuid_azure_transcribe,mod_azure_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so"
echo "   api,uuid_deepgram_transcribe,mod_deepgram_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so"
echo "   api,uuid_google_transcribe,mod_google_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so"
echo ""
echo "4. Check for any module errors in logs:"
echo "   docker exec freeswitch fs_cli -x 'console loglevel 7'"
echo "   docker logs freeswitch | grep -iE 'audio_fork|deepgram|aws|azure|google' | grep -iE 'error|fail|unable'"
echo ""
echo "5. Stop and remove test container:"
echo "   docker stop freeswitch && docker rm freeswitch"
echo ""
echo "============================================="
echo "Quick Start"
echo "============================================="
echo ""
echo "To run with all API keys configured:"
echo "  ./run-all-modules.sh ${IMAGE_TAG} \\"
echo "    DEEPGRAM_KEY \\"
echo "    AZURE_KEY eastus \\"
echo "    AWS_ACCESS_KEY_ID AWS_SECRET_ACCESS_KEY us-east-1 \\"
echo "    /path/to/google-credentials.json"
echo ""
echo "Or use the run script for easier configuration:"
echo "  ./run-all-modules.sh ${IMAGE_TAG}"
echo ""
echo "============================================="
