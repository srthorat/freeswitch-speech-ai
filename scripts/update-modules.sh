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
        --no-restart)
            NO_RESTART=true
            shift
            ;;
        --yes)
            AUTO_YES=true
            shift
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Usage: $0 [--freeswitch-prefix PATH] [--build-cpus N] [--no-restart] [--yes]"
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
    echo "  2. Rebuild all modules (mod_audio_fork, mod_aws_transcribe, mod_deepgram_transcribe)"
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

for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
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

gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_audio_fork.c
g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include lws_glue.cpp audio_pipe.cpp parser.cpp
g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so *.o -lwebsockets -lpthread -lssl -lcrypto
echo -e "${GREEN}✓ mod_audio_fork${NC}"

# Build mod_aws_transcribe
echo "Building mod_aws_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_aws_transcribe

# Clean previous build
rm -f *.o *.so

gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch mod_aws_transcribe.c
g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include aws_transcribe_glue.cpp
g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so \
    mod_aws_transcribe.o aws_transcribe_glue.o \
    -L/usr/local/lib -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core \
    -laws-c-event-stream -laws-checksums -laws-c-common \
    -lpthread -lcurl -lssl -lcrypto -lz
echo -e "${GREEN}✓ mod_aws_transcribe${NC}"

# Build mod_deepgram_transcribe
echo "Building mod_deepgram_transcribe..."
cd ${SCRIPT_DIR}/../modules/mod_deepgram_transcribe

# Clean previous build
rm -f *.o *.so

gcc -fPIC -c -I${FS_PREFIX}/include/freeswitch -I/usr/local/include mod_deepgram_transcribe.c
g++ -fPIC -c -std=c++11 -I${FS_PREFIX}/include/freeswitch -I/usr/local/include dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp
g++ -shared -o ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    mod_deepgram_transcribe.o dg_transcribe_glue.o audio_pipe.o parser.o \
    -lwebsockets -lpthread -lssl -lcrypto
echo -e "${GREEN}✓ mod_deepgram_transcribe${NC}"

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
echo ""
echo "Backup location: $BACKUP_DIR"
echo ""
echo "Verify update:"
echo "  ./health-check.sh"
echo "  ${FS_PREFIX}/bin/fs_cli -x 'show modules' | grep -E 'audio_fork|aws|deepgram'"
echo ""
