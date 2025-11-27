#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Unified Installation Script
# ============================================================================
# Supports modular installation via --module flag:
#   --module freeswitch              Install only FreeSWITCH
#   --module mod_audio_fork          Install only mod_audio_fork dependencies + module
#   --module mod_aws_transcribe      Install only mod_aws_transcribe dependencies + module
#   --module mod_deepgram_transcribe Install only mod_deepgram_transcribe dependencies + module
#   --module mod_google_transcribe   Install only mod_google_transcribe dependencies + module
#   --module mod_google_transcribev2 Install only mod_google_transcribev2 dependencies + module
#   --module all                     Install everything (default)
#
# Usage:
#   sudo ./install-all.sh --module <module-name> [OPTIONS]
#
# Options:
#   --module NAME           Module to install (freeswitch, mod_*, all)
#   --freeswitch-prefix PATH  FreeSWITCH installation directory (default: /usr/local/freeswitch)
#   --build-cpus N            Number of CPU cores for compilation (default: 4)
#   --yes                     Skip confirmation prompts (auto-accept)
#   --no-validation           Skip module validation after build
#   --help                    Show this help message
#
# Examples:
#   sudo ./install-all.sh --module freeswitch
#   sudo ./install-all.sh --module mod_google_transcribe
#   sudo ./install-all.sh --module all --build-cpus 8
# ============================================================================

set +e  # Handle errors manually for better tracking

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Default values
MODULE="all"
FS_PREFIX="/usr/local/freeswitch"
BUILD_CPUS=4
AUTO_YES=false
NO_VALIDATION=false
VERBOSE=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST_FILE="$(cd "${SCRIPT_DIR}/.." && pwd)/.freeswitch-install-manifest.txt"
LOG_DIR="/tmp/freeswitch-install-logs"

# Valid module names
VALID_MODULES=("all" "freeswitch" "mod_audio_fork" "mod_aws_transcribe" "mod_deepgram_transcribe" "mod_google_transcribe" "mod_google_transcribev2")

# Create log directory
mkdir -p "$LOG_DIR"

# ============================================================================
# Helper Functions
# ============================================================================

check_success() {
    local exit_code=$?
    local error_msg="$1"
    local log_file="$2"

    if [ $exit_code -ne 0 ]; then
        echo -e "${RED}✗ $error_msg${NC}"

        # Show log file location if available
        if [ -n "$log_file" ] && [ -f "$log_file" ]; then
            echo -e "${YELLOW}  ↪ Error log: $log_file${NC}"
            echo ""
            echo -e "${BOLD}Last 20 lines of error log:${NC}"
            tail -20 "$log_file" | sed 's/^/    /'
            echo ""
        fi

        exit $exit_code
    fi
}

log_step() {
    echo ""
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}$1${NC}"
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}"
}

log_substep() {
    echo -e "${CYAN}  ➜ $1${NC}"
}

log_success() {
    echo -e "${GREEN}  ✓ $1${NC}"
}

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

# Check if module should be installed
should_install_module() {
    local target="$1"
    [ "$MODULE" = "all" ] || [ "$MODULE" = "$target" ]
}

# Check if FreeSWITCH should be installed
should_install_freeswitch() {
    [ "$MODULE" = "all" ] || [ "$MODULE" = "freeswitch" ]
}

# ============================================================================
# Parse Command Line Arguments
# ============================================================================

while [[ $# -gt 0 ]]; do
    case $1 in
        --module)
            MODULE="$2"
            shift 2
            ;;
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
        --no-validation)
            NO_VALIDATION=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        --help)
            echo "FreeSWITCH Speech AI - Unified Installation Script"
            echo ""
            echo "Usage: $0 --module <module-name> [OPTIONS]"
            echo ""
            echo "Modules:"
            echo "  all                      Install FreeSWITCH + all transcription modules (default)"
            echo "  freeswitch               Install only FreeSWITCH"
            echo "  mod_audio_fork           Install only mod_audio_fork and dependencies"
            echo "  mod_aws_transcribe       Install only mod_aws_transcribe and dependencies"
            echo "  mod_deepgram_transcribe  Install only mod_deepgram_transcribe and dependencies"
            echo "  mod_google_transcribe    Install only mod_google_transcribe and dependencies"
            echo "  mod_google_transcribev2  Install only mod_google_transcribev2 and dependencies"
            echo ""
            echo "Options:"
            echo "  --freeswitch-prefix PATH  FreeSWITCH installation directory (default: /usr/local/freeswitch)"
            echo "  --build-cpus N            Number of CPU cores for compilation (default: 4)"
            echo "  --yes                     Skip confirmation prompts (auto-accept)"
            echo "  --no-validation           Skip module validation after build"
            echo "  --verbose                 Show detailed compilation output"
            echo "  --help                    Show this help message"
            echo ""
            echo "Examples:"
            echo "  sudo $0 --module freeswitch"
            echo "  sudo $0 --module mod_google_transcribe --build-cpus 8"
            echo "  sudo $0 --module all"
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Try '$0 --help' for more information."
            exit 1
            ;;
    esac
done

