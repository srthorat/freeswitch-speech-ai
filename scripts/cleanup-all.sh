#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Complete Cleanup Script
# ============================================================================
# Removes EVERYTHING installed by install-all.sh:
#   - FreeSWITCH
#   - All 3 modules
#   - libwebsockets
#   - AWS SDK C++
#   - Source directories
#
# Usage:
#   sudo ./cleanup-all.sh [OPTIONS]
#
# Options:
#   --keep-freeswitch      Keep FreeSWITCH installation (remove only modules and dependencies)
#   --keep-sources         Keep source directories in /usr/local/src
#   --yes                  Skip confirmation prompts
# ============================================================================

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

# Defaults
KEEP_FREESWITCH=false
KEEP_SOURCES=false
AUTO_YES=false
FS_PREFIX="/usr/local/freeswitch"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST_FILE="$(cd "${SCRIPT_DIR}/.." && pwd)/.freeswitch-install-manifest.txt"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --keep-freeswitch)
            KEEP_FREESWITCH=true
            shift
            ;;
        --keep-sources)
            KEEP_SOURCES=true
            shift
            ;;
        --yes)
            AUTO_YES=true
            shift
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

echo -e "${RED}=============================================${NC}"
echo -e "${RED}COMPLETE CLEANUP${NC}"
echo -e "${RED}=============================================${NC}"
echo ""
echo "This will remove:"
if [ "$KEEP_FREESWITCH" = false ]; then
    echo "  ✗ FreeSWITCH ($FS_PREFIX)"
fi
echo "  ✗ mod_audio_fork, mod_aws_transcribe, mod_deepgram_transcribe"
echo "  ✗ libwebsockets (/usr/local/lib/libwebsockets*)"
echo "  ✗ spandsp (/usr/local/lib/libspandsp*)"
echo "  ✗ sofia-sip (/usr/local/lib/libsofia-sip*)"
echo "  ✗ AWS SDK C++ (/usr/local/lib/libaws-*)"
if [ "$KEEP_SOURCES" = false ]; then
    echo "  ✗ Source directories (/usr/local/src/{freeswitch,libwebsockets,aws-sdk-cpp,spandsp,sofia-sip})"
fi
echo ""

if [ "$AUTO_YES" = false ]; then
    # First confirmation
    read -p "Are you ABSOLUTELY sure? This CANNOT be undone! (type 'yes' to confirm): " -r
    if [ "$REPLY" != "yes" ]; then
        echo "Aborted."
        exit 1
    fi

    # Second confirmation
    echo ""
    read -p "FINAL WARNING: All selected components will be permanently deleted. Type 'yes' again to proceed: " -r
    if [ "$REPLY" != "yes" ]; then
        echo "Aborted."
        exit 1
    fi
fi

echo ""

# Check if manifest exists
if [ ! -f "$MANIFEST_FILE" ]; then
    echo -e "${YELLOW}⚠${NC}  Installation manifest not found at: $MANIFEST_FILE"
    echo ""
    echo "Without a manifest, we cannot determine what was installed by our scripts."
    echo "We will remove ALL selected components (this may affect other applications)."
    echo ""
    if [ "$AUTO_YES" = false ]; then
        read -p "Continue with full cleanup anyway? (type 'yes' to confirm): " -r
        if [ "$REPLY" != "yes" ]; then
            echo "Aborted."
            exit 1
        fi
    fi
fi

echo ""
echo -e "${YELLOW}Starting cleanup...${NC}"

# ============================================================================
# Stop FreeSWITCH
# ============================================================================
echo "Stopping FreeSWITCH..."
systemctl stop freeswitch 2>/dev/null || true
killall freeswitch 2>/dev/null || true
sleep 2

