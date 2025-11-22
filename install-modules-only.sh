#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Modules Only Installation Script
# ============================================================================
# Installs only the 3 modules on existing FreeSWITCH installation:
#   - mod_audio_fork
#   - mod_aws_transcribe
#   - mod_deepgram_transcribe
#
# Usage:
#   sudo ./install-modules-only.sh [OPTIONS]
#
# Options:
#   --freeswitch-prefix PATH    FreeSWITCH installation path (default: /usr/local/freeswitch)
#   --build-cpus N              Number of CPUs for build (default: 4)
#
# Note: This script does NOT copy dialplan files.
#       Use install-all.sh for full installation with dialplan.
# ============================================================================

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Default values
FS_PREFIX="/usr/local/freeswitch"
BUILD_CPUS=4
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --freeswitch-prefix)
            FS_PREFIX="$2"
            shift 2
            ;;
        --build-cpus)
            BUILD_CPUS="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--freeswitch-prefix PATH] [--build-cpus N]"
            exit 1
            ;;
    esac
done

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    exit 1
fi

echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}Installing Modules Only${NC}"
echo -e "${GREEN}=============================================${NC}"
echo "FreeSWITCH Prefix: $FS_PREFIX"
echo "Build CPUs: $BUILD_CPUS"
echo ""

# Check if FreeSWITCH exists
if [ ! -d "$FS_PREFIX" ]; then
    echo -e "${RED}Error: FreeSWITCH not found at $FS_PREFIX${NC}"
    echo "Please install FreeSWITCH first or specify correct path with --freeswitch-prefix"
    exit 1
fi

# ============================================================================
# Install Dependencies
# ============================================================================
echo -e "${GREEN}[1/4] Installing dependencies...${NC}"

# Install system dependencies
if ! apt-get update; then
    echo -e "${RED}✗ Failed to update package lists${NC}"
    echo "Check your internet connection and try again"
    exit 1
fi

if ! apt-get install -y \
    build-essential \
    git \
    cmake \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev; then
    echo -e "${RED}✗ Failed to install system dependencies${NC}"
    exit 1
fi

# Build libwebsockets if not present
if ! ldconfig -p | grep -q libwebsockets; then
    echo "Building libwebsockets 4.3.3..."
    cd /usr/local/src || exit 1

    if [ ! -d "libwebsockets" ]; then
        echo "Cloning libwebsockets repository..."
        if ! git clone --depth 1 -b v4.3.3 https://github.com/warmcat/libwebsockets.git; then
            echo -e "${RED}✗ Failed to clone libwebsockets repository${NC}"
            echo "Check your internet connection and try again"
            exit 1
        fi
    fi

    cd libwebsockets || exit 1
    mkdir -p build && cd build || exit 1

    echo "Configuring libwebsockets..."
    if ! cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo; then
        echo -e "${RED}✗ Failed to configure libwebsockets${NC}"
        exit 1
    fi

    echo "Compiling libwebsockets (this may take a few minutes)..."
    if ! make -j ${BUILD_CPUS}; then
        echo -e "${RED}✗ Failed to compile libwebsockets${NC}"
        exit 1
    fi

    echo "Installing libwebsockets..."
    if ! make install; then
        echo -e "${RED}✗ Failed to install libwebsockets${NC}"
        exit 1
    fi

    ldconfig
    echo -e "${GREEN}✓ libwebsockets built and installed${NC}"
else
    echo -e "${YELLOW}ℹ${NC} libwebsockets already installed"
fi

# Build AWS SDK if not present
if ! ldconfig -p | grep -q aws-cpp-sdk-transcribestreaming; then
    echo "Building AWS SDK C++ 1.11.345..."
    echo "This will take 20-30 minutes..."
    cd /usr/local/src || exit 1

    if [ ! -d "aws-sdk-cpp" ]; then
        echo "Cloning AWS SDK repository..."
        if ! git clone --depth 1 -b 1.11.345 https://github.com/aws/aws-sdk-cpp.git; then
            echo -e "${RED}✗ Failed to clone AWS SDK repository${NC}"
            echo "Check your internet connection and try again"
            exit 1
        fi
        cd aws-sdk-cpp || exit 1

        echo "Initializing submodules..."
        if ! git submodule update --init --recursive; then
            echo -e "${RED}✗ Failed to initialize AWS SDK submodules${NC}"
            exit 1
        fi
    else
        cd aws-sdk-cpp || exit 1
    fi

    mkdir -p build && cd build || exit 1

    echo "Configuring AWS SDK..."
    if ! cmake .. \
        -DBUILD_ONLY="transcribestreaming" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_SHARED_LIBS=ON \
        -DENABLE_TESTING=OFF \
        -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull"; then
        echo -e "${RED}✗ Failed to configure AWS SDK${NC}"
        exit 1
    fi

    echo "Compiling AWS SDK (this will take 20-30 minutes)..."
    if ! make -j ${BUILD_CPUS}; then
        echo -e "${RED}✗ Failed to compile AWS SDK${NC}"
        echo "Try reducing build parallelism: --build-cpus 2"
        exit 1
    fi

    echo "Installing AWS SDK..."
    if ! make install; then
        echo -e "${RED}✗ Failed to install AWS SDK${NC}"
        exit 1
    fi

    ldconfig
    echo -e "${GREEN}✓ AWS SDK built and installed${NC}"
