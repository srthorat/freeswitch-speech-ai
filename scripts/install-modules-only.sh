#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Modules Only Installation Script
# ============================================================================
# Installs only the 4 modules on existing FreeSWITCH installation:
#   - mod_audio_fork
#   - mod_aws_transcribe
#   - mod_deepgram_transcribe
#   - mod_google_transcribe
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
AUTO_YES=false
VERBOSE=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Installation manifest file
# Placed in repository root (parent of scripts/)
MANIFEST_FILE="$(cd "${SCRIPT_DIR}/.." && pwd)/.freeswitch-install-manifest.txt"

# Detailed logging directory
LOG_DIR="/tmp/freeswitch-install-logs"
mkdir -p "$LOG_DIR"

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
        --verbose)
            VERBOSE=true
            shift
            ;;
        --help)
            echo "FreeSWITCH Speech AI - Modules-Only Installation Script"
            echo ""
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --freeswitch-prefix PATH  FreeSWITCH installation directory (default: /usr/local/freeswitch)"
            echo "  --build-cpus N            Number of CPU cores for compilation (default: 4)"
            echo "  --yes                     Skip confirmation prompts (auto-accept)"
            echo "  --verbose                 Show detailed compilation output in real-time"
            echo "  --help                    Show this help message"
            echo ""
            echo "Example:"
            echo "  sudo $0 --build-cpus 8 --freeswitch-prefix /usr/local/freeswitch"
            echo ""
            echo "Note: This script assumes FreeSWITCH is already installed."
            echo "      Use install-all.sh for a complete installation."
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

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    exit 1
fi

# ============================================================================
# Helper Functions
# ============================================================================

# Conditional logging helper: shows real-time output if --verbose, otherwise logs to file
log_command() {
    local description="$1"
    local log_file="$2"
    shift 2

    if [ "$VERBOSE" = true ]; then
        echo -e "${CYAN}    Running: $@${NC}"
        "$@" 2>&1 | tee "$log_file"
        return ${PIPESTATUS[0]}
    else
        "$@" > "$log_file" 2>&1
        return $?
    fi
}

# Enhanced error handler with automatic log display
check_success() {
    local exit_code=$?
    local error_msg="$1"
    local log_file="$2"

    if [ $exit_code -ne 0 ]; then
        echo -e "${RED}✗ $error_msg${NC}"
        if [ -n "$log_file" ] && [ -f "$log_file" ]; then
            echo -e "${YELLOW}  ↪ Error log: $log_file${NC}"
            echo ""
            echo -e "\033[1mLast 20 lines of error log:\033[0m"
            tail -20 "$log_file" | sed 's/^/    /'
            echo ""
        fi
        exit $exit_code
    fi
}

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

# Initialize or update manifest
if [ ! -f "$MANIFEST_FILE" ]; then
    echo "# FreeSWITCH Speech AI Installation Manifest" > "$MANIFEST_FILE"
    echo "# Created: $(date)" >> "$MANIFEST_FILE"
    echo "# This file tracks what was installed by installation scripts" >> "$MANIFEST_FILE"
    echo "# Format: component=status (installed|existing)" >> "$MANIFEST_FILE"
    echo "" >> "$MANIFEST_FILE"
    echo "freeswitch=existing" >> "$MANIFEST_FILE"
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

    # Remove old entry if exists
    sed -i '/^libwebsockets=/d' "$MANIFEST_FILE" 2>/dev/null || true

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
    log_command "libwebsockets cmake" "${LOG_DIR}/libwebsockets_cmake.log" \
        cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
    check_success "Failed to configure libwebsockets" "${LOG_DIR}/libwebsockets_cmake.log"

    echo "Compiling libwebsockets (this may take a few minutes)..."
    log_command "libwebsockets make" "${LOG_DIR}/libwebsockets_make.log" \
        make -j ${BUILD_CPUS}
    check_success "Failed to compile libwebsockets" "${LOG_DIR}/libwebsockets_make.log"

    echo "Installing libwebsockets..."
    log_command "libwebsockets install" "${LOG_DIR}/libwebsockets_install.log" \
        make install
    check_success "Failed to install libwebsockets" "${LOG_DIR}/libwebsockets_install.log"

    ldconfig

    # Mark as installed by us
    echo "libwebsockets=installed" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ libwebsockets built and installed${NC}"
else
    echo -e "${YELLOW}ℹ${NC} libwebsockets already installed"
    # Mark as existing (not installed by us)
    if ! grep -q "^libwebsockets=" "$MANIFEST_FILE"; then
        echo "libwebsockets=existing" >> "$MANIFEST_FILE"
    fi
fi

