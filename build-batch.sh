#!/bin/bash
set -e

# Build FreeSWITCH in batches for incremental verification
# Usage: sudo ./build-batch.sh [batch_number] [options]
# Example: sudo ./build-batch.sh 1           # Run only batch 1
#          sudo ./build-batch.sh all         # Run all batches
#          sudo ./build-batch.sh all --clean # Clean rebuild all batches
#          sudo ./build-batch.sh -h          # Show help

# Parse options
CLEAN_MODE=false
SHOW_HELP=false

for arg in "$@"; do
    case $arg in
        --clean)
            CLEAN_MODE=true
            shift
            ;;
        -h|--help)
            SHOW_HELP=true
            shift
            ;;
    esac
done

# Color output
log_info() {
    echo "[INFO] $1"
}

log_error() {
    echo "[ERROR] $1" >&2
}

log_success() {
    echo "[SUCCESS] $1"
}

# Show help
if [ "$SHOW_HELP" = true ]; then
    cat <<EOF
FreeSWITCH Batch Build Script
=============================

Usage: sudo $0 [BATCH] [OPTIONS]

BATCH:
  1       - System Dependencies + CMake (5-10 min)
  2       - gRPC + Protocol Buffers (15-30 min)
  3       - googleapis + libwebsockets (5-10 min)
  4       - Azure Speech SDK (1-2 min)
  5       - spandsp + sofia-sip + libfvad (10-15 min)
  6       - AWS SDK C++ + AWS C Common (20-40 min)
  7       - FreeSWITCH + Modules (20-30 min)
  all     - Run all batches sequentially

OPTIONS:
  --clean        Force clean rebuild (removes existing build directories)
  -h, --help     Show this help message

EXAMPLES:
  sudo $0 1                # Build batch 1 (incremental)
  sudo $0 all              # Build all batches (incremental)
  sudo $0 all --clean      # Clean rebuild all batches
  sudo $0 7 --clean        # Clean rebuild only batch 7

LOGS:
  Build logs are saved to: build-logs/batch-N.log
  Combined log for 'all': build-logs/build-all.log

EOF
    exit 0
fi

# Check if script is run as root
if [ "$EUID" -ne 0 ]; then
    log_error "Please run as root (use sudo)"
    exit 1
fi

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Check if .env file exists
if [ ! -f "$SCRIPT_DIR/.env" ]; then
    log_error ".env file not found in $SCRIPT_DIR"
    exit 1
fi

