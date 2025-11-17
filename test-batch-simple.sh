#!/bin/bash
set -e

# Simplified batch testing - skips apt-get, focuses on building
# Usage: ./test-batch-simple.sh [batch_number]

log_info() { echo "[INFO] $1"; }
log_error() { echo "[ERROR] $1" >&2; }
log_success() { echo "[SUCCESS] $1"; }

# Check if script is run as root
if [ "$EUID" -ne 0 ]; then
    log_error "Please run as root (use sudo)"
    log_error "This script needs root access for 'make install' and 'ldconfig' commands"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Read versions from .env
CMAKE_VERSION=$(grep cmakeVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
GRPC_VERSION=$(grep grpcVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
LIBWEBSOCKETS_VERSION=$(grep libwebsocketsVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SPEECH_SDK_VERSION=$(grep speechSdkVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SPANDSP_VERSION=$(grep spandspVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
SOFIA_VERSION=$(grep sofiaVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
AWS_SDK_CPP_VERSION=$(grep awsSdkCppVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')
FREESWITCH_VERSION=$(grep freeswitchVersion .env | awk -F '=' '{print $2}' | awk '{print $1}')

BUILD_DIR="/tmp/freeswitch-build"
BUILD_CPUS=$(nproc)
export LD_LIBRARY_PATH=/usr/local/lib:${LD_LIBRARY_PATH}

mkdir -p $BUILD_DIR
cd $BUILD_DIR

log_info "Configuration: CMake=$CMAKE_VERSION, gRPC=$GRPC_VERSION, AWS=$AWS_SDK_CPP_VERSION"
log_info "Build directory: $BUILD_DIR"
log_info "Build CPUs: $BUILD_CPUS"

# Batch 1: CMake
batch_1() {
    log_info "========== BATCH 1: CMake =========="

    if [ -f "/tmp/cmake-$CMAKE_VERSION/bin/cmake" ]; then
        log_info "CMake already built, installing..."
        cd /tmp/cmake-$CMAKE_VERSION
        make install
        ldconfig
    else
        log_error "CMake not found. Please run test-cmake-build.sh first"
        exit 1
    fi

    cmake --version
    log_success "BATCH 1 COMPLETE: CMake $(cmake --version | head -1)"
}

# Batch 2: gRPC
batch_2() {
    log_info "========== BATCH 2: gRPC + Protobuf =========="
    cd $BUILD_DIR

    if [ ! -d "grpc" ]; then
        log_info "Cloning gRPC $GRPC_VERSION..."
        git clone --depth 1 -b v$GRPC_VERSION https://github.com/grpc/grpc
        cd grpc
        log_info "Updating submodules..."
        git submodule update --init --recursive
    else
        log_info "gRPC already cloned"
        cd grpc
    fi

    if [ ! -d "cmake/build" ]; then
        mkdir -p cmake/build
    fi

    cd cmake/build

    if [ ! -f "Makefile" ]; then
        log_info "Configuring gRPC..."
        cmake ../.. -DBUILD_SHARED_LIBS=ON -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF \
            -DgRPC_SSL_PROVIDER=package -DCMAKE_BUILD_TYPE=Release
    fi

    log_info "Building gRPC (15-30 minutes)..."
    make -j ${BUILD_CPUS}

    log_info "Installing gRPC..."
    make install
    ldconfig

    /usr/local/bin/protoc --version
    ls -lh /usr/local/bin/grpc_cpp_plugin
    log_success "BATCH 2 COMPLETE: gRPC and protoc installed"
}

# Batch 3: googleapis + libwebsockets
batch_3() {
    log_info "========== BATCH 3: googleapis + libwebsockets =========="
    cd $BUILD_DIR

    # googleapis
    if [ ! -d "googleapis" ]; then
        log_info "Cloning googleapis..."
        git clone --depth 1 https://github.com/googleapis/googleapis.git
    else
        log_info "googleapis already cloned"
    fi

    # libwebsockets
    if [ ! -d "libwebsockets" ]; then
        log_info "Cloning libwebsockets $LIBWEBSOCKETS_VERSION..."
        git clone --depth 1 -b v$LIBWEBSOCKETS_VERSION https://github.com/warmcat/libwebsockets.git
        cd libwebsockets
        mkdir -p build
    else
        log_info "libwebsockets already cloned"
        cd libwebsockets
    fi

    cd build

    if [ ! -f "Makefile" ]; then
        log_info "Configuring libwebsockets..."
        cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
    fi

    log_info "Building libwebsockets..."
    make -j ${BUILD_CPUS}
    make install
    ldconfig

    ls -lh /usr/local/lib/libwebsockets.so*
    log_success "BATCH 3 COMPLETE: googleapis and libwebsockets ready"
}

# Batch 4: Azure Speech SDK
batch_4() {
    log_info "========== BATCH 4: Azure Speech SDK =========="

    if [ ! -d "/usr/local/include/MicrosoftSpeechSDK" ]; then
        cd /tmp
        log_info "Downloading Azure Speech SDK (latest)..."
        wget -q https://aka.ms/csspeech/linuxbinary -O SpeechSDK-Linux.tar.gz
        log_info "Extracting Azure Speech SDK..."
        tar xzf SpeechSDK-Linux.tar.gz
        cd SpeechSDK-Linux-*

        log_info "Installing headers and libraries..."
        cp -r include /usr/local/include/MicrosoftSpeechSDK
        cp -r lib/ /usr/local/lib/MicrosoftSpeechSDK
        cp /usr/local/lib/MicrosoftSpeechSDK/x64/libMicrosoft.*.so /usr/local/lib/
        ldconfig
    else
        log_info "Azure Speech SDK already installed"
    fi

    ls -lh /usr/local/lib/libMicrosoft.CognitiveServices.Speech.core.so
    log_success "BATCH 4 COMPLETE: Azure Speech SDK installed"
}

# Batch 5: spandsp + sofia-sip + libfvad
batch_5() {
    log_info "========== BATCH 5: spandsp + sofia-sip + libfvad =========="
    cd $BUILD_DIR

    # spandsp
    if [ ! -d "spandsp" ]; then
        log_info "Cloning spandsp..."
        git clone https://github.com/freeswitch/spandsp.git
        cd spandsp
        git checkout $SPANDSP_VERSION
        ./bootstrap.sh
        ./configure
    else
        log_info "spandsp already cloned"
        cd spandsp
    fi

    log_info "Building spandsp..."
    make -j ${BUILD_CPUS}
    make install
    ldconfig

    # sofia-sip
    cd $BUILD_DIR
    if [ ! -d "sofia-sip" ]; then
        log_info "Cloning sofia-sip $SOFIA_VERSION..."
        git clone --depth 1 -b v$SOFIA_VERSION https://github.com/freeswitch/sofia-sip.git
        cd sofia-sip
        ./bootstrap.sh
        ./configure
    else
        log_info "sofia-sip already cloned"
        cd sofia-sip
    fi

    log_info "Building sofia-sip..."
    make -j ${BUILD_CPUS}
    make install
    ldconfig

    # libfvad
    cd $BUILD_DIR
    if [ ! -d "libfvad" ]; then
        log_info "Cloning libfvad..."
        git clone --depth 1 https://github.com/dpirch/libfvad.git
        cd libfvad
        autoreconf -i
        ./configure
    else
        log_info "libfvad already cloned"
        cd libfvad
    fi

    log_info "Building libfvad..."
    make -j ${BUILD_CPUS}
    make install
    ldconfig

    ls -lh /usr/local/lib/libspandsp.so /usr/local/lib/libsofia-sip-ua.so /usr/local/lib/libfvad.so
    log_success "BATCH 5 COMPLETE: spandsp, sofia-sip, libfvad installed"
}

# Batch 6: AWS SDK C++ + AWS C Common
batch_6() {
    log_info "========== BATCH 6: AWS SDK C++ + AWS C Common =========="
    cd $BUILD_DIR

    # AWS SDK C++
    if [ ! -d "aws-sdk-cpp" ]; then
        log_info "Cloning AWS SDK C++ $AWS_SDK_CPP_VERSION..."
        git clone --depth 1 -b $AWS_SDK_CPP_VERSION https://github.com/aws/aws-sdk-cpp.git
        cd aws-sdk-cpp
        log_info "Updating submodules..."
        git submodule update --init --recursive
        mkdir -p build
    else
        log_info "AWS SDK C++ already cloned"
        cd aws-sdk-cpp
    fi

    cd build

    if [ ! -f "Makefile" ]; then
        log_info "Configuring AWS SDK C++..."
        cmake .. -DBUILD_ONLY="lexv2-runtime;transcribestreaming" -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_SHARED_LIBS=ON -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull -Wno-error=deprecated-declarations -Wno-error=uninitialized -Wno-error=maybe-uninitialized"
    fi

    log_info "Building AWS SDK C++ (20-40 minutes)..."
    make -j ${BUILD_CPUS}

    log_info "Installing AWS SDK C++..."
    make install

    # Copy pkg-config files
    mkdir -p /usr/local/lib/pkgconfig
    find $BUILD_DIR/aws-sdk-cpp/ -type f -name "*.pc" | xargs -I {} cp {} /usr/local/lib/pkgconfig/ || true
    ldconfig

    # AWS C Common
    cd $BUILD_DIR
    if [ ! -d "aws-c-common" ]; then
        log_info "Cloning AWS C Common..."
        git clone --depth 1 https://github.com/awslabs/aws-c-common.git
        cd aws-c-common
        mkdir -p build
    else
        log_info "AWS C Common already cloned"
        cd aws-c-common
    fi

    cd build

    if [ ! -f "Makefile" ]; then
        log_info "Configuring AWS C Common..."
        cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_FLAGS="-Wno-unused-parameter"
    fi

    log_info "Building AWS C Common..."
    make -j ${BUILD_CPUS}
    make install
    ldconfig

    ls -lh /usr/local/lib/libaws-cpp-sdk-transcribestreaming.so /usr/local/lib/libaws-c-common.a
    log_success "BATCH 6 COMPLETE: AWS SDK C++ and AWS C Common installed"
}

# Batch 7: FreeSWITCH + Modules
batch_7() {
    log_info "========== BATCH 7: FreeSWITCH + Modules =========="
    cd $BUILD_DIR

    if [ ! -d "freeswitch" ]; then
        log_info "Cloning FreeSWITCH $FREESWITCH_VERSION..."
        git clone --depth 1 -b v$FREESWITCH_VERSION https://github.com/signalwire/freeswitch.git
    else
        log_info "FreeSWITCH already cloned"
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
    if ! grep -q "mod_audio_fork" modules.conf; then
        cat >> modules.conf <<EOF
applications/mod_audio_fork
applications/mod_aws_transcribe
applications/mod_azure_transcribe
applications/mod_deepgram_transcribe
applications/mod_google_transcribe
EOF
    fi

    # Prepare AWS SDK tarball for FreeSWITCH build system
    log_info "Preparing AWS SDK tarball..."
    mkdir -p libs/aws-sdk-cpp
    if [ ! -f "${SCRIPT_DIR}/files/aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz" ]; then
        log_info "Downloading AWS SDK C++ ${AWS_SDK_CPP_VERSION} tarball..."
        cd /tmp
        wget -q https://github.com/aws/aws-sdk-cpp/archive/refs/tags/${AWS_SDK_CPP_VERSION}.tar.gz -O aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz
        mkdir -p ${SCRIPT_DIR}/files
        mv aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz ${SCRIPT_DIR}/files/
        cd $BUILD_DIR/freeswitch
    fi
    cp ${SCRIPT_DIR}/files/aws-sdk-cpp-${AWS_SDK_CPP_VERSION}.tar.gz libs/aws-sdk-cpp/

    # Copy mod_conference files
    log_info "Copying mod_conference files..."
    cd src/mod/applications/mod_conference
    cp ${SCRIPT_DIR}/files/mod_conference.h .
    cp ${SCRIPT_DIR}/files/conference_api.c .

    # Fix cJSON header conflict
    cd $BUILD_DIR/freeswitch
    log_info "Fixing cJSON header conflicts..."
    if [ -f "/usr/local/include/aws/core/external/cjson/cJSON.h" ]; then
        if ! grep -q "ifndef cJSON__h" /usr/local/include/aws/core/external/cjson/cJSON.h; then
            sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' /usr/local/include/aws/core/external/cjson/cJSON.h
            echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h
            log_info "cJSON header fixed"
        else
            log_info "cJSON header already fixed"
        fi
    fi

    # Bootstrap and configure
    if [ ! -f "configure" ]; then
        log_info "Bootstrapping FreeSWITCH..."
        ./bootstrap.sh -j
    fi

    if [ ! -f "Makefile" ]; then
        log_info "Configuring FreeSWITCH..."
        ./configure --enable-tcmalloc=yes --with-lws=yes --with-extra=yes --with-aws=yes
    fi

    # Build
    log_info "Building FreeSWITCH (20-30 minutes)..."
    make -j ${BUILD_CPUS}

    # Install
    log_info "Installing FreeSWITCH..."
    make install

    # Verify modules
    log_info "Verifying modules..."
    MODULE_DIR="/usr/local/freeswitch/mod"
    MODULES_TO_CHECK=("mod_audio_fork" "mod_aws_transcribe" "mod_azure_transcribe" "mod_deepgram_transcribe" "mod_google_transcribe")

    ALL_MODULES_EXIST=true
    for module in "${MODULES_TO_CHECK[@]}"; do
        if [ -f "$MODULE_DIR/${module}.so" ]; then
            log_success "  ✓ ${module}.so found"
            # Check dependencies
            if ldd "$MODULE_DIR/${module}.so" | grep -q "not found"; then
                log_error "  ✗ ${module} has missing dependencies:"
                ldd "$MODULE_DIR/${module}.so" | grep "not found"
                ALL_MODULES_EXIST=false
            else
                log_success "  ✓ ${module} dependencies OK"
            fi
        else
            log_error "  ✗ ${module}.so NOT FOUND"
            ALL_MODULES_EXIST=false
        fi
    done

    if [ "$ALL_MODULES_EXIST" = true ]; then
        log_success "BATCH 7 COMPLETE: FreeSWITCH and all modules installed"
    else
        log_error "BATCH 7 FAILED: Some modules missing or have dependency issues"
        exit 1
    fi
}

# Main
BATCH=${1:-1}

case "$BATCH" in
    1) batch_1 ;;
    2) batch_2 ;;
    3) batch_3 ;;
    4) batch_4 ;;
    5) batch_5 ;;
    6) batch_6 ;;
    7) batch_7 ;;
    all)
        batch_1
        batch_2
        batch_3
        batch_4
        batch_5
        batch_6
        batch_7
        ;;
    *)
        echo "Usage: $0 [1|2|3|4|5|6|7|all]"
        echo "  1 - CMake (1 min)"
        echo "  2 - gRPC + Protobuf (15-30 min)"
        echo "  3 - googleapis + libwebsockets (5-10 min)"
        echo "  4 - Azure Speech SDK (1 min)"
        echo "  5 - spandsp + sofia-sip + libfvad (10-15 min)"
        echo "  6 - AWS SDK C++ + AWS C Common (20-40 min)"
        echo "  7 - FreeSWITCH + Modules (20-30 min)"
        echo "  all - Run all batches sequentially"
        exit 1
        ;;
esac

log_success "========== Batch $BATCH COMPLETED =========="
