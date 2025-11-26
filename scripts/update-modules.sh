#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Update Modules Script
# ============================================================================
# Updates modules to latest code without reinstalling dependencies
#
# Usage:
#   sudo ./update-modules.sh [OPTIONS]
#
# Options:
#   --freeswitch-prefix PATH    FreeSWITCH installation path (default: /usr/local/freeswitch)
#   --build-cpus N              Number of CPUs for build (default: 4)
#   --no-restart                Don't restart FreeSWITCH after update
#   --yes                       Skip confirmation prompts
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
NO_RESTART=false
AUTO_YES=false
VERBOSE=false
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

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
        --no-restart)
            NO_RESTART=true
            shift
            ;;
        --yes)
            AUTO_YES=true
            shift
            ;;
        --verbose)
            VERBOSE=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--freeswitch-prefix PATH] [--build-cpus N] [--no-restart] [--yes] [--verbose]"
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
        echo -e "\033[0;36m    Running: $@\033[0m"
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
echo -e "${GREEN}FreeSWITCH Speech AI - Update Modules${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Configuration:"
echo "  FreeSWITCH Prefix: $FS_PREFIX"
echo "  Build CPUs: $BUILD_CPUS"
echo "  Restart after update: $([ "$NO_RESTART" = false ] && echo "Yes" || echo "No")"
echo ""

# Check if FreeSWITCH exists
if [ ! -d "$FS_PREFIX" ]; then
    echo -e "${RED}Error: FreeSWITCH not found at $FS_PREFIX${NC}"
    exit 1
fi

# Confirmation
if [ "$AUTO_YES" = false ]; then
    echo "This will:"
    echo "  1. Pull latest code from repository"
    echo "  2. Rebuild all modules (mod_audio_fork, mod_aws_transcribe, mod_deepgram_transcribe, mod_google_transcribe)"
    echo "  3. Replace existing modules"
    if [ "$NO_RESTART" = false ]; then
        echo "  4. Restart FreeSWITCH"
    fi
    echo ""
    read -p "Continue? (y/N) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

echo ""
echo -e "${YELLOW}Starting update...${NC}"

# Step 1: Pull latest code
echo ""
echo -e "${GREEN}[1/3] Pulling latest code...${NC}"
cd "$SCRIPT_DIR/.."

if [ -d ".git" ]; then
    CURRENT_BRANCH=$(git branch --show-current)
    echo "Current branch: $CURRENT_BRANCH"

    if ! git pull origin "$CURRENT_BRANCH"; then
        echo -e "${RED}✗ Failed to pull latest code${NC}"
        echo "You may have local changes. Commit or stash them first."
        exit 1
    fi
    echo -e "${GREEN}✓ Code updated${NC}"
else
    echo -e "${YELLOW}⚠${NC}  Not a git repository - skipping code update"
fi

# Step 2: Rebuild modules
echo ""
echo -e "${GREEN}[2/3] Rebuilding modules...${NC}"

# Backup existing modules
echo "Backing up existing modules..."
BACKUP_DIR="${FS_PREFIX}/lib/freeswitch/mod/.backup.$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe mod_google_transcribe; do
    if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
        cp "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" "$BACKUP_DIR/"
        echo -e "${GREEN}✓${NC} Backed up ${module}.so"
    fi
done

# Build mod_audio_fork
echo ""
echo "Building mod_audio_fork..."
cd ${SCRIPT_DIR}/../modules/mod_audio_fork

# Clean previous build
rm -f *.o *.so

echo "  Compiling mod_audio_fork.c..."
log_command "mod_audio_fork.c" "${LOG_DIR}/update_mod_audio_fork_c.log" \
    gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_audio_fork.c
check_success "Failed to compile mod_audio_fork.c" "${LOG_DIR}/update_mod_audio_fork_c.log"

echo "  Compiling mod_audio_fork C++ sources..."
log_command "mod_audio_fork C++" "${LOG_DIR}/update_mod_audio_fork_cpp.log" \
    g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include lws_glue.cpp audio_pipe.cpp parser.cpp
check_success "Failed to compile mod_audio_fork C++ sources" "${LOG_DIR}/update_mod_audio_fork_cpp.log"

echo "  Linking mod_audio_fork..."
log_command "mod_audio_fork link" "${LOG_DIR}/update_mod_audio_fork_link.log" \
    g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so *.o -lwebsockets -lpthread -lssl -lcrypto
check_success "Failed to link mod_audio_fork" "${LOG_DIR}/update_mod_audio_fork_link.log"

echo -e "${GREEN}✓ mod_audio_fork${NC}"

# Build mod_aws_transcribe
echo "Building mod_aws_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_aws_transcribe

# Clean previous build
rm -f *.o *.so

echo "  Compiling mod_aws_transcribe.c..."
log_command "mod_aws_transcribe.c" "${LOG_DIR}/update_mod_aws_transcribe_c.log" \
    gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch mod_aws_transcribe.c
check_success "Failed to compile mod_aws_transcribe.c" "${LOG_DIR}/update_mod_aws_transcribe_c.log"