# Read versions from .env
log_info "Reading configuration from .env..."
cd "$SCRIPT_DIR"
CMAKE_VERSION=$(grep cmakeVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
GRPC_VERSION=$(grep grpcVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
LIBWEBSOCKETS_VERSION=$(grep libwebsocketsVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SPEECH_SDK_VERSION=$(grep speechSdkVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SPANDSP_VERSION=$(grep spandspVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SOFIA_VERSION=$(grep sofiaVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
AWS_SDK_CPP_VERSION=$(grep awsSdkCppVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
FREESWITCH_VERSION=$(grep freeswitchVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')

# Set build directory and environment
BUILD_DIR="/usr/local/src"
BUILD_CPUS=$(nproc)
export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}

# Setup build logs directory
LOGS_DIR="${SCRIPT_DIR}/build-logs"
mkdir -p "$LOGS_DIR"

log_info "Build configuration:"
log_info "  Build directory: $BUILD_DIR"
log_info "  Build CPUs: $BUILD_CPUS"
log_info "  Logs directory: $LOGS_DIR"
log_info "  Clean mode: $CLEAN_MODE"
log_info "  CMake: $CMAKE_VERSION"
log_info "  gRPC: $GRPC_VERSION"
log_info "  libwebsockets: $LIBWEBSOCKETS_VERSION"
log_info "  Speech SDK: $SPEECH_SDK_VERSION"
log_info "  AWS SDK C++: $AWS_SDK_CPP_VERSION"
log_info "  FreeSWITCH: $FREESWITCH_VERSION"

# Clean mode function
clean_batch() {
    local batch=$1
    log_info "Clean mode enabled for batch $batch"

    case $batch in
        1)
            rm -rf "$BUILD_DIR/cmake-$CMAKE_VERSION" "$BUILD_DIR/cmake-$CMAKE_VERSION.tar.gz"
            log_info "  Cleaned: CMake"
            ;;
        2)
            rm -rf "$BUILD_DIR/grpc"
            log_info "  Cleaned: gRPC"
            ;;
        3)
            rm -rf "$BUILD_DIR/googleapis" "$BUILD_DIR/libwebsockets"
            log_info "  Cleaned: googleapis, libwebsockets"
            ;;
        4)
            rm -rf /usr/local/include/MicrosoftSpeechSDK /usr/local/lib/MicrosoftSpeechSDK /usr/local/lib/libMicrosoft.*.so
            log_info "  Cleaned: Azure Speech SDK"
            ;;
        5)
            rm -rf "$BUILD_DIR/spandsp" "$BUILD_DIR/sofia-sip" "$BUILD_DIR/libfvad"
            log_info "  Cleaned: spandsp, sofia-sip, libfvad"
            ;;
        6)
            rm -rf "$BUILD_DIR/aws-sdk-cpp" "$BUILD_DIR/aws-c-common"
            log_info "  Cleaned: AWS SDK C++, AWS C Common"
            ;;
        7)
            rm -rf "$BUILD_DIR/freeswitch" /usr/local/freeswitch
            log_info "  Cleaned: FreeSWITCH"
            ;;
        all)
            log_info "  Cleaning all batches..."
            clean_batch 1
            clean_batch 2
            clean_batch 3
            clean_batch 4
            clean_batch 5
            clean_batch 6
            clean_batch 7
            ;;
    esac
}

cd $BUILD_DIR

# =============================================================================
# BATCH 1: System Dependencies + CMake
# =============================================================================
batch_1() {
    local LOG_FILE="$LOGS_DIR/batch-1.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 1: System Dependencies + CMake"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 1
    fi

    log_info "Step 1.1: Installing system dependencies..."
    for i in $(seq 1 8); do mkdir -p "/usr/share/man/man${i}"; done

    apt-get update && apt-get -y --quiet --allow-remove-essential upgrade
    apt-get -y --quiet install build-essential cmake automake autoconf libtool \
        pkg-config git wget curl libssl-dev zlib1g-dev libncurses5-dev \
        libsqlite3-dev libpcre3-dev libspeex-dev libspeexdsp-dev libedit-dev \
        libldns-dev liblua5.2-dev libopus-dev yasm nasm libavformat-dev \
        libswscale-dev libjpeg-dev ca-certificates libevent-dev \
        libgoogle-perftools-dev google-perftools libsndfile1-dev

    log_success "System dependencies installed"

    log_info "Step 1.2: Building CMake $CMAKE_VERSION..."
    cd $BUILD_DIR
    if [ ! -d "cmake-$CMAKE_VERSION" ]; then
        wget -q https://github.com/Kitware/CMake/releases/download/v$CMAKE_VERSION/cmake-$CMAKE_VERSION.tar.gz
        tar xzf cmake-$CMAKE_VERSION.tar.gz
        cd cmake-$CMAKE_VERSION
        ./bootstrap --parallel=${BUILD_CPUS}
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "CMake $CMAKE_VERSION built and installed"
    else
        log_info "CMake already exists, skipping..."
    fi

    # Verify
    cmake --version
    if [ $? -eq 0 ]; then
        log_success "BATCH 1 COMPLETED: CMake is working!"
    else
        log_error "BATCH 1 FAILED: CMake verification failed"
        exit 1
    fi
}

