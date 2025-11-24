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

# Note: Don't use 'set -e' to allow script to continue even if components are missing

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

# Check if FreeSWITCH exists
if [ ! -d "$FS_PREFIX" ]; then
    echo -e "${YELLOW}⚠${NC}  FreeSWITCH not found at $FS_PREFIX"
    echo -e "${YELLOW}ℹ${NC}  Nothing to clean up"
    exit 0
fi

# Remove module files
echo "Removing module files..."
MODULES_REMOVED=0
for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
    if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
        rm -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so"
        echo -e "${GREEN}✓${NC} Removed ${module}.so"
        MODULES_REMOVED=$((MODULES_REMOVED + 1))
    else
        echo -e "${YELLOW}ℹ${NC} ${module}.so not found (already removed or never installed)"
    fi
done

if [ $MODULES_REMOVED -eq 0 ]; then
    echo -e "${YELLOW}⚠${NC}  No modules were found to remove"
fi

# Remove from modules.conf.xml
MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
if [ -f "$MODULES_CONF" ]; then
    echo "Removing from modules.conf.xml..."

    # Check if modules are configured
    if grep -q "mod_audio_fork\|mod_aws_transcribe\|mod_deepgram_transcribe" "$MODULES_CONF"; then
        # Backup first
        cp "$MODULES_CONF" "${MODULES_CONF}.backup.$(date +%Y%m%d_%H%M%S)"

        # Remove module entries
        sed -i '/mod_audio_fork/d' "$MODULES_CONF"
        sed -i '/mod_aws_transcribe/d' "$MODULES_CONF"
        sed -i '/mod_deepgram_transcribe/d' "$MODULES_CONF"
        sed -i '/Speech Transcription Modules/d' "$MODULES_CONF"

        echo -e "${GREEN}✓${NC} Removed from configuration"
    else
        echo -e "${YELLOW}ℹ${NC} Modules not found in configuration (already removed)"
    fi
else
    echo -e "${YELLOW}⚠${NC}  modules.conf.xml not found at $MODULES_CONF"
fi

# Clean up build artifacts
echo "Cleaning build artifacts..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

ARTIFACTS_FOUND=false
for module_dir in ../modules/mod_audio_fork ../modules/mod_aws_transcribe ../modules/mod_deepgram_transcribe; do
    if [ -d "$module_dir" ]; then
        if ls ${module_dir}/*.o 1> /dev/null 2>&1 || ls ${module_dir}/*.so 1> /dev/null 2>&1; then
            rm -f ${module_dir}/*.o ${module_dir}/*.so
            ARTIFACTS_FOUND=true
        fi
    fi
done

if [ "$ARTIFACTS_FOUND" = true ]; then
    echo -e "${GREEN}✓${NC} Cleaned build artifacts"
else
    echo -e "${YELLOW}ℹ${NC} No build artifacts found"
fi

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
