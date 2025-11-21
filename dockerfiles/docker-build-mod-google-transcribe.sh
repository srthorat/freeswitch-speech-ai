#!/bin/bash
# ============================================================================
# Build Script for FreeSWITCH with mod_google_transcribe
# ============================================================================
#
# This script builds a Docker image with FreeSWITCH and mod_google_transcribe
# on top of the freeswitch-base image.
#
# Base image includes:
#   - FreeSWITCH 1.10.11 fully built and configured
#   - All required SIP configurations
#   - Event Socket configuration
#
# This build adds:
#   - gRPC and Protocol Buffers
#   - googleapis (Google Cloud Speech API)
#   - mod_google_transcribe
#
# Usage:
#   ./dockerfiles/docker-build-mod-google-transcribe.sh [IMAGE_NAME] [GRPC_VERSION]
#
# Examples:
#   ./dockerfiles/docker-build-mod-google-transcribe.sh
#   ./dockerfiles/docker-build-mod-google-transcribe.sh srt2011/freeswitch-mod-google-transcribe:latest
#   ./dockerfiles/docker-build-mod-google-transcribe.sh my-registry/freeswitch-google:v1 1.65.0
#
# ============================================================================

set -e

# Configuration
IMAGE_NAME=${1:-freeswitch-mod-google-transcribe:latest}
# gRPC version: 1.64.2 (stable, tested) | Latest: Check https://github.com/grpc/grpc/releases
# To upgrade: ./docker-build-mod-google-transcribe.sh IMAGE_NAME 1.65.0
GRPC_VERSION=${2:-1.64.2}
BASE_IMAGE="srt2011/freeswitch-base:latest"

# Platform detection
PLATFORM="linux/amd64"
if [[ "$(uname -m)" == "arm64" ]] || [[ "$(uname -m)" == "aarch64" ]]; then
    echo "🔍 Detected ARM64 architecture (Apple Silicon or ARM Linux)"
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
echo "FreeSWITCH mod_google_transcribe Docker Build"
echo "============================================="
echo ""
echo "Configuration:"
echo "  📦 Base Image:       ${BASE_IMAGE}"
echo "  🏷️  Target Image:     ${IMAGE_NAME}"
echo "  🔧 gRPC Version:     ${GRPC_VERSION}"
echo "  🖥️  Platform:         ${PLATFORM}"
echo "  ⚙️  Build CPUs:       ${BUILD_CPUS}"
echo "  📂 Build Context:    $(pwd)"
echo ""
echo "Build includes:"
echo "  🆕 gRPC v${GRPC_VERSION} (with Protocol Buffers)"
echo "  🆕 googleapis (Google Cloud Speech API protobuf definitions)"
echo "  🆕 mod_google_transcribe"
echo ""
echo "⏱️  Estimated build time:"
echo "  - Intel/AMD64: 30-45 minutes"
echo "  - Apple Silicon: 50-60 minutes (with emulation)"
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
echo "Step 2: Building mod_google_transcribe image..."
echo "========================================="
docker build \
    --platform "$PLATFORM" \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --build-arg GRPC_VERSION="$GRPC_VERSION" \
    --build-arg BUILD_CPUS="$BUILD_CPUS" \
    -f dockerfiles/Dockerfile.mod_google_transcribe \
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
    ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so

echo ""
echo "Testing gRPC libraries..."
docker run --rm "$IMAGE_NAME" \
    ls -lh /usr/local/lib/libgrpc++.so

echo ""
echo "========================================="
echo "Next steps:"
echo "========================================="
echo ""
echo "1. Test the image locally:"
echo "   docker run -d --name fs-test \\"
echo "     -p 5060:5060/udp \\"
echo "     -p 8021:8021/tcp \\"
echo "     -e GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account.json \\"
echo "     -v /path/to/service-account.json:/path/to/service-account.json:ro \\"
echo "     ${IMAGE_NAME}"
echo ""
echo "2. Verify module is loaded:"
echo "   docker exec fs-test fs_cli -x 'show modules' | grep google_transcribe"
echo ""
echo "   Expected output:"
echo "   api,google_transcribe,mod_google_transcribe,..."
echo ""
echo "3. Test Google transcription in fs_cli:"
echo "   docker exec -it fs-test fs_cli"
echo "   freeswitch@internal> uuid_setvar <uuid> GOOGLE_APPLICATION_CREDENTIALS /path/to/creds.json"
echo "   freeswitch@internal> uuid_google_transcribe <uuid> start en-US interim"
echo ""
echo "4. Or use environment variables (recommended):"
echo "   docker run -d --name fs-google \\"
echo "     -p 5060:5060/udp -p 8021:8021/tcp \\"
echo "     -e GOOGLE_APPLICATION_CREDENTIALS=/creds/service-account.json \\"
echo "     -v /local/path/to/service-account.json:/creds/service-account.json:ro \\"
echo "     ${IMAGE_NAME}"
echo ""
echo "5. Using Google Cloud Service Account:"
echo "   a. Create a service account in Google Cloud Console"
echo "   b. Grant 'Cloud Speech-to-Text API User' role"
echo "   c. Download JSON key file"
echo "   d. Mount JSON file into container"
echo "   e. Set GOOGLE_APPLICATION_CREDENTIALS environment variable"
echo ""
echo "6. Push to Docker Hub (optional):"
echo "   docker push ${IMAGE_NAME}"
echo ""
echo "For full documentation, see:"
echo "  - modules/mod_google_transcribe/README.md"
echo "  - dockerfiles/README.md"
echo "  - https://cloud.google.com/speech-to-text/docs"
echo ""
echo "========================================="
echo "Build script completed! 🎉"
echo "========================================="