# =============================================================================
# BATCH 2: gRPC + Protocol Buffers
# =============================================================================
batch_2() {
    local LOG_FILE="$LOGS_DIR/batch-2.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 2: gRPC + Protocol Buffers"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 2
    fi

    log_info "Step 2.1: Building gRPC $GRPC_VERSION..."
    cd $BUILD_DIR
    if [ ! -d "grpc" ]; then
        git clone --depth 1 -b v$GRPC_VERSION https://github.com/grpc/grpc
        cd grpc
        git submodule update --init --recursive
        mkdir -p cmake/build
        cd cmake/build
        cmake ../.. -DBUILD_SHARED_LIBS=ON -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF \
            -DgRPC_SSL_PROVIDER=package -DCMAKE_BUILD_TYPE=Release
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "gRPC built and installed"
    else
        log_info "gRPC already exists, skipping..."
    fi

    # Verify
    if [ -f "/usr/local/bin/grpc_cpp_plugin" ] && [ -f "/usr/local/bin/protoc" ]; then
        /usr/local/bin/protoc --version
        log_success "BATCH 2 COMPLETED: gRPC and protoc are working!"
    else
        log_error "BATCH 2 FAILED: gRPC verification failed"
        exit 1
    fi
}

# =============================================================================
# BATCH 3: googleapis + libwebsockets
# =============================================================================
batch_3() {
    local LOG_FILE="$LOGS_DIR/batch-3.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 3: googleapis + libwebsockets"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 3
    fi

    log_info "Step 3.1: Building googleapis..."
    cd $BUILD_DIR
    if [ ! -d "googleapis" ]; then
        git clone --depth 1 https://github.com/googleapis/googleapis.git
        log_success "googleapis cloned"
    else
        log_info "googleapis already exists, skipping..."
    fi

    log_info "Step 3.2: Building libwebsockets $LIBWEBSOCKETS_VERSION..."
    cd $BUILD_DIR
    if [ ! -d "libwebsockets" ]; then
        git clone --depth 1 -b v$LIBWEBSOCKETS_VERSION https://github.com/warmcat/libwebsockets.git
        cd libwebsockets
        mkdir -p build
        cd build
        cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "libwebsockets built and installed"
    else
        log_info "libwebsockets already exists, skipping..."
    fi

    # Verify
    if [ -d "$BUILD_DIR/googleapis" ] && [ -f "/usr/local/lib/libwebsockets.so" ]; then
        log_success "BATCH 3 COMPLETED: googleapis and libwebsockets are ready!"
    else
        log_error "BATCH 3 FAILED: Verification failed"
        exit 1
    fi
}

# =============================================================================
# BATCH 4: Azure Speech SDK
# =============================================================================
batch_4() {
    local LOG_FILE="$LOGS_DIR/batch-4.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 4: Azure Speech SDK"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 4
    fi

    log_info "Downloading and installing Azure Speech SDK (latest)..."
    if [ ! -d "/usr/local/include/MicrosoftSpeechSDK" ]; then
        cd /tmp
        wget -q https://aka.ms/csspeech/linuxbinary -O SpeechSDK-Linux.tar.gz
        tar xzf SpeechSDK-Linux.tar.gz
        cd SpeechSDK-Linux-*
        cp -r include /usr/local/include/MicrosoftSpeechSDK
        cp -r lib/ /usr/local/lib/MicrosoftSpeechSDK
        cp /usr/local/lib/MicrosoftSpeechSDK/x64/libMicrosoft.*.so /usr/local/lib/
        ldconfig /usr/local/lib
        log_success "Azure Speech SDK installed"
    else
        log_info "Azure Speech SDK already installed"
    fi

    # Verify
    if [ -d "/usr/local/include/MicrosoftSpeechSDK" ] && [ -f "/usr/local/lib/libMicrosoft.CognitiveServices.Speech.core.so" ]; then
        log_success "BATCH 4 COMPLETED: Azure Speech SDK is ready!"
    else
        log_error "BATCH 4 FAILED: Azure Speech SDK verification failed"
        exit 1
    fi
}