echo "  Compiling mod_aws_transcribe C++ sources..."
log_command "mod_aws_transcribe C++" "${LOG_DIR}/update_mod_aws_transcribe_cpp.log" \
    g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include aws_transcribe_glue.cpp
check_success "Failed to compile mod_aws_transcribe C++ sources" "${LOG_DIR}/update_mod_aws_transcribe_cpp.log"

echo "  Linking mod_aws_transcribe..."
log_command "mod_aws_transcribe link" "${LOG_DIR}/update_mod_aws_transcribe_link.log" \
    g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so \
    mod_aws_transcribe.o aws_transcribe_glue.o \
    -L/usr/local/lib -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core \
    -laws-c-event-stream -laws-checksums -laws-c-common \
    -lpthread -lcurl -lssl -lcrypto -lz
check_success "Failed to link mod_aws_transcribe" "${LOG_DIR}/update_mod_aws_transcribe_link.log"

echo -e "${GREEN}✓ mod_aws_transcribe${NC}"

# Build mod_deepgram_transcribe
echo "Building mod_deepgram_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_deepgram_transcribe

# Clean previous build
rm -f *.o *.so

echo "  Compiling mod_deepgram_transcribe.c..."
log_command "mod_deepgram_transcribe.c" "${LOG_DIR}/update_mod_deepgram_transcribe_c.log" \
    gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_deepgram_transcribe.c
check_success "Failed to compile mod_deepgram_transcribe.c" "${LOG_DIR}/update_mod_deepgram_transcribe_c.log"

echo "  Compiling mod_deepgram_transcribe C++ sources..."
log_command "mod_deepgram_transcribe C++" "${LOG_DIR}/update_mod_deepgram_transcribe_cpp.log" \
    g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp
check_success "Failed to compile mod_deepgram_transcribe C++ sources" "${LOG_DIR}/update_mod_deepgram_transcribe_cpp.log"

echo "  Linking mod_deepgram_transcribe..."
log_command "mod_deepgram_transcribe link" "${LOG_DIR}/update_mod_deepgram_transcribe_link.log" \
    g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    mod_deepgram_transcribe.o dg_transcribe_glue.o audio_pipe.o parser.o \
    -lwebsockets -lpthread -lssl -lcrypto
check_success "Failed to link mod_deepgram_transcribe" "${LOG_DIR}/update_mod_deepgram_transcribe_link.log"

echo -e "${GREEN}✓ mod_deepgram_transcribe${NC}"

# Build mod_google_transcribe
echo "Building mod_google_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_google_transcribe

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
    log_command "protoc" "${LOG_DIR}/update_mod_google_transcribe_protoc.log" \
        protoc --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` speech.proto
    check_success "Failed to generate protobuf files" "${LOG_DIR}/update_mod_google_transcribe_protoc.log"
fi

# Build using Makefile
echo "  Cleaning previous build..."
log_command "mod_google_transcribe clean" "${LOG_DIR}/update_mod_google_transcribe_clean.log" \
    make clean
# Don't check success for clean, it's okay if it fails

echo "  Building mod_google_transcribe..."
log_command "mod_google_transcribe make" "${LOG_DIR}/update_mod_google_transcribe_make.log" \
    make
check_success "Failed to build mod_google_transcribe" "${LOG_DIR}/update_mod_google_transcribe_make.log"

# Install the module
echo "  Installing mod_google_transcribe..."
log_command "mod_google_transcribe install" "${LOG_DIR}/update_mod_google_transcribe_install.log" \
    make install
check_success "Failed to install mod_google_transcribe" "${LOG_DIR}/update_mod_google_transcribe_install.log"

echo -e "${GREEN}✓ mod_google_transcribe${NC}"

echo -e "${GREEN}✓ All modules rebuilt${NC}"

# Step 3: Restart FreeSWITCH
if [ "$NO_RESTART" = false ]; then
    echo ""
    echo -e "${GREEN}[3/3] Restarting FreeSWITCH...${NC}"

    if systemctl is-active --quiet freeswitch 2>/dev/null; then
        systemctl restart freeswitch
        sleep 2

        if systemctl is-active --quiet freeswitch; then
            echo -e "${GREEN}✓ FreeSWITCH restarted${NC}"
        else
            echo -e "${RED}✗ FreeSWITCH failed to restart${NC}"
            echo "Check logs: ${FS_PREFIX}/log/freeswitch.log"
            echo ""
            echo "Backup modules are available at: $BACKUP_DIR"
            exit 1
        fi
    else
        echo -e "${YELLOW}⚠${NC}  FreeSWITCH was not running - not restarting"
    fi
else
    echo ""
    echo -e "${YELLOW}ℹ${NC}  Skipping FreeSWITCH restart (--no-restart specified)"
    echo "To load new modules, restart FreeSWITCH manually:"
    echo "  systemctl restart freeswitch"
fi

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Update Complete!${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Updated modules:"
echo "  ✓ mod_audio_fork"
echo "  ✓ mod_aws_transcribe"
echo "  ✓ mod_deepgram_transcribe"
echo "  ✓ mod_google_transcribe"
echo ""
echo "Backup location: $BACKUP_DIR"
echo ""
echo "Verify update:"
echo "  ./health-check.sh"
echo "  ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram|google'"
echo ""