# Validate module name
if [[ ! " ${VALID_MODULES[@]} " =~ " ${MODULE} " ]]; then
    echo -e "${RED}Error: Invalid module '$MODULE'${NC}"
    echo "Valid modules: ${VALID_MODULES[*]}"
    exit 1
fi

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    echo "Please run: sudo $0 --module $MODULE"
    exit 1
fi

# ============================================================================
# Show Configuration
# ============================================================================

echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}FreeSWITCH Speech AI - Unified Installer${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Configuration:"
echo "  Module: $MODULE"
echo "  Build CPUs: $BUILD_CPUS"
echo "  Install Prefix: $FS_PREFIX"
if should_install_freeswitch; then
    echo "  Install FreeSWITCH: Yes"
    echo "  Copy Dialplan: Yes"
fi
if should_install_module "mod_audio_fork"; then
    echo "  Install mod_audio_fork: Yes"
fi
if should_install_module "mod_aws_transcribe"; then
    echo "  Install mod_aws_transcribe: Yes"
fi
if should_install_module "mod_deepgram_transcribe"; then
    echo "  Install mod_deepgram_transcribe: Yes"
fi
if should_install_module "mod_google_transcribe"; then
    echo "  Install mod_google_transcribe: Yes"
fi
if should_install_module "mod_google_transcribev2"; then
    echo "  Install mod_google_transcribev2: Yes"
fi
echo ""

# Confirmation prompt
if [ "$AUTO_YES" = false ]; then
    read -p "Continue with installation? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Installation cancelled."
        exit 0
    fi
fi

# Initialize manifest
if [ ! -f "$MANIFEST_FILE" ]; then
    cat > "$MANIFEST_FILE" <<EOF
# FreeSWITCH Speech AI Installation Manifest
# Created: $(date)
# Module: $MODULE
# This file tracks what was installed by installation scripts
# Format: component=status (installed|existing)

EOF
fi

# ============================================================================
# Step 1: Install System Dependencies
# ============================================================================

log_step "[Step 1/8] Installing System Dependencies"

log_substep "Updating package lists..."
apt-get update > /dev/null 2>&1
check_success "Failed to update package lists"

log_substep "Installing build tools and core dependencies..."
apt-get install -y \
    build-essential \
    cmake \
    autoconf \
    automake \
    libtool \
    libtool-bin \
    pkg-config \
    nasm \
    git \
    wget \
    ca-certificates \
    libssl-dev \
    libcurl4-openssl-dev \
    libpcre3-dev \
    libspeex1 \
    libspeexdsp-dev \
    libedit-dev \
    libtiff-dev \
    libldns-dev \
    uuid-dev \
    libopus-dev \
    libsndfile1-dev \
    libshout3-dev \
    libmpg123-dev \
    libmp3lame-dev \
    libsqlite3-dev \
    libpq-dev \
    unixodbc-dev \
    libsofia-sip-ua-dev \
    libsrtp2-dev \
    libavformat-dev \
    libswscale-dev \
    libxml2-dev \
    liblua5.2-dev \
    libgoogle-perftools-dev \
    python3 \
    python-is-python3 \
    zlib1g-dev \
    libjpeg-dev \
    > /dev/null 2>&1
check_success "Failed to install core dependencies"

log_success "Core dependencies installed"

# ============================================================================
# Step 2: Install Module-Specific Dependencies
# ============================================================================

log_step "[Step 2/8] Installing Module-Specific Dependencies"

# libwebsockets (for mod_audio_fork and mod_deepgram_transcribe)
if should_install_module "mod_audio_fork" || should_install_module "mod_deepgram_transcribe"; then
    if ! ldconfig -p | grep -q libwebsockets; then
        log_substep "Building libwebsockets 4.3.3..."
        cd /usr/local/src || exit 1

        if [ ! -d "libwebsockets" ]; then
            git clone --depth 1 -b v4.3.3 https://github.com/warmcat/libwebsockets.git > /dev/null 2>&1
            check_success "Failed to clone libwebsockets"
        fi

        cd libwebsockets && mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null 2>&1
        check_success "Failed to configure libwebsockets"

        make -j ${BUILD_CPUS} > /dev/null 2>&1
        check_success "Failed to compile libwebsockets"

        make install > /dev/null 2>&1
        check_success "Failed to install libwebsockets"

        ldconfig
        echo "libwebsockets=installed" >> "$MANIFEST_FILE"
        log_success "libwebsockets built and installed"
    else
        log_success "libwebsockets already installed"
        grep -q "^libwebsockets=" "$MANIFEST_FILE" || echo "libwebsockets=existing" >> "$MANIFEST_FILE"
    fi
fi

# AWS SDK C++ (for mod_aws_transcribe)
if should_install_module "mod_aws_transcribe"; then
    if ! ldconfig -p | grep -q aws-cpp-sdk-transcribestreaming; then
        log_substep "Building AWS SDK C++ 1.11.345 (20-30 minutes)..."
        cd /usr/local/src || exit 1

        if [ ! -d "aws-sdk-cpp" ]; then
            git clone --depth 1 -b 1.11.345 https://github.com/aws/aws-sdk-cpp.git > /dev/null 2>&1
            check_success "Failed to clone AWS SDK"
            cd aws-sdk-cpp
            git submodule update --init --recursive > /dev/null 2>&1
            check_success "Failed to initialize AWS SDK submodules"
        else
            cd aws-sdk-cpp
        fi

        mkdir -p build && cd build
        cmake .. \
            -DBUILD_ONLY="transcribestreaming" \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DBUILD_SHARED_LIBS=ON \
            -DENABLE_TESTING=OFF \
            -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull" \
            > /dev/null 2>&1
        check_success "Failed to configure AWS SDK"

        make -j ${BUILD_CPUS} > /dev/null 2>&1
        check_success "Failed to compile AWS SDK"

        make install > /dev/null 2>&1
        check_success "Failed to install AWS SDK"

        ldconfig
        echo "aws-sdk-cpp=installed" >> "$MANIFEST_FILE"
        log_success "AWS SDK built and installed"

        # Fix cJSON header conflict
        if [ -f /usr/local/include/aws/core/external/cjson/cJSON.h ]; then
            if ! grep -q '#ifndef cJSON__h' /usr/local/include/aws/core/external/cjson/cJSON.h; then
                sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' \
                    /usr/local/include/aws/core/external/cjson/cJSON.h
                echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h
                log_success "cJSON header guards added"
            fi
        fi
    else
        log_success "AWS SDK already installed"
        grep -q "^aws-sdk-cpp=" "$MANIFEST_FILE" || echo "aws-sdk-cpp=existing" >> "$MANIFEST_FILE"
    fi
fi

# gRPC (for mod_google_transcribe and mod_google_transcribev2)
if should_install_module "mod_google_transcribe" || should_install_module "mod_google_transcribev2"; then
    if ! command -v protoc &> /dev/null || ! command -v grpc_cpp_plugin &> /dev/null; then
        log_substep "Installing gRPC from system packages..."

        sed -i '/^grpc=/d' "$MANIFEST_FILE" 2>/dev/null || true

        apt-get install -y \
            libgrpc++-dev \
            libgrpc-dev \
            protobuf-compiler \
            protobuf-compiler-grpc \
            libprotobuf-dev \
            > /dev/null 2>&1
        check_success "Failed to install gRPC packages"

        command -v protoc &> /dev/null
        check_success "protoc not found after installation"

        command -v grpc_cpp_plugin &> /dev/null
        check_success "grpc_cpp_plugin not found after installation"

        echo "grpc=installed" >> "$MANIFEST_FILE"
        log_success "gRPC installed from system packages"
    else
        log_success "gRPC already installed"
        grep -q "^grpc=" "$MANIFEST_FILE" || echo "grpc=existing" >> "$MANIFEST_FILE"
    fi
fi

# Google Cloud C++ Speech library (for mod_google_transcribev2)
if should_install_module "mod_google_transcribev2"; then
    if ! ldconfig -p | grep -q libgoogle_cloud_cpp_speech; then
        log_substep "Building Google Cloud C++ Speech library v2.30.0 (10-15 minutes)..."
        cd /usr/local/src || exit 1

        # Install additional dependencies
        apt-get install -y \
            nlohmann-json3-dev \
            libcurl4-openssl-dev \
            libssl-dev \
            > /dev/null 2>&1

        if [ ! -d "google-cloud-cpp" ]; then
            git clone --depth 1 -b v2.30.0 https://github.com/googleapis/google-cloud-cpp.git > /dev/null 2>&1
            check_success "Failed to clone Google Cloud C++ SDK"
        fi

        cd google-cloud-cpp
        mkdir -p build && cd build
        cmake .. \
            -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_BUILD_TYPE=Release \
            -DGOOGLE_CLOUD_CPP_ENABLE=speech \
            -DBUILD_TESTING=OFF \
            -DGOOGLE_CLOUD_CPP_WITH_MOCKS=OFF \
            > /dev/null 2>&1
        check_success "Failed to configure Google Cloud C++ SDK"

        make -j ${BUILD_CPUS} > /dev/null 2>&1
        check_success "Failed to compile Google Cloud C++ SDK"

        make install > /dev/null 2>&1
        check_success "Failed to install Google Cloud C++ SDK"

        ldconfig
        echo "google_cloud_cpp=installed" >> "$MANIFEST_FILE"
        log_success "Google Cloud C++ Speech library built and installed"
    else
        log_success "Google Cloud C++ Speech library already installed"
        grep -q "^google_cloud_cpp=" "$MANIFEST_FILE" || echo "google_cloud_cpp=existing" >> "$MANIFEST_FILE"
    fi
fi

# ============================================================================
# Step 3: Install FreeSWITCH (if requested)
# ============================================================================

if should_install_freeswitch; then
    log_step "[Step 3/8] Installing FreeSWITCH 1.10.11"

    # Check if already installed
    if [ -d "$FS_PREFIX" ]; then
        log_substep "FreeSWITCH already exists at $FS_PREFIX"
        if [ "$AUTO_YES" = false ]; then
            read -p "Overwrite existing installation? (y/N) " -n 1 -r
            echo
            if [[ ! $REPLY =~ ^[Yy]$ ]]; then
                echo "Skipping FreeSWITCH installation"
                grep -q "^freeswitch=" "$MANIFEST_FILE" || echo "freeswitch=existing" >> "$MANIFEST_FILE"
            else
                # Proceed with installation
                # Build spandsp
                log_substep "Building spandsp 3.x (commit 0d2e6ac)..."
                cd /usr/local/src || exit 1
                if [ ! -d "spandsp" ]; then
                    log_command "spandsp clone" "${LOG_DIR}/spandsp_clone.log" \
                        git clone https://github.com/freeswitch/spandsp.git
                    check_success "Failed to clone spandsp" "${LOG_DIR}/spandsp_clone.log"
                fi
                cd spandsp
                log_command "spandsp checkout" "${LOG_DIR}/spandsp_checkout.log" \
                    git checkout 0d2e6ac
                check_success "Failed to checkout spandsp commit" "${LOG_DIR}/spandsp_checkout.log"

                log_command "spandsp bootstrap" "${LOG_DIR}/spandsp_bootstrap.log" \
                    ./bootstrap.sh
                check_success "Failed to bootstrap spandsp" "${LOG_DIR}/spandsp_bootstrap.log"

                log_command "spandsp configure" "${LOG_DIR}/spandsp_configure.log" \
                    ./configure
                check_success "Failed to configure spandsp" "${LOG_DIR}/spandsp_configure.log"

                log_command "spandsp make" "${LOG_DIR}/spandsp_make.log" \
                    make -j ${BUILD_CPUS}
                check_success "Failed to compile spandsp" "${LOG_DIR}/spandsp_make.log"

                log_command "spandsp install" "${LOG_DIR}/spandsp_install.log" \
                    make install
                check_success "Failed to install spandsp" "${LOG_DIR}/spandsp_install.log"

                ldconfig
                log_success "spandsp installed"

                # Build sofia-sip
                log_substep "Building sofia-sip 1.13.17..."
                cd /usr/local/src || exit 1
                if [ ! -d "sofia-sip" ]; then
                    log_command "sofia-sip clone" "${LOG_DIR}/sofia_clone.log" \
                        git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git
                    check_success "Failed to clone sofia-sip" "${LOG_DIR}/sofia_clone.log"
                fi
                cd sofia-sip
                log_command "sofia-sip bootstrap" "${LOG_DIR}/sofia_bootstrap.log" \
                    ./bootstrap.sh
                check_success "Failed to bootstrap sofia-sip" "${LOG_DIR}/sofia_bootstrap.log"

                log_command "sofia-sip configure" "${LOG_DIR}/sofia_configure.log" \
                    ./configure
                check_success "Failed to configure sofia-sip" "${LOG_DIR}/sofia_configure.log"

                log_command "sofia-sip make" "${LOG_DIR}/sofia_make.log" \
                    make -j ${BUILD_CPUS}
                check_success "Failed to compile sofia-sip" "${LOG_DIR}/sofia_make.log"

                log_command "sofia-sip install" "${LOG_DIR}/sofia_install.log" \
                    make install
                check_success "Failed to install sofia-sip" "${LOG_DIR}/sofia_install.log"

                ldconfig
                log_success "sofia-sip installed"

                # Build FreeSWITCH
                log_substep "Building FreeSWITCH 1.10.11 (this takes 15-20 minutes)..."
                cd /usr/local/src || exit 1
                if [ ! -d "freeswitch" ]; then
                    log_command "FreeSWITCH clone" "${LOG_DIR}/freeswitch_clone.log" \
                        git clone --depth 1 -b v1.10.11 https://github.com/signalwire/freeswitch.git
                    check_success "Failed to clone FreeSWITCH" "${LOG_DIR}/freeswitch_clone.log"
                fi
                cd freeswitch

                log_command "FreeSWITCH bootstrap" "${LOG_DIR}/freeswitch_bootstrap.log" \
                    ./bootstrap.sh -j
                check_success "Failed to bootstrap FreeSWITCH" "${LOG_DIR}/freeswitch_bootstrap.log"

                # Disable optional modules that require additional dependencies
                log_substep "Disabling optional modules..."
                sed -i 's|^endpoints/mod_verto$|#endpoints/mod_verto|' modules.conf
                sed -i 's|^endpoints/mod_skinny$|#endpoints/mod_skinny|' modules.conf
                sed -i 's|^applications/mod_signalwire$|#applications/mod_signalwire|' modules.conf
                sed -i 's|^applications/mod_av$|#applications/mod_av|' modules.conf
                sed -i 's|^languages/mod_python$|#languages/mod_python|' modules.conf
                sed -i 's|^languages/mod_python3$|#languages/mod_python3|' modules.conf
                sed -i 's|^languages/mod_java$|#languages/mod_java|' modules.conf
                sed -i 's|^languages/mod_perl$|#languages/mod_perl|' modules.conf
                sed -i 's|^languages/mod_php$|#languages/mod_php|' modules.conf
                log_success "Optional modules disabled"

                log_command "FreeSWITCH configure" "${LOG_DIR}/freeswitch_configure.log" \
                    ./configure \
                    --prefix=${FS_PREFIX} \
                    --exec-prefix=${FS_PREFIX} \
                    --bindir=${FS_PREFIX}/bin \
                    --sbindir=${FS_PREFIX}/bin \
                    --sysconfdir=${FS_PREFIX}/conf \
                    --localstatedir=${FS_PREFIX} \
                    --with-rundir=${FS_PREFIX}/run \
                    --with-logdir=${FS_PREFIX}/log \
                    --with-modinstdir=${FS_PREFIX}/lib/freeswitch/mod \
                    --enable-core-pgsql-support \
                    --enable-core-odbc-support \
                    --enable-tcmalloc \
                    --without-python \
                    --without-python3 \
                    --without-java \
                    --without-perl
                check_success "Failed to configure FreeSWITCH" "${LOG_DIR}/freeswitch_configure.log"

                log_command "FreeSWITCH make" "${LOG_DIR}/freeswitch_make.log" \
                    make -j ${BUILD_CPUS}
                check_success "Failed to compile FreeSWITCH" "${LOG_DIR}/freeswitch_make.log"

                log_command "FreeSWITCH install" "${LOG_DIR}/freeswitch_install.log" \
                    make install
                check_success "Failed to install FreeSWITCH" "${LOG_DIR}/freeswitch_install.log"

                echo "freeswitch=installed" >> "$MANIFEST_FILE"
                log_success "FreeSWITCH installed"

                # Copy dialplan
                log_substep "Copying example dialplan..."
                cp -r ${SCRIPT_DIR}/../examples/freeswitch-config/dialplan/default.xml ${FS_PREFIX}/conf/dialplan/ 2>/dev/null || true
                cp ${SCRIPT_DIR}/../examples/freeswitch-config/directory/*.xml ${FS_PREFIX}/conf/directory/default/ 2>/dev/null || true
                log_success "Dialplan copied"
            fi
        fi
    else
        # Fresh installation
        # Build spandsp
        log_substep "Building spandsp 3.x (commit 0d2e6ac)..."
        cd /usr/local/src || exit 1
        if [ ! -d "spandsp" ]; then
            log_command "spandsp clone" "${LOG_DIR}/spandsp_clone.log" \
                git clone https://github.com/freeswitch/spandsp.git
            check_success "Failed to clone spandsp" "${LOG_DIR}/spandsp_clone.log"
        fi
        cd spandsp
        log_command "spandsp checkout" "${LOG_DIR}/spandsp_checkout.log" \
            git checkout 0d2e6ac
        check_success "Failed to checkout spandsp commit" "${LOG_DIR}/spandsp_checkout.log"

        log_command "spandsp bootstrap" "${LOG_DIR}/spandsp_bootstrap.log" \
            ./bootstrap.sh
        check_success "Failed to bootstrap spandsp" "${LOG_DIR}/spandsp_bootstrap.log"

        log_command "spandsp configure" "${LOG_DIR}/spandsp_configure.log" \
            ./configure
        check_success "Failed to configure spandsp" "${LOG_DIR}/spandsp_configure.log"

        log_command "spandsp make" "${LOG_DIR}/spandsp_make.log" \
            make -j ${BUILD_CPUS}
        check_success "Failed to compile spandsp" "${LOG_DIR}/spandsp_make.log"

        log_command "spandsp install" "${LOG_DIR}/spandsp_install.log" \
            make install
        check_success "Failed to install spandsp" "${LOG_DIR}/spandsp_install.log"

        ldconfig
        log_success "spandsp installed"

        # Build sofia-sip
        log_substep "Building sofia-sip 1.13.17..."
        cd /usr/local/src || exit 1
        if [ ! -d "sofia-sip" ]; then
            log_command "sofia-sip clone" "${LOG_DIR}/sofia_clone.log" \
                git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git
            check_success "Failed to clone sofia-sip" "${LOG_DIR}/sofia_clone.log"
        fi
        cd sofia-sip
        log_command "sofia-sip bootstrap" "${LOG_DIR}/sofia_bootstrap.log" \
            ./bootstrap.sh
        check_success "Failed to bootstrap sofia-sip" "${LOG_DIR}/sofia_bootstrap.log"

        log_command "sofia-sip configure" "${LOG_DIR}/sofia_configure.log" \
            ./configure
        check_success "Failed to configure sofia-sip" "${LOG_DIR}/sofia_configure.log"

        log_command "sofia-sip make" "${LOG_DIR}/sofia_make.log" \
            make -j ${BUILD_CPUS}
        check_success "Failed to compile sofia-sip" "${LOG_DIR}/sofia_make.log"

        log_command "sofia-sip install" "${LOG_DIR}/sofia_install.log" \
            make install
        check_success "Failed to install sofia-sip" "${LOG_DIR}/sofia_install.log"

        ldconfig
        log_success "sofia-sip installed"

        # Build FreeSWITCH
        log_substep "Building FreeSWITCH 1.10.11 (this takes 15-20 minutes)..."
        cd /usr/local/src || exit 1
        if [ ! -d "freeswitch" ]; then
            log_command "FreeSWITCH clone" "${LOG_DIR}/freeswitch_clone.log" \
                git clone --depth 1 -b v1.10.11 https://github.com/signalwire/freeswitch.git
            check_success "Failed to clone FreeSWITCH" "${LOG_DIR}/freeswitch_clone.log"
        fi
        cd freeswitch

        log_command "FreeSWITCH bootstrap" "${LOG_DIR}/freeswitch_bootstrap.log" \
            ./bootstrap.sh -j
        check_success "Failed to bootstrap FreeSWITCH" "${LOG_DIR}/freeswitch_bootstrap.log"

        # Disable optional modules that require additional dependencies
        log_substep "Disabling optional modules..."
        sed -i 's|^endpoints/mod_verto$|#endpoints/mod_verto|' modules.conf
        sed -i 's|^endpoints/mod_skinny$|#endpoints/mod_skinny|' modules.conf
        sed -i 's|^applications/mod_signalwire$|#applications/mod_signalwire|' modules.conf
        sed -i 's|^applications/mod_av$|#applications/mod_av|' modules.conf
        sed -i 's|^languages/mod_python$|#languages/mod_python|' modules.conf
        sed -i 's|^languages/mod_python3$|#languages/mod_python3|' modules.conf
        sed -i 's|^languages/mod_java$|#languages/mod_java|' modules.conf
        sed -i 's|^languages/mod_perl$|#languages/mod_perl|' modules.conf
        sed -i 's|^languages/mod_php$|#languages/mod_php|' modules.conf
        log_success "Optional modules disabled"

        log_command "FreeSWITCH configure" "${LOG_DIR}/freeswitch_configure.log" \
            ./configure \
            --prefix=${FS_PREFIX} \
            --exec-prefix=${FS_PREFIX} \
            --bindir=${FS_PREFIX}/bin \
            --sbindir=${FS_PREFIX}/bin \
            --sysconfdir=${FS_PREFIX}/conf \
            --localstatedir=${FS_PREFIX} \
            --with-rundir=${FS_PREFIX}/run \
            --with-logdir=${FS_PREFIX}/log \
            --with-modinstdir=${FS_PREFIX}/lib/freeswitch/mod \
            --enable-core-pgsql-support \
            --enable-core-odbc-support \
            --enable-tcmalloc \
            --without-python \
            --without-python3 \
            --without-java \
            --without-perl
        check_success "Failed to configure FreeSWITCH" "${LOG_DIR}/freeswitch_configure.log"

        log_command "FreeSWITCH make" "${LOG_DIR}/freeswitch_make.log" \
            make -j ${BUILD_CPUS}
        check_success "Failed to compile FreeSWITCH" "${LOG_DIR}/freeswitch_make.log"

        log_command "FreeSWITCH install" "${LOG_DIR}/freeswitch_install.log" \
            make install
        check_success "Failed to install FreeSWITCH" "${LOG_DIR}/freeswitch_install.log"

        echo "freeswitch=installed" >> "$MANIFEST_FILE"
        log_success "FreeSWITCH installed"

        # Copy dialplan
        log_substep "Copying example dialplan..."
        cp -r ${SCRIPT_DIR}/../examples/freeswitch-config/dialplan/default.xml ${FS_PREFIX}/conf/dialplan/ 2>/dev/null || true
        cp ${SCRIPT_DIR}/../examples/freeswitch-config/directory/*.xml ${FS_PREFIX}/conf/directory/default/ 2>/dev/null || true
        log_success "Dialplan copied"
    fi
else
    log_step "[Step 3/8] Skipping FreeSWITCH Installation"
    log_substep "Module-only installation (FreeSWITCH not selected)"

    # Verify FreeSWITCH exists
    if [ ! -d "$FS_PREFIX" ]; then
        echo -e "${RED}Error: FreeSWITCH not found at $FS_PREFIX${NC}"
        echo "Please install FreeSWITCH first or run: sudo $0 --module freeswitch"
        exit 1
    fi
    log_success "FreeSWITCH found at $FS_PREFIX"
fi

# ============================================================================
# Step 4: Build mod_audio_fork
# ============================================================================

if should_install_module "mod_audio_fork"; then
    log_step "[Step 4/8] Building mod_audio_fork"

    cd ${SCRIPT_DIR}/../modules/mod_audio_fork || exit 1

    log_substep "Compiling mod_audio_fork.c..."
    log_command "mod_audio_fork.c" "${LOG_DIR}/mod_audio_fork_c.log" \
        gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_audio_fork.c
    check_success "Failed to compile mod_audio_fork.c" "${LOG_DIR}/mod_audio_fork_c.log"

    log_substep "Compiling C++ sources..."
    log_command "mod_audio_fork C++" "${LOG_DIR}/mod_audio_fork_cpp.log" \
        g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include lws_glue.cpp audio_pipe.cpp parser.cpp
    check_success "Failed to compile mod_audio_fork C++ sources" "${LOG_DIR}/mod_audio_fork_cpp.log"

    mkdir -p ${FS_PREFIX}/lib/freeswitch/mod

    log_substep "Linking mod_audio_fork.so..."
    log_command "mod_audio_fork link" "${LOG_DIR}/mod_audio_fork_link.log" \
        g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so *.o -lwebsockets -lpthread -lssl -lcrypto
    check_success "Failed to link mod_audio_fork" "${LOG_DIR}/mod_audio_fork_link.log"

    log_success "mod_audio_fork built and installed"
else
    log_step "[Step 4/8] Skipping mod_audio_fork"
fi

# ============================================================================
# Step 5: Build mod_aws_transcribe
# ============================================================================

if should_install_module "mod_aws_transcribe"; then
    log_step "[Step 5/8] Building mod_aws_transcribe"

    cd ${SCRIPT_DIR}/../modules/mod_aws_transcribe || exit 1

    log_substep "Compiling mod_aws_transcribe.c..."
    log_command "mod_aws_transcribe.c" "${LOG_DIR}/mod_aws_transcribe_c.log" \
        gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch mod_aws_transcribe.c
    check_success "Failed to compile mod_aws_transcribe.c" "${LOG_DIR}/mod_aws_transcribe_c.log"

    log_substep "Compiling C++ sources..."
    log_command "mod_aws_transcribe C++" "${LOG_DIR}/mod_aws_transcribe_cpp.log" \
        g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include aws_transcribe_glue.cpp
    check_success "Failed to compile mod_aws_transcribe C++ sources" "${LOG_DIR}/mod_aws_transcribe_cpp.log"

    log_substep "Linking mod_aws_transcribe.so..."
    log_command "mod_aws_transcribe link" "${LOG_DIR}/mod_aws_transcribe_link.log" \
        g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so \
            mod_aws_transcribe.o aws_transcribe_glue.o \
            -L/usr/local/lib -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core \
            -laws-c-event-stream -laws-checksums -laws-c-common \
            -lpthread -lcurl -lssl -lcrypto -lz
    check_success "Failed to link mod_aws_transcribe" "${LOG_DIR}/mod_aws_transcribe_link.log"

    log_success "mod_aws_transcribe built and installed"
else
    log_step "[Step 5/8] Skipping mod_aws_transcribe"
fi

# ============================================================================
# Step 6: Build mod_deepgram_transcribe
# ============================================================================

if should_install_module "mod_deepgram_transcribe"; then
    log_step "[Step 6/8] Building mod_deepgram_transcribe"

    cd ${SCRIPT_DIR}/../modules/mod_deepgram_transcribe || exit 1

    log_substep "Compiling mod_deepgram_transcribe.c..."
    log_command "mod_deepgram_transcribe.c" "${LOG_DIR}/mod_deepgram_transcribe_c.log" \
        gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_deepgram_transcribe.c
    check_success "Failed to compile mod_deepgram_transcribe.c" "${LOG_DIR}/mod_deepgram_transcribe_c.log"

    log_substep "Compiling C++ sources..."
    log_command "mod_deepgram_transcribe C++" "${LOG_DIR}/mod_deepgram_transcribe_cpp.log" \
        g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp
    check_success "Failed to compile mod_deepgram_transcribe C++ sources" "${LOG_DIR}/mod_deepgram_transcribe_cpp.log"

    log_substep "Linking mod_deepgram_transcribe.so..."
    log_command "mod_deepgram_transcribe link" "${LOG_DIR}/mod_deepgram_transcribe_link.log" \
        g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
            mod_deepgram_transcribe.o dg_transcribe_glue.o audio_pipe.o parser.o \
            -lwebsockets -lpthread -lssl -lcrypto
    check_success "Failed to link mod_deepgram_transcribe" "${LOG_DIR}/mod_deepgram_transcribe_link.log"

    log_success "mod_deepgram_transcribe built and installed"
else
    log_step "[Step 6/8] Skipping mod_deepgram_transcribe"
fi

# ============================================================================
# Step 7: Build mod_google_transcribe
# ============================================================================

if should_install_module "mod_google_transcribe"; then
    log_step "[Step 7/8] Building mod_google_transcribe"

    cd ${SCRIPT_DIR}/../modules/mod_google_transcribe || exit 1

    # Generate protobuf files if needed
    if [ ! -f "speech.pb.h" ] || [ "speech.proto" -nt "speech.pb.h" ]; then
        log_substep "Generating protobuf files..."
        log_command "protobuf generation" "${LOG_DIR}/mod_google_transcribe_proto.log" \
            protoc --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) speech.proto
        check_success "Failed to generate protobuf files" "${LOG_DIR}/mod_google_transcribe_proto.log"
    fi

    # Build using Makefile
    log_substep "Cleaning previous build..."
    make clean > /dev/null 2>&1

    log_substep "Building mod_google_transcribe..."
    log_command "mod_google_transcribe build" "${LOG_DIR}/mod_google_transcribe_make.log" \
        make -j ${BUILD_CPUS}
    check_success "Failed to build mod_google_transcribe" "${LOG_DIR}/mod_google_transcribe_make.log"

    log_substep "Installing mod_google_transcribe..."
    log_command "mod_google_transcribe install" "${LOG_DIR}/mod_google_transcribe_install.log" \
        make install
    check_success "Failed to install mod_google_transcribe" "${LOG_DIR}/mod_google_transcribe_install.log"

    log_success "mod_google_transcribe built and installed"
else
    log_step "[Step 7/8] Skipping mod_google_transcribe"
fi

# ============================================================================
# Step 8: Build mod_google_transcribev2
# ============================================================================

if should_install_module "mod_google_transcribev2"; then
    log_step "[Step 8/8] Building mod_google_transcribev2"

    cd ${SCRIPT_DIR}/../modules/mod_google_transcribev2 || exit 1

    # Build using Makefile
    log_substep "Cleaning previous build..."
    make clean > /dev/null 2>&1

    log_substep "Building mod_google_transcribev2..."
    log_command "mod_google_transcribev2 build" "${LOG_DIR}/mod_google_transcribev2_make.log" \
        make -j ${BUILD_CPUS} FS_PREFIX=${FS_PREFIX}
    check_success "Failed to build mod_google_transcribev2" "${LOG_DIR}/mod_google_transcribev2_make.log"

    log_substep "Installing mod_google_transcribev2..."
    log_command "mod_google_transcribev2 install" "${LOG_DIR}/mod_google_transcribev2_install.log" \
        make install FS_PREFIX=${FS_PREFIX}
    check_success "Failed to install mod_google_transcribev2" "${LOG_DIR}/mod_google_transcribev2_install.log"

    log_success "mod_google_transcribev2 built and installed"
else
    log_step "[Step 8/8] Skipping mod_google_transcribev2"
fi

# ============================================================================
# Configure FreeSWITCH modules.conf.xml
# ============================================================================

if [ "$MODULE" != "freeswitch" ]; then
    log_step "Configuring FreeSWITCH modules.conf.xml"

    MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
    if [ -f "$MODULES_CONF" ]; then
        if ! grep -q "mod_audio_fork" "$MODULES_CONF" 2>/dev/null; then
            sed -i '/<\/modules>/i \    <!-- Speech Transcription Modules -->' "$MODULES_CONF"
        fi

        if should_install_module "mod_audio_fork" && ! grep -q "mod_audio_fork" "$MODULES_CONF"; then
            sed -i '/<\/modules>/i \    <load module="mod_audio_fork"/>' "$MODULES_CONF"
        fi
        if should_install_module "mod_aws_transcribe" && ! grep -q "mod_aws_transcribe" "$MODULES_CONF"; then
            sed -i '/<\/modules>/i \    <load module="mod_aws_transcribe"/>' "$MODULES_CONF"
        fi
        if should_install_module "mod_deepgram_transcribe" && ! grep -q "mod_deepgram_transcribe" "$MODULES_CONF"; then
            sed -i '/<\/modules>/i \    <load module="mod_deepgram_transcribe"/>' "$MODULES_CONF"
        fi
        if should_install_module "mod_google_transcribe" && ! grep -q "mod_google_transcribe" "$MODULES_CONF"; then
            sed -i '/<\/modules>/i \    <load module="mod_google_transcribe"/>' "$MODULES_CONF"
        fi
        if should_install_module "mod_google_transcribev2" && ! grep -q "mod_google_transcribev2" "$MODULES_CONF"; then
            sed -i '/<\/modules>/i \    <load module="mod_google_transcribev2"/>' "$MODULES_CONF"
        fi

        log_success "modules.conf.xml configured"
    fi
fi

# ============================================================================
# Validation
# ============================================================================

if [ "$NO_VALIDATION" = false ] && [ "$MODULE" != "freeswitch" ]; then
    log_step "Validating Installation"

    if should_install_module "mod_audio_fork"; then
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so" ]; then
            log_success "mod_audio_fork.so exists"
        else
            echo -e "${RED}✗ mod_audio_fork.so NOT FOUND${NC}"
        fi
    fi

    if should_install_module "mod_aws_transcribe"; then
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so" ]; then
            log_success "mod_aws_transcribe.so exists"
        else
            echo -e "${RED}✗ mod_aws_transcribe.so NOT FOUND${NC}"
        fi
    fi

    if should_install_module "mod_deepgram_transcribe"; then
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so" ]; then
            log_success "mod_deepgram_transcribe.so exists"
        else
            echo -e "${RED}✗ mod_deepgram_transcribe.so NOT FOUND${NC}"
        fi
    fi

    if should_install_module "mod_google_transcribe"; then
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so" ]; then
            log_success "mod_google_transcribe.so exists"
            # Ensure no duplicate binaries
            if [ -f "${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so.1" ]; then
                echo -e "${YELLOW}⚠ Found duplicate: mod_google_transcribe.so.1${NC}"
            fi
        else
            echo -e "${RED}✗ mod_google_transcribe.so NOT FOUND${NC}"
        fi
    fi

    if should_install_module "mod_google_transcribev2"; then
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribev2.so" ]; then
            log_success "mod_google_transcribev2.so exists"
        else
            echo -e "${RED}✗ mod_google_transcribev2.so NOT FOUND${NC}"
        fi
    fi
fi

# ============================================================================
# Installation Complete
# ============================================================================

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Installation Complete!${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Module: $MODULE"
echo "Install Prefix: $FS_PREFIX"
echo "Build Logs: $LOG_DIR"
echo ""
echo "Next steps:"
if should_install_freeswitch; then
    echo "  1. Start FreeSWITCH: systemctl start freeswitch"
    echo "  2. Or run directly: ${FS_PREFIX}/bin/freeswitch -nc"
fi
if [ "$MODULE" != "freeswitch" ]; then
    echo "  - Reload FreeSWITCH: ${FS_PREFIX}/bin/fs_cli -x 'reload mod_sofia'"
    echo "  - Verify modules: ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram|google'"
fi
echo ""
echo "Logs are available at: $LOG_DIR"
echo "To view a specific log: cat $LOG_DIR/<module>_<step>.log"
echo ""
