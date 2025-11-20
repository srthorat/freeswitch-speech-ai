#!/bin/bash
# ============================================================================
# Build Script for FreeSWITCH with mod_aws_transcribe
# ============================================================================
#
# This script builds a Docker image with FreeSWITCH and mod_aws_transcribe
# on top of the freeswitch-base image.
#
# Base image includes:
#   - FreeSWITCH 1.10.11 fully built and configured
#   - All required SIP configurations
#   - Event Socket configuration
#
# This build adds:
#   - AWS SDK C++ (core and transcribestreaming)
#   - mod_aws_transcribe
#
# Usage:
#   ./dockerfiles/docker-build-mod-aws-transcribe.sh [IMAGE_NAME] [AWS_SDK_VERSION]
#
# Examples:
#   ./dockerfiles/docker-build-mod-aws-transcribe.sh
#   ./dockerfiles/docker-build-mod-aws-transcribe.sh srt2011/freeswitch-mod-aws-transcribe:latest
#   ./dockerfiles/docker-build-mod-aws-transcribe.sh my-registry/freeswitch-aws:v1 1.11.400
#
# ============================================================================

set -e

# Configuration
IMAGE_NAME=${1:-freeswitch-mod-aws-transcribe:latest}
# AWS SDK version: 1.11.345 (stable, tested) | Latest: 1.11.694
# To upgrade: ./docker-build-mod-aws-transcribe.sh IMAGE_NAME 1.11.694
AWS_SDK_CPP_VERSION=${2:-1.11.345}
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
echo "FreeSWITCH mod_aws_transcribe Docker Build"
echo "============================================="
echo ""
echo "Configuration:"
echo "  📦 Base Image:          ${BASE_IMAGE}"
echo "  🏷️  Target Image:        ${IMAGE_NAME}"
echo "  🔧 AWS SDK C++ Version: ${AWS_SDK_CPP_VERSION}"
echo "  🖥️  Platform:            ${PLATFORM}"
echo "  ⚙️  Build CPUs:          ${BUILD_CPUS}"
echo "  📂 Build Context:       $(pwd)"
echo ""
echo "Build includes:"
echo "  🆕 AWS SDK C++ (core + transcribestreaming)"
echo "  🔧 cJSON header conflict fix (prevents duplicate symbols)"
echo "  🆕 mod_aws_transcribe"
echo ""
echo "⏱️  Estimated build time:"
echo "  - Intel/AMD64: 25-35 minutes"
echo "  - Apple Silicon: 40-50 minutes (with emulation)"
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
echo "Step 2: Building mod_aws_transcribe image..."
echo "========================================="
docker build \
    --platform "$PLATFORM" \
    --build-arg BASE_IMAGE="$BASE_IMAGE" \
    --build-arg AWS_SDK_CPP_VERSION="$AWS_SDK_CPP_VERSION" \
    --build-arg BUILD_CPUS="$BUILD_CPUS" \
    -f dockerfiles/Dockerfile.mod_aws_transcribe \
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
    ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so

echo ""
echo "Testing AWS SDK libraries..."
docker run --rm "$IMAGE_NAME" \
    ls -lh /usr/local/lib/libaws-cpp-sdk-transcribestreaming.so

echo ""
echo "========================================="
echo "Next steps:"
echo "========================================="
echo ""
echo "1. Test the image locally:"
echo "   docker run -d --name fs-test \\"
echo "     -p 5060:5060/udp \\"
echo "     -p 8021:8021/tcp \\"
echo "     -e AWS_ACCESS_KEY_ID=your-aws-key \\"
echo "     -e AWS_SECRET_ACCESS_KEY=your-aws-secret \\"
echo "     -e AWS_REGION=us-east-1 \\"
echo "     ${IMAGE_NAME}"
echo ""
echo "2. Verify module is loaded:"
echo "   docker exec fs-test fs_cli -x 'show modules' | grep aws_transcribe"
echo ""
echo "   Expected output:"
echo "   api,aws_transcribe,mod_aws_transcribe,..."
echo ""
echo "3. Test AWS transcription in fs_cli:"
echo "   docker exec -it fs-test fs_cli"
echo "   freeswitch@internal> uuid_setvar <uuid> AWS_ACCESS_KEY_ID your-key"
echo "   freeswitch@internal> uuid_setvar <uuid> AWS_SECRET_ACCESS_KEY your-secret"
echo "   freeswitch@internal> uuid_setvar <uuid> AWS_REGION us-east-1"
echo "   freeswitch@internal> aws_transcribe <uuid> start en-US interim"
echo ""
echo "4. Or use environment variables (recommended):"
echo "   docker run -d --name fs-aws \\"
echo "     -p 5060:5060/udp -p 8021:8021/tcp \\"
echo "     -e AWS_ACCESS_KEY_ID=your-key \\"
echo "     -e AWS_SECRET_ACCESS_KEY=your-secret \\"
echo "     -e AWS_REGION=us-east-1 \\"
echo "     ${IMAGE_NAME}"
echo ""
echo "5. Push to Docker Hub (optional):"
echo "   docker push ${IMAGE_NAME}"
echo ""
echo "For full documentation, see:"
echo "  - modules/mod_aws_transcribe/README.md"
echo "  - dockerfiles/README.md"
echo "  - AWS_INTEGRATION_TASKS.md"
echo ""
echo "========================================="
echo "Build script completed! 🎉"
echo "========================================="