# =============================================================================
# BATCH 5: spandsp + sofia-sip + libfvad
# =============================================================================
batch_5() {
    local LOG_FILE="$LOGS_DIR/batch-5.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 5: spandsp + sofia-sip + libfvad"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 5
    fi

    log_info "Step 5.1: Building spandsp..."
    cd $BUILD_DIR
    if [ ! -d "spandsp" ]; then
        git clone https://github.com/freeswitch/spandsp.git
        cd spandsp
        git checkout $SPANDSP_VERSION
        ./bootstrap.sh
        ./configure
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "spandsp built and installed"
    else
        log_info "spandsp already exists, skipping..."
    fi

    log_info "Step 5.2: Building sofia-sip $SOFIA_VERSION..."
    cd $BUILD_DIR
    if [ ! -d "sofia-sip" ]; then
        git clone --depth 1 -b v$SOFIA_VERSION https://github.com/freeswitch/sofia-sip.git
        cd sofia-sip
        ./bootstrap.sh
        ./configure
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "sofia-sip built and installed"
    else
        log_info "sofia-sip already exists, skipping..."
    fi

    log_info "Step 5.3: Building libfvad..."
    cd $BUILD_DIR
    if [ ! -d "libfvad" ]; then
        git clone --depth 1 https://github.com/dpirch/libfvad.git
        cd libfvad
        autoreconf -i
        ./configure
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "libfvad built and installed"
    else
        log_info "libfvad already exists, skipping..."
    fi

    # Verify
    if [ -f "/usr/local/lib/libspandsp.so" ] && [ -f "/usr/local/lib/libsofia-sip-ua.so" ] && [ -f "/usr/local/lib/libfvad.so" ]; then
        log_success "BATCH 5 COMPLETED: spandsp, sofia-sip, and libfvad are ready!"
    else
        log_error "BATCH 5 FAILED: Verification failed"
        exit 1
    fi
}

# =============================================================================
# BATCH 6: AWS SDK C++ + AWS C Common
# =============================================================================
batch_6() {
    local LOG_FILE="$LOGS_DIR/batch-6.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 6: AWS SDK C++ + AWS C Common"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 6
    fi

    log_info "Step 6.1: Building AWS SDK C++ $AWS_SDK_CPP_VERSION..."
    cd $BUILD_DIR
    if [ ! -d "aws-sdk-cpp" ]; then
        git clone --depth 1 -b $AWS_SDK_CPP_VERSION https://github.com/aws/aws-sdk-cpp.git
        cd aws-sdk-cpp
        git submodule update --init --recursive
        mkdir -p build
        cd build
        cmake .. -DBUILD_ONLY="lexv2-runtime;transcribestreaming" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_SHARED_LIBS=ON -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull -Wno-error=deprecated-declarations -Wno-error=uninitialized -Wno-error=maybe-uninitialized"
        make -j ${BUILD_CPUS}
        make install
        mkdir -p /usr/local/lib/pkgconfig
        find /usr/local/src/aws-sdk-cpp/ -type f -name "*.pc" | xargs cp -t /usr/local/lib/pkgconfig/
        ldconfig /usr/local/lib
        log_success "AWS SDK C++ built and installed"
    else
        log_info "AWS SDK C++ already exists, skipping..."
    fi

    log_info "Step 6.2: Building AWS C Common..."
    cd $BUILD_DIR
    if [ ! -d "aws-c-common" ]; then
        git clone --depth 1 https://github.com/awslabs/aws-c-common.git
        cd aws-c-common
        mkdir -p build
        cd build
        cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_FLAGS="-Wno-unused-parameter"
        make -j ${BUILD_CPUS}
        make install
        ldconfig /usr/local/lib
        log_success "AWS C Common built and installed"
    else
        log_info "AWS C Common already exists, skipping..."
    fi

    # Verify
    if [ -f "/usr/local/lib/libaws-cpp-sdk-transcribestreaming.so" ] && [ -f "/usr/local/lib/libaws-c-common.a" ]; then
        log_success "BATCH 6 COMPLETED: AWS SDK C++ and AWS C Common are ready!"
    else
        log_error "BATCH 6 FAILED: Verification failed"
        exit 1
    fi
}