else
    echo -e "${YELLOW}ℹ${NC} AWS SDK already installed"
fi

echo -e "${GREEN}✓ Dependencies installed${NC}"

# ============================================================================
# Build Modules
# ============================================================================
echo -e "${GREEN}[2/4] Building modules...${NC}"

# Check if FreeSWITCH headers exist
if [ ! -d "${FS_PREFIX}/include/freeswitch" ]; then
    echo -e "${RED}✗ FreeSWITCH headers not found at ${FS_PREFIX}/include/freeswitch${NC}"
    echo "FreeSWITCH may not be properly installed"
    exit 1
fi

# mod_audio_fork
echo "Building mod_audio_fork..."
cd ${SCRIPT_DIR}/modules/mod_audio_fork || exit 1

if ! gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_audio_fork.c; then
    echo -e "${RED}✗ Failed to compile mod_audio_fork.c${NC}"
    exit 1
fi

if ! g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include lws_glue.cpp audio_pipe.cpp parser.cpp; then
    echo -e "${RED}✗ Failed to compile mod_audio_fork C++ sources${NC}"
    exit 1
fi

mkdir -p ${FS_PREFIX}/lib/freeswitch/mod

if ! g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so *.o -lwebsockets -lpthread -lssl -lcrypto; then
    echo -e "${RED}✗ Failed to link mod_audio_fork${NC}"
    exit 1
fi
echo -e "${GREEN}✓ mod_audio_fork${NC}"

# mod_aws_transcribe
echo "Building mod_aws_transcribe..."
cd ${SCRIPT_DIR}/modules/mod_aws_transcribe || exit 1

if ! gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch mod_aws_transcribe.c; then
    echo -e "${RED}✗ Failed to compile mod_aws_transcribe.c${NC}"
    exit 1
fi

if ! g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include aws_transcribe_glue.cpp; then
    echo -e "${RED}✗ Failed to compile mod_aws_transcribe C++ sources${NC}"
    exit 1
fi

if ! g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so \
    mod_aws_transcribe.o aws_transcribe_glue.o \
    -L/usr/local/lib -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core \
    -laws-c-event-stream -laws-checksums -laws-c-common \
    -lpthread -lcurl -lssl -lcrypto -lz; then
    echo -e "${RED}✗ Failed to link mod_aws_transcribe${NC}"
    exit 1
fi
echo -e "${GREEN}✓ mod_aws_transcribe${NC}"

# mod_deepgram_transcribe
echo "Building mod_deepgram_transcribe..."
cd ${SCRIPT_DIR}/modules/mod_deepgram_transcribe || exit 1

if ! gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_deepgram_transcribe.c; then
    echo -e "${RED}✗ Failed to compile mod_deepgram_transcribe.c${NC}"
    exit 1
fi

if ! g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp; then
    echo -e "${RED}✗ Failed to compile mod_deepgram_transcribe C++ sources${NC}"
    exit 1
fi

if ! g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    mod_deepgram_transcribe.o dg_transcribe_glue.o audio_pipe.o parser.o \
    -lwebsockets -lpthread -lssl -lcrypto; then
    echo -e "${RED}✗ Failed to link mod_deepgram_transcribe${NC}"
    exit 1
fi
echo -e "${GREEN}✓ mod_deepgram_transcribe${NC}"

# ============================================================================
# Configure FreeSWITCH
# ============================================================================
echo -e "${GREEN}[3/4] Configuring FreeSWITCH...${NC}"

MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
if [ -f "$MODULES_CONF" ]; then
    if ! grep -q "mod_audio_fork" "$MODULES_CONF"; then
        sed -i '/<\/modules>/i \    <!-- Speech Transcription Modules -->' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_audio_fork"/>' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_aws_transcribe"/>' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_deepgram_transcribe"/>' "$MODULES_CONF"
        echo -e "${GREEN}✓ Modules added to configuration${NC}"
    else
        echo -e "${YELLOW}ℹ Modules already configured${NC}"
    fi
fi

# Note: Dialplan is NOT copied by this script
# If you need the example dialplan, manually copy from:
#   examples/freeswitch-config/dialplan/default.xml -> ${FS_PREFIX}/conf/dialplan/
#   examples/freeswitch-config/directory/100*.xml -> ${FS_PREFIX}/conf/directory/default/

# ============================================================================
# Validate
# ============================================================================
echo -e "${GREEN}[4/4] Validating modules...${NC}"

for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
    if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
        echo -e "${GREEN}✓${NC} ${module}.so exists"
        if ldd "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" | grep -q "not found"; then
            echo -e "${RED}✗${NC} ${module} has missing dependencies"
            ldd "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" | grep "not found"
        else
            echo -e "${GREEN}✓${NC} ${module} dependencies OK"
        fi
    else
        echo -e "${RED}✗${NC} ${module}.so NOT FOUND"
    fi
done

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Modules Installation Complete!${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Next steps:"
echo "  1. Reload FreeSWITCH: ${FS_PREFIX}/bin/fs_cli -x 'reload mod_sofia'"
echo "  2. Or restart: systemctl restart freeswitch"
echo "  3. Verify: ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram'"
echo ""
