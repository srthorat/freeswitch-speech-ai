#!/bin/bash
set -e

# Docker build script that reads .env and passes build arguments
# Usage: ./docker-build.sh [image-name]

IMAGE_NAME=${1:-freeswitch-transcribe:latest}

# Check if .env file exists
if [ ! -f ".env" ]; then
    echo "ERROR: .env file not found in current directory"
    exit 1
fi

# Detect platform
PLATFORM="linux/amd64"
if [[ "$(uname -m)" == "arm64" ]] || [[ "$(uname -m)" == "aarch64" ]]; then
    echo "Detected ARM64 architecture (Apple Silicon or ARM Linux)"
    echo "Note: Building for linux/amd64 with emulation (this will be slower)"
    echo "For faster builds on Apple Silicon, consider using Rosetta or a native ARM64 build"
    echo ""
fi

echo "Reading versions from .env..."

# Read versions from .env
CMAKE_VERSION=$(grep cmakeVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
GRPC_VERSION=$(grep grpcVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
LIBWEBSOCKETS_VERSION=$(grep libwebsocketsVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SPEECH_SDK_VERSION=$(grep speechSdkVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SPANDSP_VERSION=$(grep spandspVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SOFIA_VERSION=$(grep sofiaVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
AWS_SDK_CPP_VERSION=$(grep awsSdkCppVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
FREESWITCH_VERSION=$(grep freeswitchVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')

echo ""
echo "Build configuration:"
echo "  Image name: $IMAGE_NAME"
echo "  CMake: $CMAKE_VERSION"
echo "  gRPC: $GRPC_VERSION"
echo "  libwebsockets: $LIBWEBSOCKETS_VERSION"
echo "  Speech SDK: $SPEECH_SDK_VERSION"
echo "  spandsp: $SPANDSP_VERSION"
echo "  sofia-sip: $SOFIA_VERSION"
echo "  AWS SDK C++: $AWS_SDK_CPP_VERSION"
echo "  FreeSWITCH: $FREESWITCH_VERSION"
echo ""
echo "Building Docker image..."
echo ""

# Build Docker image with all build arguments
docker build \
    --platform "$PLATFORM" \
    --build-arg CMAKE_VERSION="$CMAKE_VERSION" \
    --build-arg GRPC_VERSION="$GRPC_VERSION" \
    --build-arg LIBWEBSOCKETS_VERSION="$LIBWEBSOCKETS_VERSION" \
    --build-arg SPEECH_SDK_VERSION="$SPEECH_SDK_VERSION" \
    --build-arg SPANDSP_VERSION="$SPANDSP_VERSION" \
    --build-arg SOFIA_VERSION="$SOFIA_VERSION" \
    --build-arg AWS_SDK_CPP_VERSION="$AWS_SDK_CPP_VERSION" \
    --build-arg FREESWITCH_VERSION="$FREESWITCH_VERSION" \
    -t "$IMAGE_NAME" \
    .

echo ""
echo "========================================="
echo "Docker image built successfully!"
echo "========================================="
echo ""
echo "Image: $IMAGE_NAME"
echo ""
echo "To run the container:"
echo "  docker run -it --rm -p 5060:5060/tcp -p 5060:5060/udp -p 8021:8021 $IMAGE_NAME"