# ============================================================================
# Remove Modules
# ============================================================================
echo "Removing modules..."
MODULES_FOUND=false
if [ -d "${FS_PREFIX}/lib/freeswitch/mod" ]; then
    for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
        if [ -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so" ]; then
            rm -f "${FS_PREFIX}/lib/freeswitch/mod/${module}.so"
            echo -e "${GREEN}✓${NC} Removed ${module}.so"
            MODULES_FOUND=true
        fi
    done
fi

if [ "$MODULES_FOUND" = false ]; then
    echo -e "${YELLOW}ℹ${NC} No modules found (already removed or never installed)"
fi

# ============================================================================
# Remove FreeSWITCH
# ============================================================================
if [ "$KEEP_FREESWITCH" = false ]; then
    echo "Removing FreeSWITCH..."
    
    # Remove systemd service
    if [ -f "/etc/systemd/system/freeswitch.service" ]; then
        systemctl disable freeswitch 2>/dev/null || true
        rm -f /etc/systemd/system/freeswitch.service
        systemctl daemon-reload
    fi
    
    # Remove installation
    if [ -d "$FS_PREFIX" ]; then
        rm -rf "$FS_PREFIX"
        echo -e "${GREEN}✓${NC} Removed FreeSWITCH installation"
    else
        echo -e "${YELLOW}ℹ${NC} FreeSWITCH not found at $FS_PREFIX (already removed or never installed)"
    fi
    
    # Remove user
    userdel freeswitch 2>/dev/null || true
else
    echo -e "${YELLOW}ℹ${NC} Keeping FreeSWITCH installation"
fi

# ============================================================================
# Remove libwebsockets (only if we installed it OR no manifest exists)
# ============================================================================
echo "Removing libwebsockets..."

# Check manifest to see if we installed it
SHOULD_REMOVE_LWS=true
if [ -f "$MANIFEST_FILE" ]; then
    if grep -q "libwebsockets=existing" "$MANIFEST_FILE"; then
        echo -e "${YELLOW}ℹ${NC} libwebsockets was already installed - keeping it (not installed by us)"
        SHOULD_REMOVE_LWS=false
    elif grep -q "libwebsockets=installed" "$MANIFEST_FILE"; then
        echo "Removing libwebsockets (installed by our script)..."
    fi
else
    echo -e "${YELLOW}⚠${NC}  No manifest - will remove libwebsockets (may affect other apps)"
fi

if [ "$SHOULD_REMOVE_LWS" = true ]; then
    LWS_FOUND=false
    if ls /usr/local/lib/libwebsockets* 1> /dev/null 2>&1; then
        rm -f /usr/local/lib/libwebsockets*
        rm -f /usr/local/lib/pkgconfig/libwebsockets*.pc
        rm -rf /usr/local/include/libwebsockets*
        LWS_FOUND=true
    fi
    ldconfig 2>/dev/null || true

    if [ "$LWS_FOUND" = true ]; then
        echo -e "${GREEN}✓${NC} Removed libwebsockets"
    else
        echo -e "${YELLOW}ℹ${NC} libwebsockets not found (already removed or never installed)"
    fi
fi

# ============================================================================
# Remove spandsp (only if we installed it OR no manifest exists)
# ============================================================================
echo "Removing spandsp..."

SHOULD_REMOVE_SPANDSP=true
if [ -f "$MANIFEST_FILE" ]; then
    if grep -q "libspandsp=existing" "$MANIFEST_FILE"; then
        echo -e "${YELLOW}ℹ${NC} spandsp was already installed - keeping it"
        SHOULD_REMOVE_SPANDSP=false
    elif grep -q "libspandsp=installed" "$MANIFEST_FILE"; then
        echo "Removing spandsp (installed by our script)..."
    fi
fi

if [ "$SHOULD_REMOVE_SPANDSP" = true ]; then
    if ls /usr/local/lib/libspandsp* 1> /dev/null 2>&1; then
        rm -f /usr/local/lib/libspandsp*
        rm -rf /usr/local/include/spandsp*
        echo -e "${GREEN}✓${NC} Removed spandsp libraries"
    fi
fi

# ============================================================================
# Remove sofia-sip (only if we installed it OR no manifest exists)
# ============================================================================
echo "Removing sofia-sip..."

SHOULD_REMOVE_SOFIA=true
if [ -f "$MANIFEST_FILE" ]; then
    if grep -q "libsofia-sip=existing" "$MANIFEST_FILE"; then
        echo -e "${YELLOW}ℹ${NC} sofia-sip was already installed - keeping it"
        SHOULD_REMOVE_SOFIA=false
    elif grep -q "libsofia-sip=installed" "$MANIFEST_FILE"; then
        echo "Removing sofia-sip (installed by our script)..."
    fi
fi

if [ "$SHOULD_REMOVE_SOFIA" = true ]; then
    if ls /usr/local/lib/libsofia-sip* 1> /dev/null 2>&1; then
        rm -f /usr/local/lib/libsofia-sip*
        rm -rf /usr/local/include/sofia-sip*
        rm -rf /usr/local/share/sofia-sip
        echo -e "${GREEN}✓${NC} Removed sofia-sip libraries"
    fi
fi

# ============================================================================
# Remove AWS SDK C++ (only if we installed it OR no manifest exists)
# ============================================================================
echo "Removing AWS SDK C++..."

# Check manifest to see if we installed it
SHOULD_REMOVE_AWS=true
if [ -f "$MANIFEST_FILE" ]; then
    if grep -q "aws-sdk-cpp=existing" "$MANIFEST_FILE"; then
        echo -e "${YELLOW}ℹ${NC} AWS SDK C++ was already installed - keeping it (not installed by us)"
        SHOULD_REMOVE_AWS=false
    elif grep -q "aws-sdk-cpp=installed" "$MANIFEST_FILE"; then
        echo "Removing AWS SDK C++ (installed by our script)..."
    fi
else
    echo -e "${YELLOW}⚠${NC}  No manifest - will remove AWS SDK C++ (may affect other apps)"
fi

if [ "$SHOULD_REMOVE_AWS" = true ]; then
    AWS_FOUND=false
    if ls /usr/local/lib/libaws-* 1> /dev/null 2>&1; then
        rm -f /usr/local/lib/libaws-*
        rm -f /usr/local/lib/pkgconfig/aws-*.pc
        rm -rf /usr/local/include/aws
        rm -rf /usr/local/include/smithy
        AWS_FOUND=true
    fi
    ldconfig 2>/dev/null || true

    if [ "$AWS_FOUND" = true ]; then
        echo -e "${GREEN}✓${NC} Removed AWS SDK C++"
    else
        echo -e "${YELLOW}ℹ${NC} AWS SDK C++ not found (already removed or never installed)"
    fi
fi

# ============================================================================
# Remove Source Directories
# ============================================================================
if [ "$KEEP_SOURCES" = false ]; then
    echo "Removing source directories..."
    SOURCES_FOUND=false

    if [ -d "/usr/local/src/freeswitch" ]; then
        rm -rf /usr/local/src/freeswitch
        SOURCES_FOUND=true
    fi

    if [ -d "/usr/local/src/libwebsockets" ]; then
        rm -rf /usr/local/src/libwebsockets
        SOURCES_FOUND=true
    fi

    if [ -d "/usr/local/src/aws-sdk-cpp" ]; then
        rm -rf /usr/local/src/aws-sdk-cpp
        SOURCES_FOUND=true
    fi

    if [ -d "/usr/local/src/spandsp" ]; then
        rm -rf /usr/local/src/spandsp
        SOURCES_FOUND=true
    fi

    if [ -d "/usr/local/src/sofia-sip" ]; then
        rm -rf /usr/local/src/sofia-sip
        SOURCES_FOUND=true
    fi

    if [ "$SOURCES_FOUND" = true ]; then
        echo -e "${GREEN}✓${NC} Removed source directories"
    else
        echo -e "${YELLOW}ℹ${NC} No source directories found (already removed or never installed)"
    fi
else
    echo -e "${YELLOW}ℹ${NC} Keeping source directories"
fi

# ============================================================================
# Clean build artifacts in current directory
# ============================================================================
echo "Cleaning build artifacts..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"
for module_dir in modules/mod_audio_fork modules/mod_aws_transcribe modules/mod_deepgram_transcribe; do
    if [ -d "$module_dir" ]; then
        rm -f ${module_dir}/*.o ${module_dir}/*.so
    fi
done

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Cleanup Complete${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Removed:"
if [ "$KEEP_FREESWITCH" = false ]; then
    echo "  ✓ FreeSWITCH"
fi
echo "  ✓ Transcription modules"
echo "  ✓ libwebsockets"
echo "  ✓ AWS SDK C++"
if [ "$KEEP_SOURCES" = false ]; then
    echo "  ✓ Source directories"
fi
echo ""

if [ "$KEEP_FREESWITCH" = false ]; then
    echo "To reinstall everything, run:"
    echo "  sudo ./install-all.sh"
else
    echo "To reinstall modules only, run:"
    echo "  sudo ./install-modules-only.sh"
fi
echo ""
