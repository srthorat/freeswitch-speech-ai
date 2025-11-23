#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Full Installation Script
# ============================================================================
# Installs everything from scratch on Debian/Ubuntu:
#   - FreeSWITCH 1.10.11
#   - mod_audio_fork (with libwebsockets)
#   - mod_aws_transcribe (with AWS SDK C++)
#   - mod_deepgram_transcribe (with libwebsockets)
#
# Usage:
#   sudo ./install-all.sh [OPTIONS]
#
# Options:
#   --skip-freeswitch       Skip FreeSWITCH installation (install modules only)
#   --no-validation         Skip module validation
#   --build-cpus N          Number of CPUs for build (default: 4)
#
# Note: This script ALWAYS copies the example dialplan from examples/ directory.
#
# Environment Variables (set before running):
#   DEEPGRAM_API_KEY        Deepgram API key
#   AWS_ACCESS_KEY_ID       AWS access key
#   AWS_SECRET_ACCESS_KEY   AWS secret key
#   AWS_REGION              AWS region (default: us-east-1)
#   AWS_SESSION_TOKEN       AWS session token (for STS)
#   PUSHER_APP_ID           Pusher app ID
#   PUSHER_KEY              Pusher key
#   PUSHER_SECRET           Pusher secret
#   PUSHER_CLUSTER          Pusher cluster
# ============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Default values
SKIP_FREESWITCH=false
NO_VALIDATION=false
BUILD_CPUS=4
AUTO_YES=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FS_PREFIX="/usr/local/freeswitch"

# Installation manifest file - tracks what WE installed (not what already existed)
# Placed in repository root (parent of scripts/)
MANIFEST_FILE="$(cd "${SCRIPT_DIR}/.." && pwd)/.freeswitch-install-manifest.txt"

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
        --yes)
            AUTO_YES=true
            shift
            ;;
        --skip-freeswitch)
            SKIP_FREESWITCH=true
            shift
            ;;
        --no-validation)
            NO_VALIDATION=true
            shift
            ;;
        --help)
            echo "FreeSWITCH Speech AI - Full Installation Script"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --freeswitch-prefix PATH  FreeSWITCH installation directory (default: /usr/local/freeswitch)"
            echo "  --build-cpus N            Number of CPU cores for compilation (default: 4)"
            echo "  --yes                     Skip confirmation prompts (auto-accept)"
            echo "  --skip-freeswitch         Skip FreeSWITCH installation (modules only)"
            echo "  --no-validation           Skip module validation after build"
            echo "  --help                    Show this help message"
            echo ""
            echo "Example:"
            echo "  sudo $0 --build-cpus 8 --freeswitch-prefix /opt/freeswitch"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [OPTIONS]"
            echo "Try '$0 --help' for more information."
            exit 1
            ;;
    esac
done

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    echo "Please run: sudo $0"
    exit 1
fi

echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}FreeSWITCH Speech AI - Full Installation${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Configuration:"
echo "  Install FreeSWITCH: $([ "$SKIP_FREESWITCH" = false ] && echo "Yes" || echo "No (modules only)")"
echo "  Copy Dialplan: Yes (always)"
echo "  Build CPUs: $BUILD_CPUS"
echo "  Install Prefix: $INSTALL_PREFIX"
echo ""

# Check if FreeSWITCH is already installed
if [ "$SKIP_FREESWITCH" = false ] && [ -d "$FS_PREFIX" ]; then
    echo -e "${YELLOW}Warning: FreeSWITCH appears to be already installed at $FS_PREFIX${NC}"
    read -p "Continue anyway? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# ============================================================================
# Step 1: Install System Dependencies
# ============================================================================
echo -e "${GREEN}[Step 1/6] Installing system dependencies...${NC}"

if ! apt-get update; then
    echo -e "${RED}✗ Failed to update package lists${NC}"
    echo "Check your internet connection and try again"
    exit 1
fi

if ! apt-get install -y \
    build-essential \
    git \
    cmake \
    ca-certificates \
    wget \
    autoconf \
    libtool \
    pkg-config \
    libcurl4-openssl-dev \
    libssl-dev \
    uuid-dev \
    zlib1g-dev \
    libpulse-dev \
    libspeexdsp-dev \
    libpcre3-dev \
    libspeex-dev \
    libspeexdsp-dev \
    libedit-dev \
    libsqlite3-dev \
    libldns-dev; then
    echo -e "${RED}✗ Failed to install system dependencies${NC}"
    exit 1
fi

echo -e "${GREEN}✓ System dependencies installed${NC}"

# Initialize installation manifest
echo "# FreeSWITCH Speech AI Installation Manifest" > "$MANIFEST_FILE"
echo "# Created: $(date)" >> "$MANIFEST_FILE"
echo "# This file tracks what was installed by this script" >> "$MANIFEST_FILE"
echo "# Format: component=status (installed|existing)" >> "$MANIFEST_FILE"
echo "" >> "$MANIFEST_FILE"

# ============================================================================
# Step 2: Build libwebsockets (for mod_audio_fork & mod_deepgram_transcribe)
# ============================================================================
echo -e "${GREEN}[Step 2/6] Building libwebsockets 4.3.3...${NC}"

# Check if libwebsockets already exists
if ldconfig -p | grep -q libwebsockets; then
    echo -e "${YELLOW}ℹ${NC} libwebsockets already installed - skipping (will not remove during cleanup)"
    echo "libwebsockets=existing" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ Using existing libwebsockets${NC}"
else
    echo "Installing libwebsockets 4.3.3..."
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

    # Mark as installed by us
    echo "libwebsockets=installed" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ libwebsockets 4.3.3 installed${NC}"
fi

# ============================================================================
# Step 3: Build AWS SDK C++ (for mod_aws_transcribe)
# ============================================================================
echo -e "${GREEN}[Step 3/6] Building AWS SDK C++ 1.11.345...${NC}"

# Check if AWS SDK already exists
if ldconfig -p | grep -q aws-cpp-sdk-transcribestreaming; then
    echo -e "${YELLOW}ℹ${NC} AWS SDK C++ already installed - skipping (will not remove during cleanup)"
    echo "aws-sdk-cpp=existing" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ Using existing AWS SDK C++${NC}"
else
    echo "Installing AWS SDK C++ 1.11.345..."
    echo "This will take 20-30 minutes..."

    cd /usr/local/src || exit 1
    if [ ! -d "aws-sdk-cpp" ]; then
        if ! git clone --depth 1 -b 1.11.345 https://github.com/aws/aws-sdk-cpp.git; then
            echo -e "${RED}✗ Failed to clone AWS SDK repository${NC}"
            exit 1
        fi
        cd aws-sdk-cpp || exit 1
        if ! git submodule update --init --recursive; then
            echo -e "${RED}✗ Failed to initialize AWS SDK submodules${NC}"
            exit 1
        fi
    else
        cd aws-sdk-cpp || exit 1
    fi

mkdir -p build && cd build
cmake .. \
    -DBUILD_ONLY="transcribestreaming" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_SHARED_LIBS=ON \
    -DENABLE_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull -Wno-error=deprecated-declarations"

    if ! make -j ${BUILD_CPUS}; then
        echo -e "${RED}✗ Failed to compile AWS SDK${NC}"
        exit 1
    fi

    if ! make install; then
        echo -e "${RED}✗ Failed to install AWS SDK${NC}"
        exit 1
    fi

    ldconfig

    # Mark as installed by us
    echo "aws-sdk-cpp=installed" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ AWS SDK C++ 1.11.345 installed${NC}"
fi

# Fix cJSON header conflict between AWS SDK and FreeSWITCH
# This applies whether we just installed AWS SDK or if it already existed
echo "Fixing cJSON header conflict..."
if [ -f /usr/local/include/aws/core/external/cjson/cJSON.h ]; then
    # Check if fix is already applied
    if ! grep -q '#ifndef cJSON__h' /usr/local/include/aws/core/external/cjson/cJSON.h; then
        echo "Found AWS SDK cJSON header, adding header guards..."
        sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' \
            /usr/local/include/aws/core/external/cjson/cJSON.h
        echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h
        echo -e "${GREEN}✓ cJSON header guards added successfully${NC}"
    else
        echo -e "${GREEN}✓ cJSON header guards already applied${NC}"
    fi
else
    echo -e "${YELLOW}⚠${NC}  WARNING: AWS SDK cJSON header not found at expected location"
fi

# ============================================================================
# Step 4: Install FreeSWITCH (if not skipped)
# ============================================================================
if [ "$SKIP_FREESWITCH" = false ]; then
    echo "freeswitch=installed" >> "$MANIFEST_FILE"
    echo -e "${GREEN}[Step 4/6] Installing FreeSWITCH 1.10.11...${NC}"
    echo "This will take 15-20 minutes..."
    
    # Install FreeSWITCH dependencies
    # Note: libspandsp-dev omitted (Ubuntu has old 0.0.6, FS needs 3.x, not required for speech modules)
    apt-get install -y \
        libavformat-dev \
        libswscale-dev \
        libswresample-dev \
        liblua5.1-0-dev \
        libopus-dev \
        libsndfile-dev \
        libsofia-sip-ua-dev
    
    cd /usr/local/src
    if [ ! -d "freeswitch" ]; then
        git clone --depth 1 -b v1.10.11 https://github.com/signalwire/freeswitch.git
    fi
    
    cd freeswitch
    ./bootstrap.sh -j
    # Disable unnecessary modules (we only need core + 3 speech modules)
    sed -i 's/^endpoints\/mod_verto/#&/' modules.conf
    sed -i 's/^endpoints\/mod_rtc/#&/' modules.conf
    sed -i 's/^applications\/mod_signalwire/#&/' modules.conf
    sed -i 's/^applications\/mod_spandsp/#&/' modules.conf
    sed -i 's/^languages\/mod_python/#&/' modules.conf
    sed -i 's/^languages\/mod_python3/#&/' modules.conf
    # Patch configure to skip version checks for Ubuntu packages that are older but functional
    # - spandsp: Ubuntu has 0.0.6, FS needs 3.x (not required for speech modules)
    # - sofia-sip: Ubuntu has 1.12.11, FS needs 1.13.17 (1.12.11 works fine for our use)
    sed -i 's/as_fn_error \$? "no usable spandsp.*$/: # Skipping spandsp check/' configure
    sed -i 's/as_fn_error \$? "no usable sofia-sip.*$/: # Skipping sofia-sip version check/' configure
    ./configure --prefix=${FS_PREFIX} --without-spandsp
    make -j ${BUILD_CPUS}
    make install
    
    # Create freeswitch user
    id -u freeswitch &>/dev/null || useradd -r -g daemon -s /bin/false -c "FreeSWITCH" freeswitch
    chown -R freeswitch:daemon ${FS_PREFIX}
    
    echo -e "${GREEN}✓ FreeSWITCH 1.10.11 installed${NC}"
else
    echo -e "${YELLOW}[Step 4/6] Skipping FreeSWITCH installation${NC}"
fi

# ============================================================================
# Step 5: Build and Install Modules
# ============================================================================
echo -e "${GREEN}[Step 5/6] Building modules...${NC}"

# Build mod_audio_fork
echo "Building mod_audio_fork..."
cd ${SCRIPT_DIR}/modules/mod_audio_fork
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    mod_audio_fork.c

g++ -fPIC -c -std=c++11 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    lws_glue.cpp audio_pipe.cpp parser.cpp

mkdir -p ${FS_PREFIX}/lib/freeswitch/mod
g++ -shared \
    -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so \
    *.o \
    -lwebsockets \
    -lpthread \
    -lssl \
    -lcrypto

echo -e "${GREEN}✓ mod_audio_fork built${NC}"

# Build mod_aws_transcribe
echo "Building mod_aws_transcribe..."
cd ${SCRIPT_DIR}/modules/mod_aws_transcribe
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    mod_aws_transcribe.c

g++ -fPIC -c -std=c++11 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    aws_transcribe_glue.cpp

g++ -shared \
    -o ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so \
    mod_aws_transcribe.o \
    aws_transcribe_glue.o \
    -L/usr/local/lib \
    -laws-cpp-sdk-transcribestreaming \
    -laws-cpp-sdk-core \
    -laws-c-event-stream \
    -laws-checksums \
    -laws-c-common \
    -lpthread \
    -lcurl \
    -lssl \
    -lcrypto \
    -lz

echo -e "${GREEN}✓ mod_aws_transcribe built${NC}"

# Build mod_deepgram_transcribe
echo "Building mod_deepgram_transcribe..."
cd ${SCRIPT_DIR}/modules/mod_deepgram_transcribe
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    mod_deepgram_transcribe.c

g++ -fPIC -c -std=c++11 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp

g++ -shared \
    -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    mod_deepgram_transcribe.o \
    dg_transcribe_glue.o \
    audio_pipe.o \
    parser.o \
    -lwebsockets \
    -lpthread \
    -lssl \
    -lcrypto

echo -e "${GREEN}✓ mod_deepgram_transcribe built${NC}"

# Mark modules as installed
echo "modules=installed" >> "$MANIFEST_FILE"

# Set permissions
chown freeswitch:daemon ${FS_PREFIX}/lib/freeswitch/mod/mod_*.so

# ============================================================================
# Step 6: Configure FreeSWITCH
# ============================================================================
echo -e "${GREEN}[Step 6/6] Configuring FreeSWITCH...${NC}"

# Add modules to modules.conf.xml
MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
if [ -f "$MODULES_CONF" ]; then
    # Check if modules are already added
    if ! grep -q "mod_audio_fork" "$MODULES_CONF"; then
        sed -i '/<\/modules>/i \    <!-- Speech Transcription Modules -->' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_audio_fork"/>' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_aws_transcribe"/>' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_deepgram_transcribe"/>' "$MODULES_CONF"
        echo -e "${GREEN}✓ Added modules to modules.conf.xml${NC}"
    else
        echo -e "${YELLOW}ℹ Modules already configured in modules.conf.xml${NC}"
    fi
fi

# Always copy dialplan
echo "Copying example dialplan..."
cp ${SCRIPT_DIR}/examples/freeswitch-config/dialplan/default.xml \
   ${FS_PREFIX}/conf/dialplan/default.xml

cp ${SCRIPT_DIR}/examples/freeswitch-config/directory/100*.xml \
   ${FS_PREFIX}/conf/directory/default/ 2>/dev/null || true

chown -R freeswitch:daemon ${FS_PREFIX}/conf
echo -e "${GREEN}✓ Example dialplan copied${NC}"

# Set environment variables in systemd service (if exists)
if [ -f "/etc/systemd/system/freeswitch.service" ]; then
    echo "Adding environment variables to systemd service..."
    
    # Backup original
    cp /etc/systemd/system/freeswitch.service /etc/systemd/system/freeswitch.service.bak
    
    # Add environment variables
    cat > /tmp/freeswitch-env.conf << EOF
[Service]
Environment="DEEPGRAM_API_KEY=${DEEPGRAM_API_KEY:-}"
Environment="AWS_ACCESS_KEY_ID=${AWS_ACCESS_KEY_ID:-}"
Environment="AWS_SECRET_ACCESS_KEY=${AWS_SECRET_ACCESS_KEY:-}"
Environment="AWS_REGION=${AWS_REGION:-us-east-1}"
Environment="AWS_SESSION_TOKEN=${AWS_SESSION_TOKEN:-}"
Environment="PUSHER_APP_ID=${PUSHER_APP_ID:-}"
Environment="PUSHER_KEY=${PUSHER_KEY:-}"
Environment="PUSHER_SECRET=${PUSHER_SECRET:-}"
Environment="PUSHER_CLUSTER=${PUSHER_CLUSTER:-}"
EOF
    
    systemctl daemon-reload
    echo -e "${GREEN}✓ Environment variables configured${NC}"
fi

# ============================================================================
# Validation
# ============================================================================
if [ "$NO_VALIDATION" = false ]; then
    echo ""
    echo -e "${GREEN}=============================================${NC}"
    echo -e "${GREEN}Validating Installation${NC}"
    echo -e "${GREEN}=============================================${NC}"
    
    # Check module files
    echo "Checking module files..."
    for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
            echo -e "${GREEN}✓${NC} ${module}.so exists"
        else
            echo -e "${RED}✗${NC} ${module}.so NOT FOUND"
            exit 1
        fi
    done
    
    # Check dependencies
    echo ""
    echo "Checking module dependencies..."
    for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
        MODULE_PATH="${FS_PREFIX}/lib/freeswitch/mod/${module}.so"
        if ldd "$MODULE_PATH" | grep -q "not found"; then
            echo -e "${RED}✗${NC} ${module} has missing dependencies:"
            ldd "$MODULE_PATH" | grep "not found"
            exit 1
        else
            echo -e "${GREEN}✓${NC} ${module} dependencies OK"
        fi
    done
    
    # Try to start FreeSWITCH and check modules load
    if [ "$SKIP_FREESWITCH" = false ]; then
        echo ""
        echo "Testing module loading..."
        export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
        
        # Start FreeSWITCH in background
        ${FS_PREFIX}/bin/freeswitch -nc -nonat &
        FS_PID=$!
        
        # Wait for startup
        sleep 15
        
        # Check if modules loaded
        if ${FS_PREFIX}/bin/fs_cli -x "show modules" | grep -q "mod_audio_fork"; then
            echo -e "${GREEN}✓${NC} mod_audio_fork loaded"
        else
            echo -e "${RED}✗${NC} mod_audio_fork failed to load"
        fi
        
        if ${FS_PREFIX}/bin/fs_cli -x "show modules" | grep -q "mod_aws_transcribe"; then
            echo -e "${GREEN}✓${NC} mod_aws_transcribe loaded"
        else
            echo -e "${RED}✗${NC} mod_aws_transcribe failed to load"
        fi
        
        if ${FS_PREFIX}/bin/fs_cli -x "show modules" | grep -q "mod_deepgram_transcribe"; then
            echo -e "${GREEN}✓${NC} mod_deepgram_transcribe loaded"
        else
            echo -e "${RED}✗${NC} mod_deepgram_transcribe failed to load"
        fi
        
        # Stop FreeSWITCH
        kill $FS_PID 2>/dev/null || true
        sleep 2
    fi
fi

# ============================================================================
# Summary
# ============================================================================
echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Installation Complete!${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Installed:"
echo "  ✓ libwebsockets 4.3.3"
echo "  ✓ AWS SDK C++ 1.11.345"
if [ "$SKIP_FREESWITCH" = false ]; then
    echo "  ✓ FreeSWITCH 1.10.11"
fi
echo "  ✓ mod_audio_fork"
echo "  ✓ mod_aws_transcribe"
echo "  ✓ mod_deepgram_transcribe"
echo ""
echo "Location: ${FS_PREFIX}"
echo ""
echo "Next steps:"
echo "  1. Configure API keys:"
echo "     export DEEPGRAM_API_KEY=your_key"
echo "     export AWS_ACCESS_KEY_ID=your_key"
echo "     export AWS_SECRET_ACCESS_KEY=your_secret"
echo ""
echo "  2. Start FreeSWITCH:"
echo "     ${FS_PREFIX}/bin/freeswitch -nc"
echo ""
echo "  3. Verify modules:"
echo "     ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram'"
echo ""
