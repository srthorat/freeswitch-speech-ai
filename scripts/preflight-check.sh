#!/bin/bash
#
# Pre-flight system check for FreeSWITCH Speech AI installation
# Validates system requirements before starting installation
#
# Usage: ./preflight-check.sh [--strict]
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

STRICT_MODE=false
FAILED_CHECKS=0
WARNING_CHECKS=0

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --strict)
            STRICT_MODE=true
            shift
            ;;
        --help)
            echo "Usage: $0 [--strict]"
            echo ""
            echo "Options:"
            echo "  --strict    Fail on warnings (exit code 1 if any warnings)"
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

echo "========================================="
echo "FreeSWITCH Speech AI Pre-Flight Check"
echo "========================================="
echo ""

# ============================================================================
# 1. Operating System Check
# ============================================================================
echo "📋 Operating System:"
if [ -f /etc/os-release ]; then
    . /etc/os-release
    echo "  Distribution: $NAME $VERSION"

    case "$ID" in
        ubuntu)
            if [[ "$VERSION_ID" =~ ^(20\.04|22\.04|24\.04)$ ]]; then
                echo -e "  ${GREEN}✓ Supported Ubuntu version${NC}"
            else
                echo -e "  ${YELLOW}⚠ Ubuntu $VERSION_ID not officially tested${NC}"
                echo "    Recommended: Ubuntu 20.04, 22.04, or 24.04"
                WARNING_CHECKS=$((WARNING_CHECKS + 1))
            fi
            ;;
        debian)
            if [[ "$VERSION_ID" =~ ^(11|12)$ ]]; then
                echo -e "  ${GREEN}✓ Supported Debian version${NC}"
            else
                echo -e "  ${YELLOW}⚠ Debian $VERSION_ID not officially tested${NC}"
                echo "    Recommended: Debian 11 or 12"
                WARNING_CHECKS=$((WARNING_CHECKS + 1))
            fi
            ;;
        *)
            echo -e "  ${YELLOW}⚠ $NAME not officially supported${NC}"
            echo "    Recommended: Ubuntu 20.04/22.04/24.04 or Debian 11/12"
            WARNING_CHECKS=$((WARNING_CHECKS + 1))
            ;;
    esac
else
    echo -e "  ${RED}✗ Cannot detect OS distribution${NC}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 2. Disk Space Check
# ============================================================================
echo "💾 Disk Space:"
AVAILABLE_GB=$(df -BG . | tail -1 | awk '{print $4}' | sed 's/G//')
REQUIRED_GB=30

echo "  Available: ${AVAILABLE_GB}GB"
echo "  Required:  ${REQUIRED_GB}GB"

if [ "$AVAILABLE_GB" -ge "$REQUIRED_GB" ]; then
    echo -e "  ${GREEN}✓ Sufficient disk space${NC}"
elif [ "$AVAILABLE_GB" -ge 20 ]; then
    echo -e "  ${YELLOW}⚠ Low disk space (${AVAILABLE_GB}GB)${NC}"
    echo "    Recommended: 30GB+, have ${AVAILABLE_GB}GB"
    WARNING_CHECKS=$((WARNING_CHECKS + 1))
else
    echo -e "  ${RED}✗ Insufficient disk space (${AVAILABLE_GB}GB)${NC}"
    echo "    Need at least 30GB, have only ${AVAILABLE_GB}GB"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 3. Memory (RAM) Check
# ============================================================================
echo "🧠 Memory (RAM):"
TOTAL_RAM_GB=$(free -g | awk '/^Mem:/{print $2}')
AVAILABLE_RAM_GB=$(free -g | awk '/^Mem:/{print $7}')
REQUIRED_RAM_GB=8

echo "  Total:     ${TOTAL_RAM_GB}GB"
echo "  Available: ${AVAILABLE_RAM_GB}GB"
echo "  Required:  ${REQUIRED_RAM_GB}GB"

if [ "$TOTAL_RAM_GB" -ge "$REQUIRED_RAM_GB" ]; then
    echo -e "  ${GREEN}✓ Sufficient RAM${NC}"
elif [ "$TOTAL_RAM_GB" -ge 4 ]; then
    echo -e "  ${YELLOW}⚠ Low RAM (${TOTAL_RAM_GB}GB total)${NC}"
    echo "    Recommended: 8GB+, may cause slow builds or OOM errors"
    WARNING_CHECKS=$((WARNING_CHECKS + 1))
else
    echo -e "  ${RED}✗ Insufficient RAM (${TOTAL_RAM_GB}GB)${NC}"
    echo "    Need at least 8GB for reliable builds"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 4. CPU Check
# ============================================================================
echo "⚙️  CPU:"
CPU_CORES=$(nproc)
REQUIRED_CORES=4

echo "  Cores:    $CPU_CORES"
echo "  Required: $REQUIRED_CORES"

if [ "$CPU_CORES" -ge "$REQUIRED_CORES" ]; then
    echo -e "  ${GREEN}✓ Sufficient CPU cores${NC}"
elif [ "$CPU_CORES" -ge 2 ]; then
    echo -e "  ${YELLOW}⚠ Low CPU cores ($CPU_CORES)${NC}"
    echo "    Recommended: 4+ cores, build will be slow with $CPU_CORES cores"
    WARNING_CHECKS=$((WARNING_CHECKS + 1))
