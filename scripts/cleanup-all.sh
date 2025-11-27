#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Unified Cleanup Script
# ============================================================================
# Supports modular cleanup via --module flag:
#   --module freeswitch              Remove only FreeSWITCH
#   --module mod_audio_fork          Remove only mod_audio_fork
#   --module mod_aws_transcribe      Remove only mod_aws_transcribe and AWS SDK
#   --module mod_deepgram_transcribe Remove only mod_deepgram_transcribe
#   --module mod_google_transcribe   Remove only mod_google_transcribe and gRPC
#   --module mod_google_transcribev2 Remove only mod_google_transcribev2 and Google Cloud C++
#   --module all                     Remove everything (default)
#
# Usage:
#   sudo ./cleanup-all.sh --module <module-name> [OPTIONS]
#
# Options:
#   --module NAME      Module to remove (freeswitch, mod_*, all)
#   --keep-sources     Keep source directories in /usr/local/src
#   --yes              Skip confirmation prompts
#   --help             Show this help message
#
# Examples:
#   sudo ./cleanup-all.sh --module mod_google_transcribe
#   sudo ./cleanup-all.sh --module freeswitch
#   sudo ./cleanup-all.sh --module all --yes
# ============================================================================

set -e

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

# Defaults
MODULE="all"
KEEP_SOURCES=false
AUTO_YES=false
FS_PREFIX="/usr/local/freeswitch"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST_FILE="$(cd "${SCRIPT_DIR}/.." && pwd)/.freeswitch-install-manifest.txt"

# Valid module names
VALID_MODULES=("all" "freeswitch" "mod_audio_fork" "mod_aws_transcribe" "mod_deepgram_transcribe" "mod_google_transcribe" "mod_google_transcribev2")

# ============================================================================
# Helper Functions
# ============================================================================

should_remove_module() {
    local target="$1"
    [ "$MODULE" = "all" ] || [ "$MODULE" = "$target" ]
}

should_remove_freeswitch() {
    [ "$MODULE" = "all" ] || [ "$MODULE" = "freeswitch" ]
}

log_remove() {
    echo -e "${CYAN}  ➜ Removing: $1${NC}"
}

log_skip() {
    echo -e "${YELLOW}  ↷ Skipping: $1${NC}"
}

log_success() {
    echo -e "${GREEN}  ✓ $1${NC}"
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
        --keep-sources)
            KEEP_SOURCES=true
            shift
            ;;
        --yes)
            AUTO_YES=true
            shift
            ;;
        --help)
            echo "FreeSWITCH Speech AI - Unified Cleanup Script"
            echo ""
            echo "Usage: $0 --module <module-name> [OPTIONS]"
            echo ""
            echo "Modules:"
            echo "  all                      Remove FreeSWITCH + all modules + dependencies (default)"
            echo "  freeswitch               Remove only FreeSWITCH"
            echo "  mod_audio_fork           Remove only mod_audio_fork"
            echo "  mod_aws_transcribe       Remove only mod_aws_transcribe and AWS SDK dependencies"
            echo "  mod_deepgram_transcribe  Remove only mod_deepgram_transcribe"
            echo "  mod_google_transcribe    Remove only mod_google_transcribe and gRPC dependencies"
            echo "  mod_google_transcribev2  Remove only mod_google_transcribev2 and Google Cloud C++ dependencies"
            echo ""
            echo "Options:"
            echo "  --keep-sources    Keep source directories in /usr/local/src"
            echo "  --yes             Skip confirmation prompts"
            echo "  --help            Show this help message"
            echo ""
            echo "Examples:"
            echo "  sudo $0 --module mod_google_transcribe"
            echo "  sudo $0 --module freeswitch"
            echo "  sudo $0 --module all --yes"
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

# Check root
if [ "$EUID" -ne 0 ]; then
    echo -e "${RED}Error: This script must be run as root${NC}"
    echo "Please run: sudo $0 --module $MODULE"
    exit 1
fi

# ============================================================================
# Show what will be removed
# ============================================================================

echo -e "${RED}=============================================${NC}"
echo -e "${RED}FreeSWITCH Speech AI - Cleanup${NC}"
echo -e "${RED}=============================================${NC}"
echo ""
echo "Module: $MODULE"
echo ""
echo "This will remove:"

if should_remove_freeswitch; then
    echo "  ✗ FreeSWITCH ($FS_PREFIX)"
    echo "  ✗ spandsp (/usr/local/lib/libspandsp*)"
    echo "  ✗ sofia-sip (/usr/local/lib/libsofia-sip*)"
fi

if should_remove_module "mod_audio_fork"; then
    echo "  ✗ mod_audio_fork module and binary"
