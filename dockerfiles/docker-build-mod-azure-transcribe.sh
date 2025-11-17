#!/bin/bash
# ============================================================================
# Build Script for FreeSWITCH with mod_azure_transcribe
# ============================================================================
#
# This script builds a Docker image with FreeSWITCH and mod_azure_transcribe
# on top of the mod_deepgram_transcribe base image.
#
# Base image includes:
#   - FreeSWITCH 1.10.11
#   - mod_audio_fork
#   - mod_deepgram_transcribe
#
# This build adds:
#   - Microsoft Azure Cognitive Services Speech SDK
#   - mod_azure_transcribe
#
# Final image will have all three modules available.
#
# Usage:
#   ./dockerfiles/docker-build-mod-azure-transcribe.sh [IMAGE_NAME]
#
# Examples:
#   ./dockerfiles/docker-build-mod-azure-transcribe.sh
#   ./dockerfiles/docker-build-mod-azure-transcribe.sh srt2011/freeswitch-mod-azure-transcribe:latest
#   ./dockerfiles/docker-build-mod-azure-transcribe.sh my-registry/freeswitch-azure:v1
#
# ============================================================================

set -e

# Configuration
IMAGE_NAME=${1:-freeswitch-mod-azure-transcribe:latest}
BASE_IMAGE="srt2011/freeswitch-mod-deepgram-transcribe:latest"
AZURE_SPEECH_SDK_VERSION="1.38.0"

# Platform detection
PLATFORM="linux/amd64"
if [[ "$(uname -m)" == "arm64" ]] || [[ "$(uname -m)" == "aarch64" ]]; then
    echo "🔍 Detected ARM64 architecture (Apple Silicon or ARM Linux)"
    echo "⚠️  WARNING: Azure Speech SDK may have limited ARM64 support"
    echo "📋 Note: Building for linux/amd64 with emulation"
    echo ""
fi

# Detect number of CPU cores for parallel compilation
if command -v nproc &> /dev/null; then
    BUILD_CPUS=$(nproc)
elif command -v sysctl &> /dev/null; then
    BUILD_CPUS=$(sysctl -n hw.ncpu 2>/dev/null || echo "4")
else
    BUILD_CPUS=4
fi

echo "============================================="
echo "FreeSWITCH mod_azure_transcribe Docker Build"
echo "============================================="
echo ""
echo "Configuration:"
echo "  📦 Base Image:         ${BASE_IMAGE}"
echo "  🏷️  Target Image:       ${IMAGE_NAME}"
echo "  🔧 Azure SDK Version:  ${AZURE_SPEECH_SDK_VERSION}"
echo "  🖥️  Platform:           ${PLATFORM}"
echo "  ⚙️  Build CPUs:         ${BUILD_CPUS}"
echo "  📂 Build Context:      $(pwd)"
echo ""
echo "Build includes:"
echo "  ✅ mod_audio_fork (from base)"
echo "  ✅ mod_deepgram_transcribe (from base)"
echo "  🆕 mod_azure_transcribe (NEW)"
echo "  🆕 Microsoft Azure Speech SDK ${AZURE_SPEECH_SDK_VERSION}"
echo ""
echo "⏱️  Estimated build time:"
echo "  - Intel/AMD64: 15-20 minutes"
echo "  - Apple Silicon: 25-35 minutes (with emulation)"
echo ""

# Confirmation prompt
read -p "Continue with build? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "❌ Build cancelled"
    exit 1
fi

echo ""
echo "========================================="
echo "Step 1: Pulling base image..."
echo "========================================="
docker pull "$BASE_IMAGE"

echo ""
echo "========================================="
echo "Step 2: Building mod_azure_transcribe image..."
echo "========================================="
docker build \
    --platform "$PLATFORM" \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --build-arg AZURE_SPEECH_SDK_VERSION="$AZURE_SPEECH_SDK_VERSION" \
    --build-arg BUILD_CPUS="$BUILD_CPUS" \
    -f dockerfiles/Dockerfile.mod_azure_transcribe \
    -t "$IMAGE_NAME" \
    .

echo ""
echo "========================================="
echo "Build completed successfully! ✅"
echo "========================================="
echo ""
echo "Image details:"
docker images "$IMAGE_NAME" --format "table {{.Repository}}\t{{.Tag}}\t{{.Size}}\t{{.CreatedAt}}"
echo ""

echo "========================================="
echo "Quick verification test..."
echo "========================================="
echo ""
echo "Testing module dependencies..."
docker run --rm "$IMAGE_NAME" \
    ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so

echo ""
echo "========================================="
echo "Next steps:"
echo "========================================="
echo ""
echo "1. Test the image locally:"
echo "   docker run -d --name fs-test \\"
echo "     -p 5060:5060/udp \\"
echo "     -p 8021:8021/tcp \\"
echo "     -e AZURE_SUBSCRIPTION_KEY=your-azure-key \\"
echo "     -e AZURE_REGION=eastus \\"
echo "     ${IMAGE_NAME}"
echo ""
echo "2. Verify all three modules are loaded:"
echo "   docker exec fs-test fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|azure'"
echo ""
echo "   Expected output:"
echo "   api,uuid_audio_fork,mod_audio_fork,..."
echo "   api,uuid_deepgram_transcribe,mod_deepgram_transcribe,..."
echo "   api,azure_transcribe,mod_azure_transcribe,..."
echo ""
echo "3. Test Azure transcription in fs_cli:"
echo "   docker exec -it fs-test fs_cli"
echo "   freeswitch@internal> uuid_setvar <uuid> AZURE_SUBSCRIPTION_KEY your-key"
echo "   freeswitch@internal> uuid_setvar <uuid> AZURE_REGION eastus"
echo "   freeswitch@internal> azure_transcribe <uuid> start en-US interim"
echo ""
echo "4. Push to Docker Hub (optional):"
echo "   docker push ${IMAGE_NAME}"
echo ""
echo "For full documentation, see:"
echo "  - modules/mod_azure_transcribe/README.md"
echo "  - dockerfiles/README.md"
echo ""
echo "========================================="
echo "Build script completed! 🎉"
echo "========================================="