# Build AWS SDK if not present
if ! ldconfig -p | grep -q aws-cpp-sdk-transcribestreaming; then
    echo "Building AWS SDK C++ 1.11.345..."
    echo "This will take 20-30 minutes..."

    # Remove old entry if exists
    sed -i '/^aws-sdk-cpp=/d' "$MANIFEST_FILE" 2>/dev/null || true

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
    log_command "AWS SDK cmake" "${LOG_DIR}/aws_sdk_cmake.log" \
        cmake .. \
        -DBUILD_ONLY="transcribestreaming" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_SHARED_LIBS=ON \
        -DENABLE_TESTING=OFF \
        -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull"
    check_success "Failed to configure AWS SDK" "${LOG_DIR}/aws_sdk_cmake.log"

    echo "Compiling AWS SDK (this will take 20-30 minutes)..."
    log_command "AWS SDK make" "${LOG_DIR}/aws_sdk_make.log" \
        make -j ${BUILD_CPUS}
    check_success "Failed to compile AWS SDK" "${LOG_DIR}/aws_sdk_make.log"

    echo "Installing AWS SDK..."
    log_command "AWS SDK install" "${LOG_DIR}/aws_sdk_install.log" \
        make install
    check_success "Failed to install AWS SDK" "${LOG_DIR}/aws_sdk_install.log"

    ldconfig

    # Mark as installed by us
    echo "aws-sdk-cpp=installed" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ AWS SDK built and installed${NC}"
else
    echo -e "${YELLOW}ℹ${NC} AWS SDK already installed"
    # Mark as existing (not installed by us)
    if ! grep -q "^aws-sdk-cpp=" "$MANIFEST_FILE"; then
        echo "aws-sdk-cpp=existing" >> "$MANIFEST_FILE"
    fi
fi

# Fix cJSON header conflict between AWS SDK and FreeSWITCH
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

# Install gRPC from system packages (FAST VERSION - no compilation)
if ! command -v protoc &> /dev/null || ! command -v grpc_cpp_plugin &> /dev/null; then
    echo "Installing gRPC from system packages..."
    echo "This takes seconds instead of 20-40 minutes compiling from source."

    # Remove old entry if exists
    sed -i '/^grpc=/d' "$MANIFEST_FILE" 2>/dev/null || true

    # Install gRPC development packages from system repos
    if ! apt-get install -y \
        libgrpc++-dev \
        libgrpc-dev \
        protobuf-compiler \
        protobuf-compiler-grpc \
        libprotobuf-dev; then
        echo -e "${RED}✗ Failed to install gRPC packages${NC}"
        exit 1
    fi

    # Validate installation
    if ! command -v protoc &> /dev/null; then
        echo -e "${RED}✗ protoc not found after installation${NC}"
        exit 1
    fi

    if ! command -v grpc_cpp_plugin &> /dev/null; then
        echo -e "${RED}✗ grpc_cpp_plugin not found after installation${NC}"
        exit 1
    fi

    # Mark as installed
    echo "grpc=installed" >> "$MANIFEST_FILE"
    echo -e "${GREEN}✓ gRPC installed from system packages${NC}"

else
    echo -e "${YELLOW}ℹ${NC} gRPC already installed"

    if ! grep -q "^grpc=" "$MANIFEST_FILE"; then
        echo "grpc=existing" >> "$MANIFEST_FILE"
    fi
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
cd ${SCRIPT_DIR}/../modules/mod_audio_fork || exit 1

echo "  Compiling mod_audio_fork.c..."
log_command "mod_audio_fork.c" "${LOG_DIR}/mod_audio_fork_c.log" \
    gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_audio_fork.c
check_success "Failed to compile mod_audio_fork.c" "${LOG_DIR}/mod_audio_fork_c.log"

echo "  Compiling mod_audio_fork C++ sources..."
log_command "mod_audio_fork C++" "${LOG_DIR}/mod_audio_fork_cpp.log" \
    g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include lws_glue.cpp audio_pipe.cpp parser.cpp
check_success "Failed to compile mod_audio_fork C++ sources" "${LOG_DIR}/mod_audio_fork_cpp.log"

mkdir -p ${FS_PREFIX}/lib/freeswitch/mod

echo "  Linking mod_audio_fork..."
log_command "mod_audio_fork link" "${LOG_DIR}/mod_audio_fork_link.log" \
    g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so *.o -lwebsockets -lpthread -lssl -lcrypto
check_success "Failed to link mod_audio_fork" "${LOG_DIR}/mod_audio_fork_link.log"

echo -e "${GREEN}✓ mod_audio_fork${NC}"

# mod_aws_transcribe
echo "Building mod_aws_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_aws_transcribe || exit 1

echo "  Compiling mod_aws_transcribe.c..."
log_command "mod_aws_transcribe.c" "${LOG_DIR}/mod_aws_transcribe_c.log" \
    gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch mod_aws_transcribe.c
