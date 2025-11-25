#!/bin/bash
# ============================================================================
# Google Module Installation Script
# ============================================================================
# Installs gRPC, googleapis, and mod_google_transcribe for FreeSWITCH
#
# This script is called by install-all.sh or can be run standalone
#
# Usage:
#   sudo ./scripts/install-google-module.sh [GRPC_VERSION] [BUILD_CPUS]
#
# Examples:
#   sudo ./scripts/install-google-module.sh
#   sudo ./scripts/install-google-module.sh 1.64.2 8
#
# ============================================================================

set -e

# Configuration
GRPC_VERSION=${1:-1.64.2}
BUILD_CPUS=${2:-$(nproc 2>/dev/null || echo "4")}
FS_PREFIX=${FS_PREFIX:-/usr/local/freeswitch}
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
RED='\033[0;31m'
NC='\033[0m'

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    exit 1
fi

echo -e "${CYAN}=========================================${NC}"
echo -e "${CYAN}Installing Google Module Dependencies${NC}"
echo -e "${CYAN}=========================================${NC}"
echo "  gRPC Version: ${GRPC_VERSION}"
echo "  Build CPUs: ${BUILD_CPUS}"
echo "  FreeSWITCH Prefix: ${FS_PREFIX}"
echo ""

# ============================================================================
# Step 1: Build gRPC and Protocol Buffers
# ============================================================================
echo -e "${CYAN}Step 1: Building gRPC v${GRPC_VERSION}${NC}"

if ldconfig -p | grep -q libgrpc++; then
    echo -e "${YELLOW}  gRPC already installed - skipping${NC}"
else
    echo -e "${CYAN}  Cloning gRPC repository...${NC}"
    cd /usr/local/src
    if [ -d "grpc" ]; then
        rm -rf grpc
    fi
    git clone --depth 1 -b v${GRPC_VERSION} https://github.com/grpc/grpc
    cd grpc
    git submodule update --init --recursive
    echo -e "${GREEN}  ✓ gRPC source downloaded${NC}"

    echo -e "${CYAN}  Building gRPC (20-40 minutes)...${NC}"
    mkdir -p cmake/build && cd cmake/build
    cmake ../.. \
        -DBUILD_SHARED_LIBS=ON \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DgRPC_SSL_PROVIDER=package \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17

    make -j ${BUILD_CPUS}
    make install
    ldconfig
    echo -e "${GREEN}  ✓ gRPC v${GRPC_VERSION} installed${NC}"
fi

# ============================================================================
# Step 2: Install googleapis and Generate Protobuf Files
# ============================================================================
echo -e "${CYAN}Step 2: Installing googleapis for Speech V2 API${NC}"

cd /usr/local/src
if [ -d "googleapis" ]; then
    rm -rf googleapis
fi

echo -e "${CYAN}  Cloning googleapis repository...${NC}"
git clone --depth 1 https://github.com/googleapis/googleapis.git
cd googleapis

