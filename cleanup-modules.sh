#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Cleanup Modules Script
# ============================================================================
# Removes only the 3 transcription modules:
#   - mod_audio_fork
#   - mod_aws_transcribe
#   - mod_deepgram_transcribe
#
# Usage:
#   sudo ./cleanup-modules.sh [--freeswitch-prefix PATH]
# ============================================================================

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

FS_PREFIX="/usr/local/freeswitch"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --freeswitch-prefix)
            FS_PREFIX="$2"
            shift 2
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            exit 1
            ;;
    esac
done

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    exit 1
fi

echo -e "${YELLOW}=============================================${NC}"
echo -e "${YELLOW}Removing Speech Transcription Modules${NC}"
echo -e "${YELLOW}=============================================${NC}"
echo "FreeSWITCH Prefix: $FS_PREFIX"
echo ""

# First confirmation
read -p "This will remove mod_audio_fork, mod_aws_transcribe, and mod_deepgram_transcribe. Continue? (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
fi

# Second confirmation
echo ""
read -p "Are you sure? This action will remove all module files and configuration. (y/N) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "Aborted."
    exit 1
fi

echo ""
# Remove module files
echo "Removing module files..."
for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
    if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
        rm -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so"
        echo -e "${GREEN}✓${NC} Removed ${module}.so"
    else
        echo -e "${YELLOW}ℹ${NC} ${module}.so not found"
    fi
done

# Remove from modules.conf.xml
MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
if [ -f "$MODULES_CONF" ]; then
    echo "Removing from modules.conf.xml..."
    
    # Backup first
    cp "$MODULES_CONF" "${MODULES_CONF}.backup.$(date +%Y%m%d_%H%M%S)"
    
    # Remove module entries
    sed -i '/mod_audio_fork/d' "$MODULES_CONF"
    sed -i '/mod_aws_transcribe/d' "$MODULES_CONF"
    sed -i '/mod_deepgram_transcribe/d' "$MODULES_CONF"
    sed -i '/Speech Transcription Modules/d' "$MODULES_CONF"
    
    echo -e "${GREEN}✓${NC} Removed from configuration"
fi

# Clean up build artifacts
echo "Cleaning build artifacts..."
cd $(dirname "$0")
for module_dir in modules/mod_audio_fork modules/mod_aws_transcribe modules/mod_deepgram_transcribe; do
    if [ -d "$module_dir" ]; then
        cd "$module_dir"
        rm -f *.o *.so
        cd - > /dev/null
    fi
done

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Modules Removed${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "The following were NOT removed (run cleanup-all.sh to remove):"
echo "  - libwebsockets"
echo "  - AWS SDK C++"
echo "  - FreeSWITCH"
echo ""
echo "To reload FreeSWITCH without these modules:"
echo "  ${FS_PREFIX}/bin/fs_cli -x 'reload mod_sofia'"
echo "  or: systemctl restart freeswitch"
echo ""
