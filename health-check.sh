#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - Health Check Script
# ============================================================================
# Validates that all components are working correctly
#
# Usage:
#   ./health-check.sh [--freeswitch-prefix PATH]
#
# Exit codes:
#   0 - All checks passed
#   1 - One or more checks failed
# ============================================================================

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

FS_PREFIX="/usr/local/freeswitch"
FAILED_CHECKS=0

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
echo "FreeSWITCH Speech AI - Health Check"
echo "========================================"
echo ""

# Check 1: FreeSWITCH running
echo -n "Checking FreeSWITCH service... "
if systemctl is-active --quiet freeswitch 2>/dev/null || pgrep -x freeswitch > /dev/null; then
    echo -e "${GREEN}✓ Running${NC}"
else
    echo -e "${RED}✗ Not running${NC}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi

# Check 2: FreeSWITCH responsive
echo -n "Checking FreeSWITCH CLI connectivity... "
if ${FS_PREFIX}/bin/fs_cli -x "status" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ Responsive${NC}"
else
    echo -e "${RED}✗ Not responsive${NC}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi

# Check 3: Modules loaded
echo ""
echo "Checking modules loaded:"
for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
    echo -n "  $module... "
    if ${FS_PREFIX}/bin/fs_cli -x "module_exists ${module}" 2>/dev/null | grep -q "true"; then
        echo -e "${GREEN}✓ Loaded${NC}"
    else
        echo -e "${RED}✗ Not loaded${NC}"
        FAILED_CHECKS=$((FAILED_CHECKS + 1))
    fi
done

# Check 4: Module dependencies
echo ""
echo "Checking module dependencies:"
for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe; do
    MODULE_PATH="${FS_PREFIX}/lib/freeswitch/mod/${module}.so"
    if [ -f "$MODULE_PATH" ]; then
        echo -n "  $module dependencies... "
        if ldd "$MODULE_PATH" 2>/dev/null | grep -q "not found"; then
            echo -e "${RED}✗ Missing dependencies:${NC}"
            ldd "$MODULE_PATH" | grep "not found"
            FAILED_CHECKS=$((FAILED_CHECKS + 1))
        else
            echo -e "${GREEN}✓ OK${NC}"
        fi
    fi
done

# Check 5: SIP port listening
echo ""
echo -n "Checking SIP port (5060)... "
if netstat -tuln 2>/dev/null | grep -q ":5060 " || ss -tuln 2>/dev/null | grep -q ":5060 "; then
    echo -e "${GREEN}✓ Listening${NC}"
else
    echo -e "${YELLOW}⚠${NC}  Not listening (may be normal if not configured)"
fi

# Check 6: Event socket port
echo -n "Checking Event Socket (8021)... "
if netstat -tuln 2>/dev/null | grep -q ":8021 " || ss -tuln 2>/dev/null | grep -q ":8021 "; then
    echo -e "${GREEN}✓ Listening${NC}"
else
    echo -e "${YELLOW}⚠${NC}  Not listening"
fi

# Check 7: Configuration files
echo ""
echo "Checking configuration files:"
MODULES_CONF="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
echo -n "  modules.conf.xml... "
if [ -f "$MODULES_CONF" ]; then
    if grep -q "mod_audio_fork\|mod_aws_transcribe\|mod_deepgram_transcribe" "$MODULES_CONF"; then
        echo -e "${GREEN}✓ Configured${NC}"
    else
        echo -e "${YELLOW}⚠${NC}  Modules not configured"
    fi
else
    echo -e "${RED}✗ Not found${NC}"
    FAILED_CHECKS=$((FAILED_CHECKS + 1))
fi

# Summary
echo ""
echo "========================================"
if [ $FAILED_CHECKS -eq 0 ]; then
    echo -e "${GREEN}✓ All critical checks passed!${NC}"
    echo "========================================"
    exit 0
else
    echo -e "${RED}✗ $FAILED_CHECKS check(s) failed${NC}"
    echo "========================================"
    echo ""
    echo "Troubleshooting:"
    echo "  - Check logs: ${FS_PREFIX}/log/freeswitch.log"
    echo "  - Verify modules: ${FS_PREFIX}/bin/fs_cli -x 'show modules'"
    echo "  - Restart FreeSWITCH: systemctl restart freeswitch"
    exit 1
fi
