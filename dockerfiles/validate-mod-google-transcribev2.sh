#!/bin/bash
# ============================================================================
# Validation Script for mod_google_transcribev2
# ============================================================================
# This script validates that mod_google_transcribev2 is properly installed
# and functional in a running FreeSWITCH container.
#
# Usage:
#   ./dockerfiles/validate-mod-google-transcribev2.sh <container_name_or_id>
#
# Example:
#   ./dockerfiles/validate-mod-google-transcribev2.sh freeswitch-google
#
# Exit codes:
#   0 - All validations passed
#   1 - Validation failed
# ============================================================================

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Container name
CONTAINER="${1:-freeswitch-google}"

echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}mod_google_transcribev2 Validation${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""
echo "Container: $CONTAINER"
echo ""

# Helper function for validation steps
validate_step() {
    local step_num="$1"
    local step_name="$2"
    local command="$3"
    local expected_pattern="$4"

    echo -e "${BLUE}[$step_num] $step_name${NC}"
    echo "Command: $command"
    echo ""

    if docker exec "$CONTAINER" bash -c "$command" > /tmp/validation_output.txt 2>&1; then
        if [ -n "$expected_pattern" ]; then
            if grep -q "$expected_pattern" /tmp/validation_output.txt; then
                echo -e "${GREEN}✅ PASS${NC}"
                cat /tmp/validation_output.txt
            else
                echo -e "${RED}❌ FAIL - Expected pattern not found: $expected_pattern${NC}"
                cat /tmp/validation_output.txt
                return 1
            fi
        else
            echo -e "${GREEN}✅ PASS${NC}"
            cat /tmp/validation_output.txt
        fi
    else
        echo -e "${RED}❌ FAIL - Command failed${NC}"
        cat /tmp/validation_output.txt
        return 1
    fi
    echo ""
    return 0
}

# Check if container is running
echo -e "${BLUE}[0] Checking if container is running...${NC}"
if ! docker ps --format '{{.Names}}' | grep -q "^${CONTAINER}$"; then
    echo -e "${RED}❌ ERROR: Container '$CONTAINER' is not running${NC}"
    echo ""
    echo "Available containers:"
    docker ps --format 'table {{.Names}}\t{{.Status}}\t{{.Ports}}'
    exit 1
fi
echo -e "${GREEN}✅ Container is running${NC}"
echo ""

# Validation tests
FAILED=0

# 1. Check module file exists
validate_step "1" "Check module file exists" \
    "ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so" \
    "mod_google_transcribev2.so" || FAILED=1

# 2. Check module dependencies
validate_step "2" "Check module dependencies (ldd)" \
    "ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so" \
    "" || FAILED=1

# 3. Verify no missing dependencies
validate_step "3" "Verify no missing dependencies" \
    "ldd /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so | grep -v 'not found'" \
    "" || FAILED=1

# 4. Check Google Cloud SDK libraries
validate_step "4" "Check Google Cloud C++ Speech library" \
    "ls -lh /usr/local/lib/libgoogle_cloud_cpp_speech.so*" \
    "libgoogle_cloud_cpp_speech.so" || FAILED=1

# 5. Check module is loaded in FreeSWITCH
echo -e "${BLUE}[5] Check if mod_google_transcribev2 is loaded${NC}"
echo "Command: fs_cli -x 'module_exists mod_google_transcribev2'"
echo ""
if docker exec "$CONTAINER" /usr/local/freeswitch/bin/fs_cli -x "module_exists mod_google_transcribev2" 2>&1 | grep -q "true"; then
    echo -e "${GREEN}✅ PASS - Module is loaded${NC}"
    docker exec "$CONTAINER" /usr/local/freeswitch/bin/fs_cli -x "module_exists mod_google_transcribev2"
elif docker exec "$CONTAINER" /usr/local/freeswitch/bin/fs_cli -x "module_exists mod_google_transcribev2" 2>&1 | grep -q "false"; then
    echo -e "${YELLOW}⚠️  WARNING - Module exists but is not loaded${NC}"
    echo "Attempting to load module..."
    docker exec "$CONTAINER" /usr/local/freeswitch/bin/fs_cli -x "load mod_google_transcribev2"
    sleep 2
    if docker exec "$CONTAINER" /usr/local/freeswitch/bin/fs_cli -x "module_exists mod_google_transcribev2" 2>&1 | grep -q "true"; then
        echo -e "${GREEN}✅ Module loaded successfully${NC}"
    else
        echo -e "${RED}❌ FAIL - Could not load module${NC}"
        FAILED=1
    fi
else
    echo -e "${RED}❌ FAIL - Could not check module status${NC}"
    echo "FreeSWITCH may not be running. Checking..."
    docker exec "$CONTAINER" /usr/local/freeswitch/bin/fs_cli -x "status"
    FAILED=1
fi
echo ""

# 6. Check FreeSWITCH version
validate_step "6" "Check FreeSWITCH version" \
    "/usr/local/freeswitch/bin/fs_cli -x 'version'" \
    "FreeSWITCH" || FAILED=1

# 7. Check for module errors in logs
echo -e "${BLUE}[7] Check for module errors in logs${NC}"
echo "Command: grep -i 'mod_google_transcribev2' /usr/local/freeswitch/log/freeswitch.log | tail -20"
echo ""
if docker exec "$CONTAINER" bash -c "grep -i 'mod_google_transcribev2' /usr/local/freeswitch/log/freeswitch.log 2>/dev/null | tail -20" > /tmp/module_logs.txt; then
    if grep -qiE "error|fail|cannot|unable" /tmp/module_logs.txt; then
        echo -e "${RED}❌ FAIL - Errors found in module logs:${NC}"
        cat /tmp/module_logs.txt
        FAILED=1
    else
        echo -e "${GREEN}✅ PASS - No errors found in module logs${NC}"
        cat /tmp/module_logs.txt
    fi
else
    echo -e "${YELLOW}⚠️  WARNING - Could not find module logs (module may not have been loaded yet)${NC}"
fi
echo ""

# 8. Check environment variables
echo -e "${BLUE}[8] Check Google Cloud environment variables${NC}"
echo ""
GOOGLE_APP_CREDS=$(docker exec "$CONTAINER" bash -c 'echo $GOOGLE_APPLICATION_CREDENTIALS')
GOOGLE_PROJECT=$(docker exec "$CONTAINER" bash -c 'echo $GOOGLE_PROJECT_ID')
GCP_LOC=$(docker exec "$CONTAINER" bash -c 'echo $GCP_LOCATION')

echo "GOOGLE_APPLICATION_CREDENTIALS: ${GOOGLE_APP_CREDS:-<not set>}"
echo "GOOGLE_PROJECT_ID: ${GOOGLE_PROJECT:-<not set>}"
echo "GCP_LOCATION: ${GCP_LOC:-<not set>}"
echo ""

if [ -z "$GOOGLE_APP_CREDS" ] || [ -z "$GOOGLE_PROJECT" ]; then
    echo -e "${YELLOW}⚠️  WARNING - Google Cloud credentials not configured${NC}"
    echo "To configure, run container with:"
    echo "  -e GOOGLE_APPLICATION_CREDENTIALS=/path/to/creds.json"
    echo "  -e GOOGLE_PROJECT_ID=your-project-id"
    echo "  -e GCP_LOCATION=us-central1"
else
    echo -e "${GREEN}✅ Environment variables are set${NC}"
    # Check if credentials file exists
    if docker exec "$CONTAINER" test -f "$GOOGLE_APP_CREDS"; then
        echo -e "${GREEN}✅ Credentials file exists: $GOOGLE_APP_CREDS${NC}"
    else
        echo -e "${RED}❌ Credentials file not found: $GOOGLE_APP_CREDS${NC}"
        FAILED=1
    fi
fi
echo ""

# Summary
echo -e "${BLUE}=========================================${NC}"
echo -e "${BLUE}Validation Summary${NC}"
echo -e "${BLUE}=========================================${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}✅ ALL VALIDATIONS PASSED${NC}"
    echo ""
    echo "mod_google_transcribev2 is properly installed and loaded!"
    echo ""
    echo "Next steps:"
    echo "  1. Configure Google Cloud credentials (if not already done)"
    echo "  2. Make a test call and use uuid_google_transcribev2 API"
    echo "  3. Check transcription output in logs"
    exit 0
else
    echo -e "${RED}❌ SOME VALIDATIONS FAILED${NC}"
    echo ""
    echo "Please review the errors above and:"
    echo "  1. Check container logs: docker logs $CONTAINER"
    echo "  2. Check FreeSWITCH logs inside container:"
    echo "     docker exec $CONTAINER tail -f /usr/local/freeswitch/log/freeswitch.log"
    echo "  3. Verify build completed successfully"
    exit 1
fi