# =============================================================================
# BATCH 7: FreeSWITCH + Modules
# =============================================================================
batch_7() {
    local LOG_FILE="$LOGS_DIR/batch-7.log"
    exec > >(tee "$LOG_FILE") 2>&1

    log_info "========================================="
    log_info "BATCH 7: FreeSWITCH + Modules"
    log_info "========================================="
    log_info "Log file: $LOG_FILE"

    # Clean if requested
    if [ "$CLEAN_MODE" = true ]; then
        clean_batch 7
    fi

    # Check if FreeSWITCH is already installed and all modules exist
    if [ "$CLEAN_MODE" = false ] && [ -f "/usr/local/freeswitch/bin/freeswitch" ]; then
        log_info "Checking if FreeSWITCH and modules are already installed..."

        MODULE_DIR="/usr/local/freeswitch/mod"
        MODULES_TO_CHECK=("mod_audio_fork" "mod_aws_transcribe" "mod_azure_transcribe" "mod_deepgram_transcribe" "mod_google_transcribe")
        ALL_MODULES_EXIST=true

        for module in "${MODULES_TO_CHECK[@]}"; do
            if [ ! -f "$MODULE_DIR/${module}.so" ]; then
                ALL_MODULES_EXIST=false
                break
            fi
        done

        if [ "$ALL_MODULES_EXIST" = true ]; then
            log_success "FreeSWITCH and all modules already installed, skipping build"
            log_success "BATCH 7 COMPLETED: FreeSWITCH is ready!"
            return 0
        else
            log_info "Some modules missing, will rebuild FreeSWITCH"
        fi
    fi

    log_info "Building FreeSWITCH $FREESWITCH_VERSION with transcription modules..."
    cd $BUILD_DIR

    if [ ! -d "freeswitch" ]; then
        log_info "Cloning FreeSWITCH..."
        git clone --depth 1 -b v$FREESWITCH_VERSION https://github.com/signalwire/freeswitch.git
    fi

    cd freeswitch

    # Copy modules
    log_info "Copying transcription modules..."
    cp -r ${SCRIPT_DIR}/modules/* src/mod/applications/

    # Copy googleapis
    log_info "Copying googleapis..."
    cp -r $BUILD_DIR/googleapis libs/

    # Add modules to modules.conf
    log_info "Adding modules to modules.conf..."
    cat >> modules.conf <<EOF
applications/mod_audio_fork
applications/mod_aws_transcribe
applications/mod_azure_transcribe
applications/mod_deepgram_transcribe
applications/mod_google_transcribe
EOF

    # Copy AWS SDK tarball for FreeSWITCH build system
    log_info "Preparing AWS SDK tarball..."
    mkdir -p libs/aws-sdk-cpp
    # FreeSWITCH expects AWS SDK tarball in libs/aws-sdk-cpp/
    # Using the version matching .env configuration
    if [ ! -f "${SCRIPT_DIR}/files/aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz" ]; then
        log_info "Downloading AWS SDK C++ ${AWS_SDK_CPP_VERSION} tarball..."
        cd /tmp
        wget -q https://github.com/aws/aws-sdk-cpp/archive/refs/tags/${AWS_SDK_CPP_VERSION}.tar.gz -O aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz
        mv aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz ${SCRIPT_DIR}/files/
        cd $BUILD_DIR/freeswitch
    fi
    cp ${SCRIPT_DIR}/files/aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz libs/aws-sdk-cpp/

    # Copy mod_conference files
    cd src/mod/applications/mod_conference
    cp ${SCRIPT_DIR}/files/mod_conference.h .
    cp ${SCRIPT_DIR}/files/conference_api.c .

    # Fix cJSON header conflict
    cd $BUILD_DIR/freeswitch
    log_info "Fixing cJSON header conflicts..."
    if ! grep -q "ifndef cJSON__h" /usr/local/include/aws/core/external/cjson/cJSON.h; then
        sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' /usr/local/include/aws/core/external/cjson/cJSON.h
        echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h
        log_info "cJSON header fixed"
    else
        log_info "cJSON header already fixed"
    fi

    # Bootstrap and configure
    log_info "Bootstrapping FreeSWITCH..."
    ./bootstrap.sh -j

    log_info "Configuring FreeSWITCH..."
    ./configure --enable-tcmalloc=yes --with-lws=yes --with-extra=yes --with-aws=yes

    # Build
    log_info "Building FreeSWITCH (this will take 20-30 minutes)..."
    make -j ${BUILD_CPUS}

    # Install
    log_info "Installing FreeSWITCH..."
    make install

    # Verify modules were built
    log_info "Verifying modules..."
    MODULE_DIR="/usr/local/freeswitch/mod"
    MODULES_TO_CHECK=("mod_audio_fork" "mod_aws_transcribe" "mod_azure_transcribe" "mod_deepgram_transcribe" "mod_google_transcribe")

    ALL_MODULES_EXIST=true
    for module in "${MODULES_TO_CHECK[@]}"; do
        if [ -f "$MODULE_DIR/${module}.so" ]; then
            log_success "  ✓ ${module}.so found"
        else
            log_error "  ✗ ${module}.so NOT FOUND"
            ALL_MODULES_EXIST=false
        fi
    done

    # Check dependencies
    for module in "${MODULES_TO_CHECK[@]}"; do
        if [ -f "$MODULE_DIR/${module}.so" ]; then
            if ldd "$MODULE_DIR/${module}.so" | grep -q "not found"; then
                log_error "  ✗ ${module} has missing dependencies:"
                ldd "$MODULE_DIR/${module}.so" | grep "not found"
                ALL_MODULES_EXIST=false
            else
                log_success "  ✓ ${module} dependencies OK"
            fi
        fi
    done

    if [ "$ALL_MODULES_EXIST" = true ]; then
        log_success "BATCH 7 COMPLETED: FreeSWITCH and all modules are ready!"
    else
        log_error "BATCH 7 FAILED: Some modules are missing or have dependency issues"
        exit 1
    fi
}

# =============================================================================
# Main execution
# =============================================================================

BATCH=${1:-1}

case "$BATCH" in
    1)
        batch_1
        ;;
    2)
        batch_2
        ;;
    3)
        batch_3
        ;;
    4)
        batch_4
        ;;
    5)
        batch_5
        ;;
    6)
        batch_6
        ;;
    7)
        batch_7
        ;;
    all)
        # Log all batches to a combined file
        LOG_FILE="$LOGS_DIR/build-all.log"
        exec > >(tee "$LOG_FILE") 2>&1
        log_info "Running all batches - combined log: $LOG_FILE"

        batch_1
        batch_2
        batch_3
        batch_4
        batch_5
        batch_6
        batch_7
        ;;
    *)
        echo "Usage: sudo $0 [1|2|3|4|5|6|7|all]"
        echo ""
        echo "Batches:"
        echo "  1 - System Dependencies + CMake (5-10 min)"
        echo "  2 - gRPC + Protocol Buffers (15-30 min)"
        echo "  3 - googleapis + libwebsockets (5-10 min)"
        echo "  4 - Azure Speech SDK (1-2 min)"
        echo "  5 - spandsp + sofia-sip + libfvad (10-15 min)"
        echo "  6 - AWS SDK C++ + AWS C Common (20-40 min)"
        echo "  7 - FreeSWITCH + Modules (20-30 min)"
        echo "  all - Run all batches sequentially"
        exit 1
        ;;
esac

log_success "========================================="
log_success "Batch $BATCH completed successfully!"
log_success "========================================="
