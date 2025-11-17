#!/bin/bash
set -e

# ============================================================================
# FreeSWITCH Base Image Build Script
# ============================================================================

IMAGE_NAME=${1:-freeswitch-base:1.10.11}

echo "============================================="
echo "FreeSWITCH Base Image Builder"
echo "============================================="
echo ""
echo "Image name: $IMAGE_NAME"
echo ""
echo "This will build a FULL FreeSWITCH 1.10.11 image with:"
echo "  ✓ All standard modules compiled from source"
echo "  ✓ SIP and WebRTC support"
echo "  ✓ Event socket (fs_cli enabled)"
echo "  ✓ Extensions 1000 and 1001 configured"
echo "  ✓ Basic Linux utilities (ps, netstat, ping)"
echo "  ✓ Supervisor for process management"
echo ""

# Detect platform
PLATFORM="linux/amd64"
if [[ "$(uname -m)" == "arm64" ]]; then
    echo "⚠️  Detected ARM64 architecture (Apple Silicon)"
    echo "Building for linux/amd64 with emulation (slower)"
    PLATFORM="linux/amd64"
fi
echo "Platform: $PLATFORM"
echo ""

# Detect CPU cores
if [[ "$OSTYPE" == "darwin"* ]]; then
    BUILD_CPUS=$(sysctl -n hw.ncpu)
else
    BUILD_CPUS=$(nproc)
fi
echo "Using $BUILD_CPUS CPU cores for compilation"
echo ""

echo "Estimated build time:"
echo "  - Intel/AMD64: 30-45 minutes (full build with all modules)"
echo "  - Apple Silicon: 60-90 minutes (with emulation)"
echo ""

read -p "Press Enter to continue or Ctrl+C to cancel..."
echo ""

echo "============================================="
echo "Starting Docker build..."
echo "============================================="
echo ""

# Record start time
START_TIME=$(date +%s)

# Read dependency versions from .env
SPANDSP_VERSION=$(grep spandspVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SOFIA_VERSION=$(grep sofiaVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')

docker build \
    --platform "$PLATFORM" \
    --build-arg BUILD_CPUS="$BUILD_CPUS" \
    --build-arg SPANDSP_VERSION="$SPANDSP_VERSION" \
    --build-arg SOFIA_VERSION="$SOFIA_VERSION" \
    -f dockerfiles/Dockerfile.freeswitch-base \
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
echo "Quick Start:"
echo "============================================="
echo ""
echo "1. Start FreeSWITCH with supervisor (default):"
echo "   docker run --rm -it --name fs $IMAGE_NAME"
echo ""
echo "2. Connect to FreeSWITCH with fs_cli:"
echo "   Terminal 1: docker run --rm -it --name fs $IMAGE_NAME"
echo "   Terminal 2: docker exec -it fs fs_cli"
echo ""
echo "3. Test extensions 1000 and 1001:"
echo "   - Register SIP clients to extensions 1000 and 1001"
echo "   - Password: 1234 for both"
echo "   - Call between them to test"
echo ""
echo "4. Get a shell in the container:"
echo "   docker exec -it fs bash"
echo ""
echo "5. Run FreeSWITCH directly (no supervisor):"
echo "   docker run --rm -it $IMAGE_NAME freeswitch -nc -nf"
echo ""
echo "============================================="
echo "Exposed Ports:"
echo "============================================="
echo ""
echo "  5060-5061 : SIP (TCP/UDP)"
echo "  5080-5081 : WebSocket SIP (TCP/UDP)"
echo "  8021      : Event Socket (fs_cli)"
echo "  7443      : WebRTC (TCP)"
echo "  16384-32768 : RTP Media (UDP)"
echo ""
echo "To expose ports, use:"
echo "  docker run --rm -it --network host $IMAGE_NAME"
echo ""
echo "Or map specific ports:"
echo "  docker run --rm -it -p 5060:5060/udp -p 8021:8021 $IMAGE_NAME"
echo ""
echo "============================================="