echo -e "${CYAN}  Generating protobuf files for Speech V2 API...${NC}"
mkdir -p gens
/usr/local/bin/protoc \
    --proto_path=. \
    --cpp_out=gens \
    --grpc_out=gens \
    --plugin=protoc-gen-grpc=/usr/local/bin/grpc_cpp_plugin \
    google/cloud/speech/v2/*.proto \
    google/api/*.proto \
    google/rpc/*.proto \
    google/longrunning/*.proto \
    google/type/*.proto

# Count generated files
GENERATED_FILES=$(find gens -type f -name "*.pb.cc" | wc -l)
echo -e "${GREEN}  ✓ Generated ${GENERATED_FILES} protobuf source files${NC}"

# Verify Speech V2 API files
if [ -f "gens/google/cloud/speech/v2/cloud_speech.pb.cc" ]; then
    echo -e "${GREEN}  ✓ Google Cloud Speech V2 API files generated${NC}"
else
    echo -e "${RED}  ✗ Failed to generate Speech V2 API files${NC}"
    exit 1
fi

# ============================================================================
# Step 3: Build mod_google_transcribe
# ============================================================================
echo -e "${CYAN}Step 3: Building mod_google_transcribe${NC}"

# Check if FreeSWITCH is installed
if [ ! -d "${FS_PREFIX}/include/freeswitch" ]; then
    echo -e "${RED}Error: FreeSWITCH not found at ${FS_PREFIX}${NC}"
    exit 1
fi

cd "${SCRIPT_DIR}/../modules/mod_google_transcribe"
if [ ! -f "mod_google_transcribe.c" ]; then
    echo -e "${RED}Error: mod_google_transcribe.c not found${NC}"
    exit 1
fi

echo -e "${CYAN}  Compiling mod_google_transcribe.c...${NC}"
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    mod_google_transcribe.c
echo -e "${GREEN}  ✓ mod_google_transcribe.c compiled${NC}"

echo -e "${CYAN}  Compiling google_glue.cpp (C++17)...${NC}"
g++ -fPIC -c -std=c++17 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    -I/usr/local/src/googleapis/gens \
    google_glue.cpp
echo -e "${GREEN}  ✓ google_glue.cpp compiled${NC}"

echo -e "${CYAN}  Linking mod_google_transcribe.so...${NC}"
mkdir -p ${FS_PREFIX}/lib/freeswitch/mod
g++ -shared \
    -o ${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so \
    mod_google_transcribe.o \
    google_glue.o \
    /usr/local/src/googleapis/gens/google/cloud/speech/v2/*.pb.cc \
    /usr/local/src/googleapis/gens/google/api/*.pb.cc \
    /usr/local/src/googleapis/gens/google/rpc/*.pb.cc \
    /usr/local/src/googleapis/gens/google/longrunning/*.pb.cc \
    /usr/local/src/googleapis/gens/google/type/*.pb.cc \
    -L/usr/local/lib \
    -lgrpc++ \
    -lgrpc \
    -lprotobuf \
    -lpthread \
    -lssl \
    -lcrypto \
    -lcurl \
    -lz
echo -e "${GREEN}  ✓ mod_google_transcribe.so linked${NC}"

ldconfig

# ============================================================================
# Step 4: Validation
# ============================================================================
echo -e "${CYAN}Step 4: Module Validation${NC}"

MODULE_PATH="${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so"

if [ ! -f "${MODULE_PATH}" ]; then
    echo -e "${RED}  ✗ Module file not found: ${MODULE_PATH}${NC}"
    exit 1
fi
echo -e "${GREEN}  ✓ Module file exists${NC}"

echo -e "${CYAN}  Checking dependencies...${NC}"
ldd "${MODULE_PATH}"

# Verify gRPC linkage
if ldd "${MODULE_PATH}" | grep -q "libgrpc++"; then
    echo -e "${GREEN}  ✓ gRPC++ linked${NC}"
else
    echo -e "${RED}  ✗ gRPC++ not linked${NC}"
    exit 1
fi

# Verify OpenSSL (for Pusher)
if ldd "${MODULE_PATH}" | grep -q "libssl"; then
    echo -e "${GREEN}  ✓ OpenSSL linked (Pusher HMAC)${NC}"
else
    echo -e "${RED}  ✗ OpenSSL not linked${NC}"
    exit 1
fi

# Verify libcurl (for Pusher)
if ldd "${MODULE_PATH}" | grep -q "libcurl"; then
    echo -e "${GREEN}  ✓ libcurl linked (Pusher HTTP)${NC}"
else
    echo -e "${RED}  ✗ libcurl not linked${NC}"
    exit 1
fi

# Check for missing dependencies
if ldd "${MODULE_PATH}" | grep -q "not found"; then
    echo -e "${RED}  ✗ Missing dependencies found:${NC}"
    ldd "${MODULE_PATH}" | grep "not found"
    exit 1
fi
echo -e "${GREEN}  ✓ No missing dependencies${NC}"

# Set permissions
chown freeswitch:freeswitch "${MODULE_PATH}" 2>/dev/null || true

echo ""
echo -e "${GREEN}=========================================${NC}"
echo -e "${GREEN}Google Module Installation Complete!${NC}"
echo -e "${GREEN}=========================================${NC}"
echo ""
echo "Module location:"
echo "  ${MODULE_PATH}"
echo ""
echo "Next steps:"
echo "  1. Add to modules.conf.xml:"
echo "     <load module=\"mod_google_transcribe\"/>"
echo ""
echo "  2. Set Google Cloud credentials:"
echo "     export GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account.json"
echo ""
echo "  3. (Optional) Configure Pusher:"
echo "     export PUSHER_APP_ID=your-app-id"
echo "     export PUSHER_KEY=your-key"
echo "     export PUSHER_SECRET=your-secret"
echo ""
