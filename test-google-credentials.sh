#!/bin/bash
# Google Cloud Credentials Diagnostic Script

echo "=== Google Cloud Credentials Diagnostic ==="
echo ""

# Check environment variable
echo "1. Checking GOOGLE_APPLICATION_CREDENTIALS environment variable:"
if [ -z "$GOOGLE_APPLICATION_CREDENTIALS" ]; then
    echo "   ❌ NOT SET in current shell"
    echo "   Checking systemd service..."
    systemctl show freeswitch.service --property=Environment 2>/dev/null | grep GOOGLE_APPLICATION_CREDENTIALS || echo "   ❌ NOT SET in systemd"
else
    echo "   ✅ SET to: $GOOGLE_APPLICATION_CREDENTIALS"
fi
echo ""

# Check GCP_PROJECT_ID
echo "2. Checking GCP_PROJECT_ID:"
if [ -z "$GCP_PROJECT_ID" ]; then
    echo "   ❌ NOT SET in current shell"
else
    echo "   ✅ SET to: $GCP_PROJECT_ID"
fi
echo ""

# Find credentials file
echo "3. Looking for credentials file:"
CRED_FILE="/home/ubuntu/dev/sipp/st-stt-v2.json"
if [ -f "$CRED_FILE" ]; then
    echo "   ✅ File exists: $CRED_FILE"
    ls -lh "$CRED_FILE"
    echo ""

    # Check file permissions
    if [ -r "$CRED_FILE" ]; then
        echo "   ✅ File is readable"
    else
        echo "   ❌ File is NOT readable"
    fi
    echo ""

    # Validate JSON
    echo "4. Validating JSON structure:"
    if command -v jq &> /dev/null; then
        if jq empty "$CRED_FILE" 2>/dev/null; then
            echo "   ✅ Valid JSON"
            echo ""
            echo "5. Checking required fields:"
            jq -r 'if .type then "   ✅ type: " + .type else "   ❌ Missing: type" end' "$CRED_FILE"
            jq -r 'if .project_id then "   ✅ project_id: " + .project_id else "   ❌ Missing: project_id" end' "$CRED_FILE"
            jq -r 'if .private_key_id then "   ✅ private_key_id: " + .private_key_id else "   ❌ Missing: private_key_id" end' "$CRED_FILE"
            jq -r 'if .client_email then "   ✅ client_email: " + .client_email else "   ❌ Missing: client_email" end' "$CRED_FILE"
        else
            echo "   ❌ INVALID JSON"
        fi
    else
        echo "   ⚠️  jq not installed, skipping JSON validation"
        echo "   Showing first 5 lines of file:"
        head -5 "$CRED_FILE" | sed 's/^/   /'
    fi
else
    echo "   ❌ File NOT FOUND: $CRED_FILE"
    echo ""
    echo "   Searching for .json files in /home/ubuntu:"
    find /home/ubuntu -name "*.json" -type f 2>/dev/null | grep -v node_modules | head -10
fi
echo ""

# Check if FreeSWITCH is running
echo "6. Checking FreeSWITCH process:"
if pgrep -x freeswitch > /dev/null; then
    echo "   ✅ FreeSWITCH is running"
    echo "   Process details:"
    ps aux | grep freeswitch | grep -v grep | head -2 | sed 's/^/   /'
else
    echo "   ❌ FreeSWITCH is NOT running"
fi
echo ""

# Check module
echo "7. Checking mod_google_transcribe module:"
MODULE_PATH=$(find /usr -name "mod_google_transcribe.so" -type f 2>/dev/null | head -1)
if [ -n "$MODULE_PATH" ]; then
    echo "   ✅ Module found: $MODULE_PATH"
    ls -lh "$MODULE_PATH"
else
    echo "   ❌ Module NOT found"
    echo "   Module needs to be built and installed"
fi
echo ""

echo "=== Recommendations ==="
if [ ! -f "$CRED_FILE" ]; then
    echo "1. Upload credentials file to: $CRED_FILE"
    echo "   From local machine run:"
    echo "   scp /path/to/st-stt-v2.json ubuntu@your-server:$CRED_FILE"
fi

if [ -z "$MODULE_PATH" ]; then
    echo "2. Build and install the module:"
    echo "   cd modules/mod_google_transcribe"
    echo "   make && make install"
fi

echo ""
echo "3. After fixing issues, restart FreeSWITCH:"
echo "   systemctl daemon-reload"
echo "   systemctl restart freeswitch"
echo ""