else
    echo -e "  ${RED}✗ Insufficient CPU cores ($CPU_CORES)${NC}"
    echo "    Need at least 4 cores for reasonable build times"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 5. Internet Connectivity Check
# ============================================================================
echo "🌐 Internet Connectivity:"
if ping -c 1 -W 5 github.com &> /dev/null; then
    echo -e "  ${GREEN}✓ Internet connection available${NC}"
else
    echo -e "  ${RED}✗ Cannot reach github.com${NC}"
    echo "    Internet connection required for downloading dependencies"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 6. Required System Packages Check
# ============================================================================
echo "📦 Required System Packages:"
REQUIRED_PACKAGES=(
    "build-essential"
    "git"
    "wget"
    "curl"
    "pkg-config"
    "autoconf"
    "automake"
    "libtool"
    "libssl-dev"
    "zlib1g-dev"
)

MISSING_PACKAGES=()
for package in "${REQUIRED_PACKAGES[@]}"; do
    if dpkg -l | grep -q "^ii  $package"; then
        echo -e "  ${GREEN}✓${NC} $package"
    else
        echo -e "  ${RED}✗${NC} $package (missing)"
        MISSING_PACKAGES+=("$package")
    fi
done

if [ ${#MISSING_PACKAGES[@]} -eq 0 ]; then
    echo -e "  ${GREEN}✓ All required packages installed${NC}"
else
    echo ""
    echo -e "  ${RED}✗ Missing ${#MISSING_PACKAGES[@]} required package(s)${NC}"
    echo "    Install with:"
    echo "    sudo apt-get update && sudo apt-get install -y ${MISSING_PACKAGES[*]}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 7. Compiler Check
# ============================================================================
echo "🔨 Compiler:"
if command -v gcc &> /dev/null; then
    GCC_VERSION=$(gcc --version | head -1)
    echo "  GCC: $GCC_VERSION"
    echo -e "  ${GREEN}✓ GCC available${NC}"
else
    echo -e "  ${RED}✗ GCC not found${NC}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi

if command -v g++ &> /dev/null; then
    GXX_VERSION=$(g++ --version | head -1)
    echo "  G++: $GXX_VERSION"
    echo -e "  ${GREEN}✓ G++ available${NC}"
else
    echo -e "  ${RED}✗ G++ not found${NC}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# 8. Conflicting Installations Check
# ============================================================================
echo "🔍 Checking for Conflicting Installations:"
CONFLICTS_FOUND=false

# Check for existing FreeSWITCH
if [ -d "/usr/local/freeswitch" ] || [ -d "/opt/freeswitch" ] || [ -d "/etc/freeswitch" ]; then
    echo -e "  ${YELLOW}ℹ${NC}  FreeSWITCH already installed"
    echo "    Use install-all.sh --module <module> to update specific modules"
    CONFLICTS_FOUND=true
fi

# Check for package manager FreeSWITCH
if dpkg -l | grep -q "^ii  freeswitch"; then
    echo -e "  ${YELLOW}⚠${NC}  FreeSWITCH installed via package manager"
    echo "    May conflict with manual installation"
    WARNING_CHECKS=$((WARNING_CHECKS + 1))
    CONFLICTS_FOUND=true
fi

if [ "$CONFLICTS_FOUND" = false ]; then
    echo -e "  ${GREEN}✓ No conflicting installations found${NC}"
fi
echo ""

# ============================================================================
# 9. Permissions Check
# ============================================================================
echo "🔐 Permissions:"
if [ "$EUID" -eq 0 ]; then
    echo -e "  ${GREEN}✓ Running as root${NC}"
elif groups | grep -q sudo; then
    echo -e "  ${GREEN}✓ User has sudo privileges${NC}"
else
    echo -e "  ${RED}✗ Not running as root and no sudo privileges${NC}"
    echo "    Installation requires root or sudo access"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi
echo ""

# ============================================================================
# Summary
# ============================================================================
echo "========================================="
echo "Summary:"
echo "========================================="

if [ $FAILED_CHECKS -eq 0 ] && [ $WARNING_CHECKS -eq 0 ]; then
    echo -e "${GREEN}✓ All checks passed!${NC}"
    echo "  System is ready for FreeSWITCH Speech AI installation."
    echo ""
    echo "Next steps:"
    echo "  1. Full installation:    sudo ./scripts/install-all.sh"
    echo "  2. Module only:          sudo ./scripts/install-all.sh --module <module>"
    echo "  3. Update modules:       sudo ./scripts/update-modules.sh"
    exit 0
elif [ $FAILED_CHECKS -eq 0 ]; then
    echo -e "${YELLOW}⚠ ${WARNING_CHECKS} warning(s) found${NC}"
    echo "  Installation may proceed but with potential issues."
    echo ""
    if [ "$STRICT_MODE" = true ]; then
        echo "  Strict mode enabled - treating warnings as failures"
        exit 1
    else
        echo "  Review warnings above before proceeding."
        exit 0
    fi
else
    echo -e "${RED}✗ ${FAILED_CHECKS} critical check(s) failed${NC}"
    if [ $WARNING_CHECKS -gt 0 ]; then
        echo -e "${YELLOW}⚠ ${WARNING_CHECKS} warning(s) found${NC}"
    fi
    echo ""
    echo "  Please fix the issues above before attempting installation."
    exit 1
fi
