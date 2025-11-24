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

# Disable automatic exit on error - we'll handle errors manually for better tracking
set +e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
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

# ============================================================================
# Installation Step Tracking
# ============================================================================
declare -a COMPLETED_STEPS=()
declare -a ALL_STEPS=(
    "Install system dependencies"
    "Build libwebsockets 4.3.3"
    "Build AWS SDK C++ 1.11.345"
    "Build spandsp 3.x from source"
    "Build sofia-sip 1.13.17 from source"
    "Install FreeSWITCH 1.10.11"
    "Build mod_audio_fork"
    "Build mod_deepgram_transcribe"
    "Build mod_aws_transcribe"
    "Configure FreeSWITCH and modules"
    "Validate installation"
)
CURRENT_STEP=0
CURRENT_SUBSTEP=""

# Function to show progress
show_progress() {
    local step_num=$1
    local total_steps=${#ALL_STEPS[@]}
    local description=$2
    echo ""
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BOLD}${CYAN}[Step ${step_num}/${total_steps}] ${description}${NC}"
    echo -e "${BOLD}${BLUE}═══════════════════════════════════════════════════════════════${NC}"
}

# Function to log substep
log_substep() {
    CURRENT_SUBSTEP="$1"
    echo -e "${CYAN}  ➜ ${1}${NC}"
}

# Function to mark step as complete
complete_step() {
    local step_description="$1"
    COMPLETED_STEPS+=("$step_description")
    echo -e "${GREEN}  ✓ ${step_description} completed${NC}"
}