fi

if should_remove_module "mod_aws_transcribe"; then
    echo "  ✗ mod_aws_transcribe module and binary"
    echo "  ✗ AWS SDK C++ (/usr/local/lib/libaws-*)"
fi

if should_remove_module "mod_deepgram_transcribe"; then
    echo "  ✗ mod_deepgram_transcribe module and binary"
fi

if should_remove_module "mod_google_transcribe"; then
    echo "  ✗ mod_google_transcribe module and binary"
fi

if should_remove_module "mod_google_transcribev2"; then
    echo "  ✗ mod_google_transcribev2 module and binary"
    echo "  ✗ Google Cloud C++ libraries (/usr/local/lib/libgoogle_cloud_cpp*)"
fi

# libwebsockets shared by audio_fork and deepgram
if should_remove_module "mod_audio_fork" || should_remove_module "mod_deepgram_transcribe"; then
    echo "  ✗ libwebsockets (/usr/local/lib/libwebsockets*)"
fi

if ! $KEEP_SOURCES; then
    echo "  ✗ Source directories (/usr/local/src/*)"
fi

echo ""

# Confirmation
if [ "$AUTO_YES" = false ]; then
    read -p "Are you sure you want to proceed? (yes/no): " -r
    if [ "$REPLY" != "yes" ]; then
        echo "Cleanup cancelled."
        exit 0
    fi
fi

# ============================================================================
# Cleanup Process
# ============================================================================

echo ""
echo -e "${CYAN}Starting cleanup...${NC}"
echo ""

# Stop FreeSWITCH if running
if should_remove_freeswitch && systemctl is-active --quiet freeswitch 2>/dev/null; then
    log_remove "Stopping FreeSWITCH service"
    systemctl stop freeswitch || true
    log_success "FreeSWITCH stopped"
fi

# Remove modules
if should_remove_module "mod_audio_fork"; then
    log_remove "mod_audio_fork"
    rm -f ${FS_PREFIX}/lib/freeswitch/mod/mod_audio_fork.so
    log_success "mod_audio_fork removed"
fi

if should_remove_module "mod_aws_transcribe"; then
    log_remove "mod_aws_transcribe"
    rm -f ${FS_PREFIX}/lib/freeswitch/mod/mod_aws_transcribe.so
    log_success "mod_aws_transcribe removed"
fi

if should_remove_module "mod_deepgram_transcribe"; then
    log_remove "mod_deepgram_transcribe"
    rm -f ${FS_PREFIX}/lib/freeswitch/mod/mod_deepgram_transcribe.so
    log_success "mod_deepgram_transcribe removed"
fi

