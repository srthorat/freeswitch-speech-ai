#!/bin/bash
set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info()    { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[OK]${NC}  $1"; }
log_warn()    { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error()   { echo -e "${RED}[ERROR]${NC} $1"; }

MANIFEST_FILE="/usr/local/src/install.manifest"
mkdir -p /usr/local/src


# ==========================================
# RELIABLE gRPC DETECTION (NO LDCONFIG)
# ==========================================
grpc_installed() {
    [[ -f /usr/lib/x86_64-linux-gnu/libgrpc++.so ]] && \
    [[ -x /usr/bin/grpc_cpp_plugin ]] && \
    [[ -x /usr/bin/protoc ]]
}


log_info "Checking if gRPC runtime is already installed..."

if grpc_installed; then
    log_warn "gRPC is already installed (file check passed)"

    if ! grep -q "^grpc=" "$MANIFEST_FILE" 2>/dev/null; then
        echo "grpc=existing" >> "$MANIFEST_FILE"
    fi

    log_success "Skipping gRPC installation"
else
    # ==========================================
    # INSTALL gRPC VIA DEBIAN PACKAGES
    # ==========================================
    log_info "Installing gRPC from Debian repositories..."

    sed -i '/^grpc=/d' "$MANIFEST_FILE" 2>/dev/null || true
    sed -i '/^googleapis=/d' "$MANIFEST_FILE" 2>/dev/null || true

    apt-get update -y

    apt-get install -y \
        libgrpc++-dev \
        libgrpc-dev \
        protobuf-compiler \
        protobuf-compiler-grpc \
        libprotobuf-dev

    # Validate via file checks
    if ! grpc_installed; then
        log_error "gRPC still NOT found after installation!"
        echo "Debug listing:"
        ls -l /usr/lib/x86_64-linux-gnu/libgrpc*
        exit 1
    fi

    echo "grpc=installed" >> "$MANIFEST_FILE"
    log_success "gRPC installed successfully"
fi

# ==========================================
# CLONE GOOGLEAPIS AND GENERATE PROTOS
# ==========================================

log_info "Preparing googleapis proto definitions..."

cd /usr/local/src
rm -rf googleapis

git clone --depth 1 https://github.com/googleapis/googleapis.git
cd googleapis
mkdir -p gens

log_info "Generating Speech V2 protobufs..."

protoc \
    --proto_path=. \
    --cpp_out=gens \
    --grpc_out=gens \
    --plugin=protoc-gen-grpc=/usr/bin/grpc_cpp_plugin \
    google/cloud/speech/v2/*.proto \
    google/api/*.proto \
    google/rpc/*.proto \
    google/longrunning/*.proto \
    google/type/*.proto \
    2>&1 | grep -v "warning:"

if [[ ! -f "gens/google/cloud/speech/v2/cloud_speech.pb.cc" ]]; then
    log_error "Protobuf generation failed (cloud_speech.pb.cc missing)"
    exit 1
fi

GEN=$(find gens -type f -name "*.pb.cc" | wc -l)
log_success "Generated $GEN protobuf files"

echo "googleapis=installed" >> "$MANIFEST_FILE"
log_success "✓ All dependencies installed successfully"