# Function to handle errors
handle_error() {
    local exit_code=$1
    local error_msg="$2"
    local failed_command="$3"
    local line_number="$4"

    echo ""
    echo -e "${RED}╔══════════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║                    INSTALLATION FAILED                       ║${NC}"
    echo -e "${RED}╚══════════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${BOLD}Error Details:${NC}"
    echo -e "  ${RED}✗${NC} ${error_msg}"
    echo -e "  ${RED}✗${NC} Exit code: ${exit_code}"
    if [ -n "$failed_command" ]; then
        echo -e "  ${RED}✗${NC} Failed command: ${failed_command}"
    fi
    if [ -n "$line_number" ]; then
        echo -e "  ${RED}✗${NC} Script line: ${line_number}"
    fi
    if [ -n "$CURRENT_SUBSTEP" ]; then
        echo -e "  ${RED}✗${NC} Failed substep: ${CURRENT_SUBSTEP}"
    fi
    echo ""

    # Show completed steps
    if [ ${#COMPLETED_STEPS[@]} -gt 0 ]; then
        echo -e "${BOLD}Completed Steps (before failure):${NC}"
        for step in "${COMPLETED_STEPS[@]}"; do
            echo -e "  ${GREEN}✓${NC} ${step}"
        done
        echo ""
    fi

    # Show what failed
    if [ $CURRENT_STEP -gt 0 ] && [ $CURRENT_STEP -le ${#ALL_STEPS[@]} ]; then
        local failed_step="${ALL_STEPS[$((CURRENT_STEP-1))]}"
        echo -e "${BOLD}Failed Step:${NC}"
        echo -e "  ${RED}✗${NC} Step ${CURRENT_STEP}/${#ALL_STEPS[@]}: ${failed_step}"
        echo ""
    fi

    # Show what remains
    if [ $CURRENT_STEP -lt ${#ALL_STEPS[@]} ]; then
        echo -e "${BOLD}Remaining Steps (not attempted):${NC}"
        for ((i=$CURRENT_STEP; i<${#ALL_STEPS[@]}; i++)); do
            echo -e "  ${YELLOW}⊗${NC} Step $((i+1))/${#ALL_STEPS[@]}: ${ALL_STEPS[$i]}"
        done
        echo ""
    fi

    echo -e "${YELLOW}Troubleshooting:${NC}"
    echo -e "  1. Check the error message above for details"
    echo -e "  2. Review the installation manifest: ${MANIFEST_FILE}"
    echo -e "  3. Check system logs: /var/log/syslog or journalctl"
    echo -e "  4. Run cleanup script and try again: sudo ./cleanup-all.sh"
    echo ""

    exit $exit_code
}

# Function to check command success
check_success() {
    local exit_code=$?
    local error_msg="$1"
    local command="$2"

    if [ $exit_code -ne 0 ]; then
        handle_error $exit_code "$error_msg" "$command" "${BASH_LINENO[0]}"
    fi
}

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
CURRENT_STEP=1
show_progress $CURRENT_STEP "Install System Dependencies"

log_substep "Updating package lists..."
apt-get update > /dev/null 2>&1
check_success "Failed to update package lists (apt-get update)" "apt-get update"

log_substep "Installing build tools and core dependencies..."
# All dependencies from Dockerfile.freeswitch-base (lines 33-84)
apt-get install -y \
    build-essential \
    git \
    cmake \
    ca-certificates \
    wget \
    autoconf \
    automake \
    libtool \
    libtool-bin \
    pkg-config \
    nasm \
    python3 \
    python-is-python3 \
    libcurl4-openssl-dev \
    libssl-dev \
    uuid-dev \
    zlib1g-dev \
    libpulse-dev \
    libspeexdsp-dev \
    libpcre3-dev \
    libspeex1 \
    libspeex-dev \
    libedit-dev \
    libsqlite3-dev \
    libldns-dev \
    libtiff-dev \
    libopus-dev \
    libsndfile1-dev \
    libshout3-dev \
    libmpg123-dev \
    libmp3lame-dev \
    libpq-dev \
    unixodbc-dev \
    libsrtp2-dev \
    libavformat-dev \
    libswscale-dev \
    libxml2-dev \
    liblua5.2-dev \
    libgoogle-perftools-dev \
    libjpeg-dev 2>&1 | grep -E "(error|E:|unable to locate)" || true
check_success "Failed to install system dependencies" "apt-get install"

# Verify critical packages are installed
log_substep "Verifying critical packages are installed..."
MISSING_PACKAGES=()
for pkg in build-essential git cmake libssl-dev libcurl4-openssl-dev unixodbc-dev libsqlite3-dev; do
    if ! dpkg -l | grep -q "^ii.*${pkg}"; then
        MISSING_PACKAGES+=("${pkg}")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -gt 0 ]; then
    echo -e "${RED}✗ Missing critical packages: ${MISSING_PACKAGES[*]}${NC}"
    handle_error 1 "Critical packages not installed: ${MISSING_PACKAGES[*]}" "package verification"
fi
echo -e "  ${GREEN}✓${NC} All critical packages verified"

complete_step "System dependencies"

# Initialize installation manifest
log_substep "Initializing installation manifest..."
echo "# FreeSWITCH Speech AI Installation Manifest" > "$MANIFEST_FILE"
echo "# Created: $(date)" >> "$MANIFEST_FILE"
echo "# This file tracks what was installed by this script" >> "$MANIFEST_FILE"
echo "# Format: component=status (installed|existing)" >> "$MANIFEST_FILE"
echo "" >> "$MANIFEST_FILE"

# ============================================================================
# Step 2: Build libwebsockets (for mod_audio_fork & mod_deepgram_transcribe)
# ============================================================================
CURRENT_STEP=2
show_progress $CURRENT_STEP "Build libwebsockets 4.3.3"

# Check if libwebsockets already exists
if ldconfig -p | grep -q libwebsockets; then
    log_substep "libwebsockets already installed - using existing version"
    echo "libwebsockets=existing" >> "$MANIFEST_FILE"
    complete_step "libwebsockets (existing)"
else
    log_substep "Cloning libwebsockets v4.3.3 from GitHub..."
    cd /usr/local/src
    check_success "Failed to change directory to /usr/local/src" "cd /usr/local/src"

    if [ ! -d "libwebsockets" ]; then
        git clone --depth 1 -b v4.3.3 https://github.com/warmcat/libwebsockets.git > /dev/null 2>&1
        check_success "Failed to clone libwebsockets repository" "git clone libwebsockets"
    fi

    log_substep "Configuring libwebsockets with CMake..."
    cd libwebsockets
    check_success "Failed to enter libwebsockets directory" "cd libwebsockets"

    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo > /dev/null 2>&1
    check_success "Failed to configure libwebsockets" "cmake"

    log_substep "Compiling libwebsockets (using ${BUILD_CPUS} CPU cores)..."
    make -j ${BUILD_CPUS} > /dev/null 2>&1
    check_success "Failed to compile libwebsockets" "make -j ${BUILD_CPUS}"

    log_substep "Installing libwebsockets..."
    make install > /dev/null 2>&1
    check_success "Failed to install libwebsockets" "make install"

    ldconfig

    echo "libwebsockets=installed" >> "$MANIFEST_FILE"
    complete_step "libwebsockets 4.3.3 built and installed"
fi

# ============================================================================
# Step 3: Build AWS SDK C++ (for mod_aws_transcribe)
# ============================================================================
CURRENT_STEP=3
show_progress $CURRENT_STEP "Build AWS SDK C++ 1.11.345"
echo -e "${YELLOW}  Note: This step will take 20-30 minutes...${NC}"

# Check if AWS SDK already exists
if ldconfig -p | grep -q aws-cpp-sdk-transcribestreaming; then
    log_substep "AWS SDK C++ already installed - using existing version"
    echo "aws-sdk-cpp=existing" >> "$MANIFEST_FILE"
    complete_step "AWS SDK C++ (existing)"
else
    log_substep "Cloning AWS SDK C++ v1.11.345 from GitHub..."
    cd /usr/local/src
    check_success "Failed to change directory to /usr/local/src" "cd /usr/local/src"

    if [ ! -d "aws-sdk-cpp" ]; then
        git clone --depth 1 -b 1.11.345 https://github.com/aws/aws-sdk-cpp.git > /dev/null 2>&1
        check_success "Failed to clone AWS SDK repository" "git clone aws-sdk-cpp"

        log_substep "Initializing AWS SDK submodules (this may take a few minutes)..."
        cd aws-sdk-cpp
        git submodule update --init --recursive > /dev/null 2>&1
        check_success "Failed to initialize AWS SDK submodules" "git submodule update"
    else
        cd aws-sdk-cpp
    fi

    log_substep "Configuring AWS SDK C++ with CMake (transcribestreaming only)..."
    mkdir -p build && cd build
    cmake .. \
        -DBUILD_ONLY="transcribestreaming" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_SHARED_LIBS=ON \
        -DENABLE_TESTING=OFF \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull -Wno-error=deprecated-declarations -Wno-error=uninitialized -Wno-error=maybe-uninitialized" > /dev/null 2>&1
    check_success "Failed to configure AWS SDK C++" "cmake"

    log_substep "Compiling AWS SDK C++ (using ${BUILD_CPUS} CPU cores, 20-30 min)..."
    make -j ${BUILD_CPUS} > /dev/null 2>&1
    check_success "Failed to compile AWS SDK C++" "make -j ${BUILD_CPUS}"

    log_substep "Installing AWS SDK C++..."
    make install > /dev/null 2>&1
    check_success "Failed to install AWS SDK C++" "make install"

    ldconfig

    echo "aws-sdk-cpp=installed" >> "$MANIFEST_FILE"
    complete_step "AWS SDK C++ 1.11.345 built and installed"
fi

# Fix cJSON header conflict between AWS SDK and FreeSWITCH
log_substep "Fixing cJSON header conflict..."
if [ -f /usr/local/include/aws/core/external/cjson/cJSON.h ]; then
    if ! grep -q '#ifndef cJSON__h' /usr/local/include/aws/core/external/cjson/cJSON.h; then
        sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' \
            /usr/local/include/aws/core/external/cjson/cJSON.h
        echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h
        echo -e "  ${GREEN}✓${NC} cJSON header guards added"
    else
        echo -e "  ${GREEN}✓${NC} cJSON header guards already applied"
    fi
else
    echo -e "  ${YELLOW}⚠${NC}  WARNING: AWS SDK cJSON header not found at expected location"
fi

# ============================================================================
# Step 4: Build spandsp 3.x from source
# ============================================================================
CURRENT_STEP=4
show_progress $CURRENT_STEP "Build spandsp 3.x from source"

cd /usr/local/src
check_success "Failed to change directory to /usr/local/src" "cd /usr/local/src"

if [ ! -d "spandsp" ]; then
    log_substep "Cloning spandsp from FreeSWITCH GitHub..."
    git clone https://github.com/freeswitch/spandsp.git > /dev/null 2>&1
    check_success "Failed to clone spandsp repository" "git clone spandsp"

    cd spandsp
    log_substep "Checking out spandsp commit 0d2e6ac..."
    git checkout 0d2e6ac > /dev/null 2>&1
    check_success "Failed to checkout spandsp version" "git checkout 0d2e6ac"

    log_substep "Bootstrapping spandsp..."
    ./bootstrap.sh > /dev/null 2>&1
    check_success "Failed to bootstrap spandsp" "./bootstrap.sh"

    log_substep "Configuring spandsp..."
    ./configure > /dev/null 2>&1
    check_success "Failed to configure spandsp" "./configure"

    log_substep "Compiling spandsp (using ${BUILD_CPUS} CPU cores)..."
    make -j ${BUILD_CPUS} > /dev/null 2>&1
    check_success "Failed to compile spandsp" "make -j ${BUILD_CPUS}"

    log_substep "Installing spandsp..."
    make install > /dev/null 2>&1
    check_success "Failed to install spandsp" "make install"

    ldconfig
    echo "libspandsp=installed" >> "$MANIFEST_FILE"
    complete_step "spandsp 3.x built and installed"
else
    log_substep "spandsp source directory already exists - skipping build"
    complete_step "spandsp (already built)"
fi

# ============================================================================
# Step 5: Build sofia-sip 1.13.17 from source
# ============================================================================
CURRENT_STEP=5
show_progress $CURRENT_STEP "Build sofia-sip 1.13.17 from source"

cd /usr/local/src
check_success "Failed to change directory to /usr/local/src" "cd /usr/local/src"

if [ ! -d "sofia-sip" ]; then
    log_substep "Cloning sofia-sip v1.13.17 from FreeSWITCH GitHub..."
    git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git > /dev/null 2>&1
    check_success "Failed to clone sofia-sip repository" "git clone sofia-sip"

    cd sofia-sip
    log_substep "Bootstrapping sofia-sip..."
    ./bootstrap.sh > /dev/null 2>&1
    check_success "Failed to bootstrap sofia-sip" "./bootstrap.sh"

    log_substep "Configuring sofia-sip..."
    ./configure > /dev/null 2>&1
    check_success "Failed to configure sofia-sip" "./configure"

    log_substep "Compiling sofia-sip (using ${BUILD_CPUS} CPU cores)..."
    make -j ${BUILD_CPUS} > /dev/null 2>&1
    check_success "Failed to compile sofia-sip" "make -j ${BUILD_CPUS}"

    log_substep "Installing sofia-sip..."
    make install > /dev/null 2>&1
    check_success "Failed to install sofia-sip" "make install"

    ldconfig
    echo "libsofia-sip=installed" >> "$MANIFEST_FILE"
    complete_step "sofia-sip 1.13.17 built and installed"
else
    log_substep "sofia-sip source directory already exists - skipping build"
    complete_step "sofia-sip (already built)"
fi

# ============================================================================
# Step 6: Install FreeSWITCH 1.10.11 (if not skipped)
# ============================================================================
CURRENT_STEP=6
if [ "$SKIP_FREESWITCH" = false ]; then
    show_progress $CURRENT_STEP "Install FreeSWITCH 1.10.11"
    echo -e "${YELLOW}  Note: This step will take 15-20 minutes...${NC}"
    echo "freeswitch=installed" >> "$MANIFEST_FILE"

    cd /usr/local/src
    check_success "Failed to change directory to /usr/local/src" "cd /usr/local/src"

    if [ ! -d "freeswitch" ]; then
        log_substep "Cloning FreeSWITCH v1.10.11 from GitHub..."
        git clone --depth 1 -b v1.10.11 https://github.com/signalwire/freeswitch.git > /dev/null 2>&1
        check_success "Failed to clone FreeSWITCH repository" "git clone freeswitch"
    fi

    cd freeswitch
    check_success "Failed to enter FreeSWITCH directory" "cd freeswitch"

    log_substep "Bootstrapping FreeSWITCH..."
    ./bootstrap.sh -j
    check_success "Failed to bootstrap FreeSWITCH" "./bootstrap.sh"

    log_substep "Ensuring critical modules are enabled (mod_event_socket)..."
    grep -q "^event_handlers/mod_event_socket$" modules.conf || echo "event_handlers/mod_event_socket" >> modules.conf
    echo -e "  ${GREEN}✓${NC} mod_event_socket enabled (required for fs_cli)"

    log_substep "Disabling optional modules (matching Dockerfile approach)..."
    sed -i 's/^endpoints\/mod_verto$/#&/' modules.conf
    sed -i 's/^endpoints\/mod_rtc$/#&/' modules.conf
    sed -i 's/^endpoints\/mod_skinny$/#&/' modules.conf
    sed -i 's/^applications\/mod_signalwire$/#&/' modules.conf
    sed -i 's/^applications\/mod_av$/#&/' modules.conf
    sed -i 's/^languages\/mod_python$/#&/' modules.conf
    sed -i 's/^languages\/mod_python3$/#&/' modules.conf
    sed -i 's/^languages\/mod_java$/#&/' modules.conf
    sed -i 's/^languages\/mod_perl$/#&/' modules.conf
    sed -i 's/^languages\/mod_php$/#&/' modules.conf
    echo -e "  ${GREEN}✓${NC} Disabled optional modules (verto, rtc, skinny, signalwire, av, python, java, perl, php)"

    log_substep "Configuring FreeSWITCH (prefix: ${FS_PREFIX})..."
    ./configure --prefix=${FS_PREFIX} \
        --enable-core-pgsql-support \
        --enable-core-odbc-support \
        --enable-tcmalloc \
        --without-python \
        --without-python3 \
        --without-java \
        --without-perl
    check_success "Failed to configure FreeSWITCH" "./configure"

    log_substep "Compiling FreeSWITCH (using ${BUILD_CPUS} CPU cores, 15-20 min)..."
    make -j ${BUILD_CPUS}
    check_success "Failed to compile FreeSWITCH" "make -j ${BUILD_CPUS}"

    log_substep "Installing FreeSWITCH..."
    make install
    check_success "Failed to install FreeSWITCH" "make install"

    log_substep "Installing FreeSWITCH sounds and music on hold..."
    make cd-sounds-install cd-moh-install
    check_success "Failed to install FreeSWITCH sounds" "make cd-sounds-install"

    log_substep "Installing sample configuration (vanilla)..."
    mkdir -p ${FS_PREFIX}/conf
    cp -r /usr/local/src/freeswitch/conf/vanilla/* ${FS_PREFIX}/conf/
    check_success "Failed to copy vanilla configuration" "cp vanilla config"
    echo -e "  ${GREEN}✓${NC} Sample configuration installed"

    log_substep "Configuring log directory in switch.conf.xml..."
    # Ensure FreeSWITCH logs ONLY to ${FS_PREFIX}/log
    sed -i "s|<param name=\"log-directory\" value=\".*\"/>|<param name=\"log-directory\" value=\"${FS_PREFIX}/log\"/>|" \
        ${FS_PREFIX}/conf/autoload_configs/switch.conf.xml
    sed -i "s|<param name=\"log-file\" value=\".*\"/>|<param name=\"log-file\" value=\"freeswitch.log\"/>|" \
        ${FS_PREFIX}/conf/autoload_configs/switch.conf.xml
    echo -e "  ${GREEN}✓${NC} Log directory configured: ${FS_PREFIX}/log/freeswitch.log"

    log_substep "Configuring Event Socket for IPv4 binding..."
    cat > ${FS_PREFIX}/conf/autoload_configs/event_socket.conf.xml <<'EOF'
<configuration name="event_socket.conf" description="Socket Client">
  <settings>
    <param name="nat-map" value="false"/>
    <!-- Bind to all IPv4 interfaces for fs_cli access -->
    <param name="listen-ip" value="0.0.0.0"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
    <!-- Uncomment to restrict access to loopback only -->
    <!--<param name="apply-inbound-acl" value="loopback.auto"/>-->
    <!--<param name="stop-on-bind-error" value="true"/>-->
  </settings>
</configuration>
EOF
    echo -e "  ${GREEN}✓${NC} Event Socket configured for IPv4 (0.0.0.0:8021)"

    log_substep "Creating log and db directories..."
    mkdir -p ${FS_PREFIX}/log ${FS_PREFIX}/db
    echo -e "  ${GREEN}✓${NC} Log and db directories created"

    log_substep "Creating FreeSWITCH group and user..."
    # Create freeswitch group if it doesn't exist
    getent group freeswitch > /dev/null 2>&1 || groupadd -r freeswitch
    # Create freeswitch user if it doesn't exist (with freeswitch group)
    id -u freeswitch &>/dev/null || useradd -r -g freeswitch -s /bin/false -c "FreeSWITCH" freeswitch
    echo -e "  ${GREEN}✓${NC} FreeSWITCH user and group created"

    log_substep "Setting ownership and permissions..."
    chown -R freeswitch:freeswitch ${FS_PREFIX}
    echo -e "  ${GREEN}✓${NC} Ownership set to freeswitch:freeswitch"

    complete_step "FreeSWITCH 1.10.11 built and installed"
else
    show_progress $CURRENT_STEP "Skipping FreeSWITCH installation (--skip-freeswitch)"
    echo -e "${YELLOW}  FreeSWITCH installation skipped as requested${NC}"
fi

# ============================================================================
# Step 7: Build mod_audio_fork
# ============================================================================
CURRENT_STEP=7
show_progress $CURRENT_STEP "Build mod_audio_fork"

cd ${SCRIPT_DIR}/../modules/mod_audio_fork
check_success "Failed to change directory to mod_audio_fork" "cd modules/mod_audio_fork"

log_substep "Compiling mod_audio_fork.c..."
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    mod_audio_fork.c 2>&1 | tee /tmp/mod_audio_fork_gcc.log > /dev/null
check_success "Failed to compile mod_audio_fork.c (check /tmp/mod_audio_fork_gcc.log)" "gcc mod_audio_fork.c"

log_substep "Compiling C++ files (lws_glue.cpp, audio_pipe.cpp, parser.cpp)..."
g++ -fPIC -c -std=c++11 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    lws_glue.cpp audio_pipe.cpp parser.cpp 2>&1 | tee /tmp/mod_audio_fork_g++.log > /dev/null
check_success "Failed to compile C++ files (check /tmp/mod_audio_fork_g++.log)" "g++ C++ files"

log_substep "Linking mod_audio_fork.so..."
mkdir -p ${FS_PREFIX}/lib/freeswitch/mod
g++ -shared \
    -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so \
    *.o \
    -lwebsockets \
    -lpthread \
    -lssl \
    -lcrypto 2>&1 | tee /tmp/mod_audio_fork_link.log > /dev/null
check_success "Failed to link mod_audio_fork.so (check /tmp/mod_audio_fork_link.log)" "g++ linking"

log_substep "Validating mod_audio_fork.so with ldd..."
if ldd ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so | grep -q "not found"; then
    echo -e "${RED}✗ mod_audio_fork has missing dependencies:${NC}"
    ldd ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so | grep "not found"
    handle_error 1 "mod_audio_fork has missing dependencies" "ldd validation"
fi
echo -e "  ${GREEN}✓${NC} All dependencies satisfied"

complete_step "mod_audio_fork built and validated"

# ============================================================================
# Step 8: Build mod_deepgram_transcribe
# ============================================================================
CURRENT_STEP=8
show_progress $CURRENT_STEP "Build mod_deepgram_transcribe"

cd ${SCRIPT_DIR}/../modules/mod_deepgram_transcribe
check_success "Failed to change directory to mod_deepgram_transcribe" "cd modules/mod_deepgram_transcribe"

log_substep "Compiling mod_deepgram_transcribe.c..."
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    mod_deepgram_transcribe.c 2>&1 | tee /tmp/mod_deepgram_gcc.log > /dev/null
check_success "Failed to compile mod_deepgram_transcribe.c (check /tmp/mod_deepgram_gcc.log)" "gcc mod_deepgram_transcribe.c"

log_substep "Compiling C++ files (dg_transcribe_glue.cpp, audio_pipe.cpp, parser.cpp)..."
g++ -fPIC -c -std=c++11 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp 2>&1 | tee /tmp/mod_deepgram_g++.log > /dev/null
check_success "Failed to compile C++ files (check /tmp/mod_deepgram_g++.log)" "g++ C++ files"

log_substep "Linking mod_deepgram_transcribe.so..."
g++ -shared \
    -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    mod_deepgram_transcribe.o \
    dg_transcribe_glue.o \
    audio_pipe.o \
    parser.o \
    -lwebsockets \
    -lpthread \
    -lssl \
    -lcrypto 2>&1 | tee /tmp/mod_deepgram_link.log > /dev/null
check_success "Failed to link mod_deepgram_transcribe.so (check /tmp/mod_deepgram_link.log)" "g++ linking"

log_substep "Validating mod_deepgram_transcribe.so with ldd..."
if ldd ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so | grep -q "not found"; then
    echo -e "${RED}✗ mod_deepgram_transcribe has missing dependencies:${NC}"
    ldd ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so | grep "not found"
    handle_error 1 "mod_deepgram_transcribe has missing dependencies" "ldd validation"
fi
echo -e "  ${GREEN}✓${NC} All dependencies satisfied"

complete_step "mod_deepgram_transcribe built and validated"

# ============================================================================
# Step 9: Build mod_aws_transcribe
# ============================================================================
CURRENT_STEP=9
show_progress $CURRENT_STEP "Build mod_aws_transcribe"

cd ${SCRIPT_DIR}/../modules/mod_aws_transcribe
check_success "Failed to change directory to mod_aws_transcribe" "cd modules/mod_aws_transcribe"

log_substep "Compiling mod_aws_transcribe.c..."
gcc -fPIC -c \
    -I${FS_PREFIX}/include/freeswitch \
    mod_aws_transcribe.c 2>&1 | tee /tmp/mod_aws_gcc.log > /dev/null
check_success "Failed to compile mod_aws_transcribe.c (check /tmp/mod_aws_gcc.log)" "gcc mod_aws_transcribe.c"

log_substep "Compiling aws_transcribe_glue.cpp..."
g++ -fPIC -c -std=c++11 \
    -I${FS_PREFIX}/include/freeswitch \
    -I/usr/local/include \
    aws_transcribe_glue.cpp 2>&1 | tee /tmp/mod_aws_g++.log > /dev/null
check_success "Failed to compile aws_transcribe_glue.cpp (check /tmp/mod_aws_g++.log)" "g++ aws_transcribe_glue.cpp"

log_substep "Linking mod_aws_transcribe.so with AWS SDK libraries..."
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
    -lz 2>&1 | tee /tmp/mod_aws_link.log > /dev/null
check_success "Failed to link mod_aws_transcribe.so (check /tmp/mod_aws_link.log)" "g++ linking"

log_substep "Validating mod_aws_transcribe.so with ldd..."
if ldd ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so | grep -q "not found"; then
    echo -e "${RED}✗ mod_aws_transcribe has missing dependencies:${NC}"
    ldd ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so | grep "not found"
    handle_error 1 "mod_aws_transcribe has missing dependencies" "ldd validation"
fi
echo -e "  ${GREEN}✓${NC} All dependencies satisfied"

complete_step "mod_aws_transcribe built and validated"

# Mark modules as installed and set permissions
echo "modules=installed" >> "$MANIFEST_FILE"
log_substep "Setting module permissions..."
chown freeswitch:freeswitch ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so 2>/dev/null || true
chown freeswitch:freeswitch ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so 2>/dev/null || true
chown freeswitch:freeswitch ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so 2>/dev/null || true

# ============================================================================
# Step 10: Configure FreeSWITCH and Modules
# ============================================================================
CURRENT_STEP=10
show_progress $CURRENT_STEP "Configure FreeSWITCH and Modules"

# Add modules to modules.conf.xml
MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
if [ -f "$MODULES_CONF" ]; then
    log_substep "Adding modules to modules.conf.xml..."
    if ! grep -q "mod_audio_fork" "$MODULES_CONF"; then
        sed -i '/<\/modules>/i \    <!-- Speech Transcription Modules -->' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_audio_fork"/>' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_aws_transcribe"/>' "$MODULES_CONF"
        sed -i '/<\/modules>/i \    <load module="mod_deepgram_transcribe"/>' "$MODULES_CONF"
        echo -e "  ${GREEN}✓${NC} Added modules to modules.conf.xml"
    else
        echo -e "  ${YELLOW}ℹ${NC}  Modules already configured in modules.conf.xml"
    fi
else
    echo -e "  ${YELLOW}⚠${NC}  WARNING: modules.conf.xml not found at ${MODULES_CONF}"
fi

# Always copy dialplan
log_substep "Copying example dialplan and directory configuration..."
if [ -d "${SCRIPT_DIR}/../examples/freeswitch-config/dialplan" ]; then
    cp ${SCRIPT_DIR}/../examples/freeswitch-config/dialplan/default.xml \
       ${FS_PREFIX}/conf/dialplan/default.xml
    check_success "Failed to copy dialplan configuration" "cp dialplan"

    cp ${SCRIPT_DIR}/../examples/freeswitch-config/directory/100*.xml \
       ${FS_PREFIX}/conf/directory/default/ 2>/dev/null || true

    chown -R freeswitch:freeswitch ${FS_PREFIX}/conf 2>/dev/null || true
    echo -e "  ${GREEN}✓${NC} Example dialplan and directory configuration copied"
else
    echo -e "  ${YELLOW}⚠${NC}  WARNING: Example configuration not found"
fi

# Create systemd service
log_substep "Creating FreeSWITCH systemd service..."
cat > /etc/systemd/system/freeswitch.service <<EOF
[Unit]
Description=FreeSWITCH
After=syslog.target network.target local-fs.target
Wants=network-online.target

[Service]
Type=forking
PIDFile=${FS_PREFIX}/run/freeswitch.pid

ExecStart=${FS_PREFIX}/bin/freeswitch -ncwait -nonat -conf ${FS_PREFIX}/conf -log ${FS_PREFIX}/log -db ${FS_PREFIX}/db
ExecReload=/usr/bin/kill -HUP \$MAINPID
ExecStop=${FS_PREFIX}/bin/freeswitch -stop

User=freeswitch
Group=freeswitch

TimeoutStartSec=45s
TimeoutStopSec=45s
Restart=on-failure
RestartSec=5s

[Install]
WantedBy=multi-user.target
EOF
echo -e "  ${GREEN}✓${NC} FreeSWITCH systemd service created"

# Create environment configuration in drop-in directory
log_substep "Creating FreeSWITCH environment configuration..."
mkdir -p /etc/systemd/system/freeswitch.service.d
cat > /etc/systemd/system/freeswitch.service.d/environment.conf <<EOF
[Service]
Environment="LD_LIBRARY_PATH=/usr/local/lib"
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
echo -e "  ${GREEN}✓${NC} Environment configuration created at /etc/systemd/system/freeswitch.service.d/environment.conf"

# Create run directory for PID file
mkdir -p ${FS_PREFIX}/run
chown freeswitch:freeswitch ${FS_PREFIX}/run

systemctl daemon-reload
systemctl enable freeswitch.service
echo -e "  ${GREEN}✓${NC} FreeSWITCH systemd service enabled"

# Configure system resource limits (ulimit)
log_substep "Configuring system resource limits..."
cat > /etc/security/limits.d/freeswitch.conf <<EOF
freeswitch soft nofile 999999
freeswitch hard nofile 999999
freeswitch soft core unlimited
freeswitch hard core unlimited
freeswitch soft memlock unlimited
freeswitch hard memlock unlimited
freeswitch soft stack 240
freeswitch hard stack 240
EOF
echo -e "  ${GREEN}✓${NC} Resource limits configured (file descriptors: 999999, core dumps: unlimited)"

# Configure log rotation
log_substep "Configuring log rotation..."
cat > /etc/logrotate.d/freeswitch <<'EOF'
${FS_PREFIX}/log/freeswitch.log {
    daily
    rotate 30
    missingok
    notifempty
    compress
    delaycompress
    postrotate
        ${FS_PREFIX}/bin/fs_cli -x "fsctl send_sighup" > /dev/null 2>&1 || true
    endscript
}

${FS_PREFIX}/log/*.log {
    daily
    rotate 7
    missingok
    notifempty
    compress
    delaycompress
}
EOF
# Expand variables in logrotate config
sed -i "s|\${FS_PREFIX}|${FS_PREFIX}|g" /etc/logrotate.d/freeswitch
echo -e "  ${GREEN}✓${NC} Log rotation configured (main log: 30 days, other logs: 7 days)"

# Secure environment file permissions
log_substep "Securing environment file permissions..."
chmod 600 /etc/systemd/system/freeswitch.service.d/environment.conf
echo -e "  ${GREEN}✓${NC} Environment file permissions set to 600 (root only)"

# Configure sysctl kernel parameters
log_substep "Configuring kernel parameters for optimal performance..."
cat > /etc/sysctl.d/99-freeswitch.conf <<'EOF'
# Network Performance
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.ipv4.tcp_rmem = 4096 87380 16777216
net.ipv4.tcp_wmem = 4096 65536 16777216

# Connection Limits
net.core.somaxconn = 4096
net.ipv4.tcp_max_syn_backlog = 8192

# Port Range
net.ipv4.ip_local_port_range = 16384 65535

# Time-wait sockets
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_fin_timeout = 30

# Enable timestamps
net.ipv4.tcp_timestamps = 1

# Disable ICMP redirects
net.ipv4.conf.all.accept_redirects = 0
net.ipv4.conf.default.accept_redirects = 0

# Enable TCP window scaling
net.ipv4.tcp_window_scaling = 1
EOF
sysctl -p /etc/sysctl.d/99-freeswitch.conf > /dev/null 2>&1
echo -e "  ${GREEN}✓${NC} Kernel parameters optimized for high-performance networking"

# Create additional directories
log_substep "Creating additional working directories..."
mkdir -p ${FS_PREFIX}/temp ${FS_PREFIX}/cache ${FS_PREFIX}/recordings ${FS_PREFIX}/storage
chown -R freeswitch:freeswitch ${FS_PREFIX}/temp ${FS_PREFIX}/cache ${FS_PREFIX}/recordings ${FS_PREFIX}/storage
echo -e "  ${GREEN}✓${NC} Working directories created (temp, cache, recordings, storage)"

# Configure core dumps
log_substep "Configuring core dump collection..."
mkdir -p /var/crash/freeswitch
chown freeswitch:freeswitch /var/crash/freeswitch
# Add core dump configuration to systemd service
sed -i '/\[Service\]/a LimitCORE=infinity\nWorkingDirectory=/var/crash/freeswitch' /etc/systemd/system/freeswitch.service
systemctl daemon-reload
echo -e "  ${GREEN}✓${NC} Core dump collection enabled at /var/crash/freeswitch"

# Configure ACL and Event Socket security
log_substep "Configuring Access Control Lists..."
cat > ${FS_PREFIX}/conf/autoload_configs/acl.conf.xml <<'EOF'
<configuration name="acl.conf" description="Network Lists">
  <network-lists>
    <!-- Local network access -->
    <list name="lan" default="allow">
      <node type="allow" cidr="192.168.0.0/16"/>
      <node type="allow" cidr="10.0.0.0/8"/>
      <node type="allow" cidr="172.16.0.0/12"/>
      <node type="allow" cidr="127.0.0.0/8"/>
    </list>

    <!-- Event Socket restricted to localhost for security -->
    <list name="event-socket" default="deny">
      <node type="allow" cidr="127.0.0.1/32"/>
    </list>
  </network-lists>
</configuration>
EOF
# Apply ACL to Event Socket
sed -i 's|<!--<param name="apply-inbound-acl" value="loopback.auto"/>-->|<param name="apply-inbound-acl" value="event-socket"/>|' \
    ${FS_PREFIX}/conf/autoload_configs/event_socket.conf.xml
chown freeswitch:freeswitch ${FS_PREFIX}/conf/autoload_configs/acl.conf.xml
echo -e "  ${GREEN}✓${NC} ACL configured (Event Socket restricted to localhost)"

# Integrate health check
if [ -f "${SCRIPT_DIR}/health-check.sh" ]; then
    log_substep "Integrating health check monitoring..."

    cat > /etc/systemd/system/freeswitch-healthcheck.service <<EOF
[Unit]
Description=FreeSWITCH Health Check
After=freeswitch.service

[Service]
Type=oneshot
ExecStart=${SCRIPT_DIR}/health-check.sh
StandardOutput=journal
StandardError=journal
EOF

    cat > /etc/systemd/system/freeswitch-healthcheck.timer <<'EOF'
[Unit]
Description=FreeSWITCH Health Check Timer
After=freeswitch.service

[Timer]
OnBootSec=2min
OnUnitActiveSec=5min

[Install]
WantedBy=timers.target
EOF

    systemctl daemon-reload
    systemctl enable freeswitch-healthcheck.timer
    echo -e "  ${GREEN}✓${NC} Health check monitoring enabled (runs every 5 minutes)"
else
    echo -e "  ${YELLOW}⚠${NC}  Health check script not found at ${SCRIPT_DIR}/health-check.sh"
fi

complete_step "FreeSWITCH and modules configured"

# ============================================================================
# Step 11: Validate Installation
# ============================================================================
CURRENT_STEP=11
if [ "$NO_VALIDATION" = false ]; then
    show_progress $CURRENT_STEP "Validate Installation"

    # Check module files exist
    log_substep "Checking module files exist..."
    for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
            echo -e "  ${GREEN}✓${NC} ${module}.so exists"
        else
            handle_error 1 "${module}.so NOT FOUND" "File existence check"
        fi
    done

    # Check dependencies (already done during build, but double-check)
    log_substep "Verifying module dependencies (ldd)..."
    for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
        MODULE_PATH="${FS_PREFIX}/lib/freeswitch/mod/${module}.so"
        if ldd "$MODULE_PATH" | grep -q "not found"; then
            echo -e "${RED}✗${NC} ${module} has missing dependencies:"
            ldd "$MODULE_PATH" | grep "not found"
            handle_error 1 "${module} has missing dependencies" "ldd check"
        else
            echo -e "  ${GREEN}✓${NC} ${module} dependencies OK"
        fi
    done

    # Try to start FreeSWITCH and check modules load
    if [ "$SKIP_FREESWITCH" = false ]; then
        log_substep "Testing module loading (starting FreeSWITCH)..."
        export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH

        # Start FreeSWITCH in background with explicit conf directory
        ${FS_PREFIX}/bin/freeswitch -nc -nonat \
            -conf ${FS_PREFIX}/conf \
            -log ${FS_PREFIX}/log \
            -db ${FS_PREFIX}/db > /dev/null 2>&1 &
        FS_PID=$!

        # Wait for startup (modules need time to load)
        echo -e "${CYAN}  ➜ Waiting 60 seconds for FreeSWITCH to fully start and load modules...${NC}"
        sleep 60

        # Check if modules loaded
        MODULE_LOAD_FAILED=false

        if ${FS_PREFIX}/bin/fs_cli -x "show modules" 2>/dev/null | grep -q "mod_audio_fork"; then
            echo -e "  ${GREEN}✓${NC} mod_audio_fork loaded successfully"
        else
            echo -e "  ${RED}✗${NC} mod_audio_fork failed to load"
            MODULE_LOAD_FAILED=true
        fi

        if ${FS_PREFIX}/bin/fs_cli -x "show modules" 2>/dev/null | grep -q "mod_aws_transcribe"; then
            echo -e "  ${GREEN}✓${NC} mod_aws_transcribe loaded successfully"
        else
            echo -e "  ${RED}✗${NC} mod_aws_transcribe failed to load"
            MODULE_LOAD_FAILED=true
        fi

        if ${FS_PREFIX}/bin/fs_cli -x "show modules" 2>/dev/null | grep -q "mod_deepgram_transcribe"; then
            echo -e "  ${GREEN}✓${NC} mod_deepgram_transcribe loaded successfully"
        else
            echo -e "  ${RED}✗${NC} mod_deepgram_transcribe failed to load"
            MODULE_LOAD_FAILED=true
        fi

        # Stop FreeSWITCH
        log_substep "Stopping FreeSWITCH test instance..."
        kill $FS_PID 2>/dev/null || true
        sleep 2

        if [ "$MODULE_LOAD_FAILED" = true ]; then
            echo -e "${RED}One or more modules failed to load. Check FreeSWITCH logs at:${NC}"
            echo -e "${RED}  ${FS_PREFIX}/log/freeswitch.log${NC}"
            handle_error 1 "One or more modules failed to load" "Module loading test"
        fi
    fi

    complete_step "Installation validated successfully"
else
    show_progress $CURRENT_STEP "Skipping Validation (--no-validation)"
    echo -e "${YELLOW}  Validation skipped as requested${NC}"
fi

# ============================================================================
# Installation Complete - Summary
# ============================================================================
echo ""
echo -e "${BOLD}${GREEN}╔══════════════════════════════════════════════════════════════╗${NC}"
echo -e "${BOLD}${GREEN}║                                                              ║${NC}"
echo -e "${BOLD}${GREEN}║           ✓ INSTALLATION COMPLETED SUCCESSFULLY!            ║${NC}"
echo -e "${BOLD}${GREEN}║                                                              ║${NC}"
echo -e "${BOLD}${GREEN}╚══════════════════════════════════════════════════════════════╝${NC}"
echo ""

# Show all completed steps
if [ ${#COMPLETED_STEPS[@]} -gt 0 ]; then
    echo -e "${BOLD}Completed Steps (${#COMPLETED_STEPS[@]}/${#ALL_STEPS[@]}):${NC}"
    for step in "${COMPLETED_STEPS[@]}"; do
        echo -e "  ${GREEN}✓${NC} ${step}"
    done
    echo ""
fi

echo -e "${BOLD}Installation Summary:${NC}"
echo "  ✓ libwebsockets 4.3.3"
echo "  ✓ AWS SDK C++ 1.11.345"
echo "  ✓ spandsp 3.x (from source)"
echo "  ✓ sofia-sip 1.13.17 (from source)"
if [ "$SKIP_FREESWITCH" = false ]; then
    echo "  ✓ FreeSWITCH 1.10.11"
fi
echo "  ✓ mod_audio_fork"
echo "  ✓ mod_aws_transcribe"
echo "  ✓ mod_deepgram_transcribe"
echo ""
echo -e "${BOLD}Installation Location:${NC} ${FS_PREFIX}"
echo -e "${BOLD}Installation Manifest:${NC} ${MANIFEST_FILE}"
echo ""
echo -e "${BOLD}${CYAN}Next Steps:${NC}"
echo ""
echo -e "${BOLD}1. Configure API keys:${NC}"
echo "   export DEEPGRAM_API_KEY=your_key"
echo "   export AWS_ACCESS_KEY_ID=your_key"
echo "   export AWS_SECRET_ACCESS_KEY=your_secret"
echo "   export AWS_REGION=us-east-1"
echo ""
echo -e "${BOLD}2. Start FreeSWITCH:${NC}"
echo "   sudo systemctl start freeswitch"
echo "   # Or manually: ${FS_PREFIX}/bin/freeswitch -nc -nonat -conf ${FS_PREFIX}/conf -log ${FS_PREFIX}/log -db ${FS_PREFIX}/db"
echo ""
echo -e "${BOLD}3. Check FreeSWITCH status:${NC}"
echo "   sudo systemctl status freeswitch"
echo ""
echo -e "${BOLD}4. Verify modules are loaded:${NC}"
echo "   ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram'"
echo ""
echo -e "${BOLD}5. Test a call:${NC}"
echo "   - Register SIP extension 1000 (password: 1234)"
echo "   - Call extension 1002 to test Deepgram transcription"
echo "   - Call extension 1003 to test AWS Transcribe"
echo ""
echo -e "${BOLD}${YELLOW}Troubleshooting:${NC}"
echo "  - Check FreeSWITCH logs: ${FS_PREFIX}/log/freeswitch.log"
echo "  - Check module logs: grep 'mod_audio_fork\|mod_aws\|mod_deepgram' ${FS_PREFIX}/log/freeswitch.log"
echo "  - Review manifest: cat ${MANIFEST_FILE}"
echo "  - To clean up and reinstall: sudo ${SCRIPT_DIR}/cleanup-all.sh"
echo ""
echo -e "${BOLD}${GREEN}Installation completed at: $(date)${NC}"
echo ""
