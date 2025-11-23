#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Status Script
# ============================================================================
# Shows installation status of all components
#
# Usage:
#   ./status.sh [--freeswitch-prefix PATH]
# ============================================================================

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

FS_PREFIX="/usr/local/freeswitch"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST_FILE="$(cd "${SCRIPT_DIR}/.." && pwd)/.freeswitch-install-manifest.txt"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --freeswitch-prefix)
            FS_PREFIX="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "========================================"
echo "FreeSWITCH Speech AI - Status"
echo "========================================"
echo ""

# Check FreeSWITCH
if [ -d "$FS_PREFIX" ]; then
    FS_VERSION=$("${FS_PREFIX}/bin/freeswitch" -version 2>/dev/null | head -1 || echo "Unknown")
    if systemctl is-active --quiet freeswitch 2>/dev/null || pgrep -x freeswitch > /dev/null; then
        echo -e "${GREEN}✓${NC} FreeSWITCH: Running ($FS_VERSION)"
    else
        echo -e "${YELLOW}⚠${NC}  FreeSWITCH: Installed but not running ($FS_VERSION)"
    fi
else
    echo -e "${RED}✗${NC} FreeSWITCH: Not installed"
fi

# Check modules
echo ""
echo "Modules:"
for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
    if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
        # Check if module is loaded
        if ${FS_PREFIX}/bin/fs_cli -x "module_exists ${module}" 2>/dev/null | grep -q "true"; then
            echo -e "  ${GREEN}✓${NC} ${module}: Installed and loaded"
        else
            echo -e "  ${YELLOW}⚠${NC}  ${module}: Installed but not loaded"
        fi
    else
        echo -e "  ${RED}✗${NC} ${module}: Not installed"
    fi
done

# Check dependencies
echo ""
echo "Dependencies:"

if ldconfig -p | grep -q libwebsockets; then
    LWS_VER=$(ldconfig -p | grep libwebsockets | head -1 | awk '{print $1}')
    echo -e "  ${GREEN}✓${NC} libwebsockets: Installed ($LWS_VER)"
else
    echo -e "  ${RED}✗${NC} libwebsockets: Not installed"
fi

if ldconfig -p | grep -q aws-cpp-sdk-transcribestreaming; then
    echo -e "  ${GREEN}✓${NC} AWS SDK C++: Installed"
else
    echo -e "  ${RED}✗${NC} AWS SDK C++: Not installed"
fi

# Check manifest
echo ""
echo "Installation Manifest:"
if [ -f "$MANIFEST_FILE" ]; then
    echo -e "  ${GREEN}✓${NC} Found at: $MANIFEST_FILE"
    echo ""
    echo "  Installed components:"
    while IFS='=' read -r component status; do
        if [[ ! "$component" =~ ^# && -n "$component" ]]; then
            if [ "$status" = "installed" ]; then
                echo -e "    ${GREEN}✓${NC} $component (installed by us)"
            elif [ "$status" = "existing" ]; then
                echo -e "    ${YELLOW}ℹ${NC}  $component (was already installed)"
            fi
        fi
    done < "$MANIFEST_FILE"
else
    echo -e "  ${YELLOW}⚠${NC}  No manifest file found"
    echo "  Run install-all.sh or install-modules-only.sh to create one"
fi

echo ""
echo "========================================"