check_success "Failed to compile mod_aws_transcribe.c" "${LOG_DIR}/mod_aws_transcribe_c.log"

echo "  Compiling mod_aws_transcribe C++ sources..."
log_command "mod_aws_transcribe C++" "${LOG_DIR}/mod_aws_transcribe_cpp.log" \
    g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include aws_transcribe_glue.cpp
check_success "Failed to compile mod_aws_transcribe C++ sources" "${LOG_DIR}/mod_aws_transcribe_cpp.log"

echo "  Linking mod_aws_transcribe..."
log_command "mod_aws_transcribe link" "${LOG_DIR}/mod_aws_transcribe_link.log" \
    g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so \
    mod_aws_transcribe.o aws_transcribe_glue.o \
    -L/usr/local/lib -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core \
    -laws-c-event-stream -laws-checksums -laws-c-common \
    -lpthread -lcurl -lssl -lcrypto -lz
check_success "Failed to link mod_aws_transcribe" "${LOG_DIR}/mod_aws_transcribe_link.log"

echo -e "${GREEN}✓ mod_aws_transcribe${NC}"

# mod_deepgram_transcribe
echo "Building mod_deepgram_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_deepgram_transcribe || exit 1

echo "  Compiling mod_deepgram_transcribe.c..."
log_command "mod_deepgram_transcribe.c" "${LOG_DIR}/mod_deepgram_transcribe_c.log" \
    gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_deepgram_transcribe.c
check_success "Failed to compile mod_deepgram_transcribe.c" "${LOG_DIR}/mod_deepgram_transcribe_c.log"

echo "  Compiling mod_deepgram_transcribe C++ sources..."
log_command "mod_deepgram_transcribe C++" "${LOG_DIR}/mod_deepgram_transcribe_cpp.log" \
    g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp
check_success "Failed to compile mod_deepgram_transcribe C++ sources" "${LOG_DIR}/mod_deepgram_transcribe_cpp.log"

echo "  Linking mod_deepgram_transcribe..."
log_command "mod_deepgram_transcribe link" "${LOG_DIR}/mod_deepgram_transcribe_link.log" \
    g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    mod_deepgram_transcribe.o dg_transcribe_glue.o audio_pipe.o parser.o \
    -lwebsockets -lpthread -lssl -lcrypto
check_success "Failed to link mod_deepgram_transcribe" "${LOG_DIR}/mod_deepgram_transcribe_link.log"

echo -e "${GREEN}✓ mod_deepgram_transcribe${NC}"

# mod_google_transcribe
echo "Building mod_google_transcribe..."

cd ${SCRIPT_DIR}/../modules/mod_google_transcribe || exit 1

# Check for required tools
if ! command -v protoc &> /dev/null; then
    echo -e "${RED}✗ protoc not found${NC}"
    echo "Protocol Buffers compiler is required"
    exit 1
fi

if ! command -v grpc_cpp_plugin &> /dev/null; then
    echo -e "${RED}✗ grpc_cpp_plugin not found${NC}"
    echo "gRPC C++ plugin is required"
    exit 1
fi

# Generate protobuf files if they don't exist or are older than .proto file
if [ ! -f "speech.pb.h" ] || [ "speech.proto" -nt "speech.pb.h" ]; then
    echo "  Generating protobuf files..."
    log_command "protoc" "${LOG_DIR}/mod_google_transcribe_protoc.log" \
        protoc --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` speech.proto
    check_success "Failed to generate protobuf files" "${LOG_DIR}/mod_google_transcribe_protoc.log"
fi

# Build using Makefile
echo "  Cleaning previous build..."
log_command "mod_google_transcribe clean" "${LOG_DIR}/mod_google_transcribe_clean.log" \
    make clean
# Don't check success for clean, it's okay if it fails

echo "  Building mod_google_transcribe..."
log_command "mod_google_transcribe make" "${LOG_DIR}/mod_google_transcribe_make.log" \
    make
check_success "Failed to build mod_google_transcribe" "${LOG_DIR}/mod_google_transcribe_make.log"

# Install the module
echo "  Installing mod_google_transcribe..."
log_command "mod_google_transcribe install" "${LOG_DIR}/mod_google_transcribe_install.log" \
    make install
check_success "Failed to install mod_google_transcribe" "${LOG_DIR}/mod_google_transcribe_install.log"

echo -e "${GREEN}✓ mod_google_transcribe${NC}"

# Mark modules as installed
sed -i '/^modules=/d' "$MANIFEST_FILE" 2>/dev/null || true
echo "modules=installed" >> "$MANIFEST_FILE"

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
        sed -i '/<\/modules>/i \    <load module="mod_google_transcribe"/>' "$MODULES_CONF"
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

for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe mod_google_transcribe; do
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
echo "  3. Verify: ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram|google'"
echo ""