if should_remove_module "mod_google_transcribe"; then
    log_remove "mod_google_transcribe"
    rm -f ${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so*
    log_success "mod_google_transcribe removed"
fi

if should_remove_module "mod_google_transcribev2"; then
    log_remove "mod_google_transcribev2"
    rm -f ${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribev2.so
    log_success "mod_google_transcribev2 removed"
fi

# Remove FreeSWITCH
if should_remove_freeswitch; then
    log_remove "FreeSWITCH installation"
    rm -rf ${FS_PREFIX}
    rm -f /etc/systemd/system/freeswitch.service
    systemctl daemon-reload 2>/dev/null || true
    log_success "FreeSWITCH removed"

    log_remove "spandsp libraries"
    rm -f /usr/local/lib/libspandsp*
    rm -rf /usr/local/include/spandsp*
    log_success "spandsp removed"

    log_remove "sofia-sip libraries"
    rm -f /usr/local/lib/libsofia-sip*
    rm -rf /usr/local/include/sofia-sip*
    log_success "sofia-sip removed"
fi

# Remove dependencies
if should_remove_module "mod_audio_fork" || should_remove_module "mod_deepgram_transcribe"; then
    if [ "$MODULE" = "all" ] || [ "$MODULE" = "mod_audio_fork" ] || [ "$MODULE" = "mod_deepgram_transcribe" ]; then
        # Only remove if we're cleaning all, or both modules that use it
        if grep -q "libwebsockets=installed" "$MANIFEST_FILE" 2>/dev/null || [ "$MODULE" = "all" ]; then
            log_remove "libwebsockets libraries"
            rm -f /usr/local/lib/libwebsockets*
            rm -rf /usr/local/include/libwebsockets*
            ldconfig
            log_success "libwebsockets removed"
        else
            log_skip "libwebsockets (was pre-existing)"
        fi
    fi
fi

if should_remove_module "mod_aws_transcribe"; then
    if grep -q "aws-sdk-cpp=installed" "$MANIFEST_FILE" 2>/dev/null || [ "$MODULE" = "all" ]; then
        log_remove "AWS SDK C++ libraries"
        rm -f /usr/local/lib/libaws-*
        rm -rf /usr/local/lib/cmake/aws-*
        rm -rf /usr/local/include/aws
        ldconfig
        log_success "AWS SDK C++ removed"
    else
        log_skip "AWS SDK C++ (was pre-existing)"
    fi
fi

if should_remove_module "mod_google_transcribe"; then
    if grep -q "grpc=installed" "$MANIFEST_FILE" 2>/dev/null; then
        log_remove "gRPC and Protocol Buffers (system packages)"
        apt-get remove -y libgrpc++-dev libgrpc-dev protobuf-compiler protobuf-compiler-grpc libprotobuf-dev > /dev/null 2>&1 || true
        apt-get autoremove -y > /dev/null 2>&1 || true
        log_success "gRPC removed"
    else
        log_skip "gRPC (was pre-existing or not installed)"
    fi
fi

if should_remove_module "mod_google_transcribev2"; then
    if grep -q "google_cloud_cpp=installed" "$MANIFEST_FILE" 2>/dev/null; then
        log_remove "Google Cloud C++ libraries"
        rm -f /usr/local/lib/libgoogle_cloud_cpp*
        rm -rf /usr/local/lib/cmake/google_cloud_cpp*
        rm -rf /usr/local/include/google/cloud
        ldconfig
        log_success "Google Cloud C++ libraries removed"
    else
        log_skip "Google Cloud C++ libraries (was pre-existing or not installed)"
    fi
fi

# Remove source directories
if ! $KEEP_SOURCES; then
    log_remove "Source directories"

    if should_remove_freeswitch; then
        rm -rf /usr/local/src/freeswitch
        rm -rf /usr/local/src/spandsp
        rm -rf /usr/local/src/sofia-sip
    fi

    if should_remove_module "mod_audio_fork" || should_remove_module "mod_deepgram_transcribe"; then
        rm -rf /usr/local/src/libwebsockets
    fi

    if should_remove_module "mod_aws_transcribe"; then
        rm -rf /usr/local/src/aws-sdk-cpp
    fi

    if should_remove_module "mod_google_transcribe"; then
        rm -rf /usr/local/src/grpc
        rm -rf /usr/local/src/googleapis
    fi

    if should_remove_module "mod_google_transcribev2"; then
        rm -rf /usr/local/src/google-cloud-cpp
    fi

    log_success "Source directories removed"
else
    log_skip "Source directories (--keep-sources specified)"
fi

# Update/remove manifest
if [ "$MODULE" = "all" ]; then
    log_remove "Installation manifest"
    rm -f "$MANIFEST_FILE"
    log_success "Manifest removed"
else
    # Remove specific module entries from manifest
    if [ -f "$MANIFEST_FILE" ]; then
        if should_remove_freeswitch; then
            sed -i '/^freeswitch=/d' "$MANIFEST_FILE" 2>/dev/null || true
            sed -i '/^spandsp=/d' "$MANIFEST_FILE" 2>/dev/null || true
            sed -i '/^sofia-sip=/d' "$MANIFEST_FILE" 2>/dev/null || true
        fi
        if should_remove_module "mod_audio_fork" || should_remove_module "mod_deepgram_transcribe"; then
            sed -i '/^libwebsockets=/d' "$MANIFEST_FILE" 2>/dev/null || true
        fi
        if should_remove_module "mod_aws_transcribe"; then
            sed -i '/^aws-sdk-cpp=/d' "$MANIFEST_FILE" 2>/dev/null || true
        fi
        if should_remove_module "mod_google_transcribe"; then
            sed -i '/^grpc=/d' "$MANIFEST_FILE" 2>/dev/null || true
            sed -i '/^googleapis=/d' "$MANIFEST_FILE" 2>/dev/null || true
        fi
        if should_remove_module "mod_google_transcribev2"; then
            sed -i '/^google_cloud_cpp=/d' "$MANIFEST_FILE" 2>/dev/null || true
        fi
        log_success "Manifest updated"
    fi
fi

# Run ldconfig to update library cache
ldconfig

# ============================================================================
# Cleanup Complete
# ============================================================================

echo ""
echo -e "${GREEN}=============================================${NC}"
echo -e "${GREEN}✓ Cleanup Complete!${NC}"
echo -e "${GREEN}=============================================${NC}"
echo ""
echo "Module: $MODULE"
echo ""
echo "Removed components have been deleted from the system."
if $KEEP_SOURCES; then
    echo "Source directories were preserved in /usr/local/src"
fi
echo ""
