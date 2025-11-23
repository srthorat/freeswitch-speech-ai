#!/bin/bash
# ============================================================================
# Run FreeSWITCH Docker Image with ALL 5 Modules on MacBook
# ============================================================================
#
# Usage:
#   ./run-all-modules.sh <docker-image-name> [DEEPGRAM_KEY] [AZURE_KEY] [AZURE_REGION] [AWS_ACCESS_KEY_ID] [AWS_SECRET_ACCESS_KEY] [AWS_REGION] [AWS_SESSION_TOKEN] [GOOGLE_CREDENTIALS_PATH]
#
# Examples:
#   ./run-all-modules.sh freeswitch-speech-ai:all-modules
#
#   With all API keys:
#   ./run-all-modules.sh freeswitch-speech-ai:all-modules \
#     YOUR_DEEPGRAM_KEY \
#     YOUR_AZURE_KEY eastus \
#     YOUR_AWS_ACCESS_KEY_ID YOUR_AWS_SECRET_ACCESS_KEY us-east-1 \
#     "" \
#     /path/to/google-credentials.json
#
# ============================================================================

set -e

REMOTE_IMAGE=${1}
DEEPGRAM_API_KEY=${2:-""}
AZURE_SUBSCRIPTION_KEY=${3:-""}
AZURE_REGION=${4:-"eastus"}
AWS_ACCESS_KEY_ID=${5:-""}
AWS_SECRET_ACCESS_KEY=${6:-""}
AWS_REGION=${7:-"us-east-1"}
AWS_SESSION_TOKEN=${8:-""}
GOOGLE_CREDENTIALS_PATH=${9:-""}
CONTAINER_NAME="freeswitch"

# Validation
if [ -z "$REMOTE_IMAGE" ]; then
    echo "❌ Error: Docker image name is required"
    echo ""
    echo "Usage: $0 <docker-image-name> [DEEPGRAM_KEY] [AZURE_KEY] [AZURE_REGION] [AWS_ACCESS_KEY_ID] [AWS_SECRET_ACCESS_KEY] [AWS_REGION] [AWS_SESSION_TOKEN] [GOOGLE_CREDENTIALS_PATH]"
    echo ""
    echo "Examples:"
    echo "  $0 freeswitch-speech-ai:all-modules"
    echo ""
    echo "With all API keys:"
    echo "  $0 freeswitch-speech-ai:all-modules \\"
    echo "    YOUR_DEEPGRAM_KEY \\"
    echo "    YOUR_AZURE_KEY eastus \\"
    echo "    YOUR_AWS_ACCESS_KEY_ID YOUR_AWS_SECRET_ACCESS_KEY us-east-1 \\"
    echo "    \"\" \\"
    echo "    /path/to/google-credentials.json"
    exit 1
fi

echo "============================================="
echo "FreeSWITCH All Modules Runner"
echo "============================================="
echo ""
echo "Image: $REMOTE_IMAGE"
echo "Container: $CONTAINER_NAME"

# Show API key configuration
if [ -n "$DEEPGRAM_API_KEY" ]; then
    echo "Deepgram API Key: ${DEEPGRAM_API_KEY:0:10}... (configured)"
fi
if [ -n "$AZURE_SUBSCRIPTION_KEY" ]; then
    echo "Azure Subscription Key: ${AZURE_SUBSCRIPTION_KEY:0:10}... (configured)"
    echo "Azure Region: $AZURE_REGION"
fi
if [ -n "$AWS_ACCESS_KEY_ID" ]; then
    # Detect credential type
    if [[ "$AWS_ACCESS_KEY_ID" == AKIA* ]]; then
        echo "AWS Credentials: Permanent (AKIA*)"
    elif [[ "$AWS_ACCESS_KEY_ID" == ASIA* ]]; then
        if [ -n "$AWS_SESSION_TOKEN" ]; then
            echo "AWS Credentials: Temporary (ASIA* + session token)"
        else
            echo "⚠️  AWS Credentials: Temporary (ASIA* - MISSING SESSION TOKEN!)"
        fi
    else
        echo "AWS Credentials: Configured"
    fi
    echo "  Access Key ID: ${AWS_ACCESS_KEY_ID:0:10}..."
    echo "  Region: $AWS_REGION"
    if [ -n "$AWS_SESSION_TOKEN" ]; then
        echo "  Session Token: ${AWS_SESSION_TOKEN:0:20}... (present)"
    fi
fi
if [ -n "$GOOGLE_CREDENTIALS_PATH" ]; then
    if [ -f "$GOOGLE_CREDENTIALS_PATH" ]; then
        echo "Google Credentials: $GOOGLE_CREDENTIALS_PATH (found)"
    else
        echo "⚠️  Google Credentials: $GOOGLE_CREDENTIALS_PATH (NOT FOUND!)"
    fi
fi
echo ""

# Check if Docker is running
if ! docker info >/dev/null 2>&1; then
    echo "❌ Error: Docker is not running!"
    echo ""
    echo "Please start Docker Desktop and try again."
    exit 1
fi

echo "✅ Docker is running"
echo ""

# Stop and remove existing container if it exists
if docker ps -a | grep -q "$CONTAINER_NAME"; then
    echo "⚠️  Existing container found. Removing..."
    docker stop "$CONTAINER_NAME" 2>/dev/null || true
    docker rm "$CONTAINER_NAME" 2>/dev/null || true
    echo "✅ Old container removed"
    echo ""
fi

# Pull image if it's a remote image
if [[ "$REMOTE_IMAGE" == *"/"* ]]; then
    echo "Step 1: Pulling image from Docker Hub..."
    echo "This may take 5-10 minutes depending on your internet speed..."
    docker pull "$REMOTE_IMAGE"
    echo "✅ Image pulled successfully"
    echo ""
else
    echo "Step 1: Using local image..."
    echo ""
fi

# Run container
echo "Step 2: Starting FreeSWITCH container..."

# Build docker run command with optional environment variables
DOCKER_CMD="docker run -d \
    --name $CONTAINER_NAME \
    --platform linux/amd64 \
    --net=host \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 5080:5080/udp \
    -p 18021:8021/tcp \
    -p 16384-16484:16384-16484/udp"

# Add API keys if provided
if [ -n "$DEEPGRAM_API_KEY" ]; then
    DOCKER_CMD="$DOCKER_CMD -e DEEPGRAM_API_KEY=$DEEPGRAM_API_KEY"
fi
if [ -n "$AZURE_SUBSCRIPTION_KEY" ]; then
    DOCKER_CMD="$DOCKER_CMD -e AZURE_SUBSCRIPTION_KEY=$AZURE_SUBSCRIPTION_KEY"
    DOCKER_CMD="$DOCKER_CMD -e AZURE_REGION=$AZURE_REGION"
fi
if [ -n "$AWS_ACCESS_KEY_ID" ]; then
    DOCKER_CMD="$DOCKER_CMD -e AWS_ACCESS_KEY_ID=$AWS_ACCESS_KEY_ID"
    DOCKER_CMD="$DOCKER_CMD -e AWS_SECRET_ACCESS_KEY=$AWS_SECRET_ACCESS_KEY"
    DOCKER_CMD="$DOCKER_CMD -e AWS_REGION=$AWS_REGION"
    # Optional session token for temporary credentials (ASIA* keys)
    if [ -n "$AWS_SESSION_TOKEN" ]; then
        DOCKER_CMD="$DOCKER_CMD -e AWS_SESSION_TOKEN=$AWS_SESSION_TOKEN"
    fi
fi
if [ -n "$GOOGLE_CREDENTIALS_PATH" ] && [ -f "$GOOGLE_CREDENTIALS_PATH" ]; then
    DOCKER_CMD="$DOCKER_CMD -v $GOOGLE_CREDENTIALS_PATH:/etc/google/credentials.json:ro"
    DOCKER_CMD="$DOCKER_CMD -e GOOGLE_APPLICATION_CREDENTIALS=/etc/google/credentials.json"
fi

DOCKER_CMD="$DOCKER_CMD $REMOTE_IMAGE"

# Execute docker run
eval $DOCKER_CMD

echo "✅ Container started"
echo ""

# Wait for FreeSWITCH to start
echo "Step 3: Waiting for FreeSWITCH to start (30 seconds)..."
sleep 30

# Check if container is still running
if ! docker ps | grep -q "$CONTAINER_NAME"; then
    echo "❌ Error: Container stopped unexpectedly!"
    echo ""
    echo "Container logs:"
    docker logs "$CONTAINER_NAME"
    exit 1
fi

echo "✅ Container is running"
echo ""

# Test fs_cli connection
echo "Step 4: Testing fs_cli connection..."
if docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "status" >/dev/null 2>&1; then
    echo "✅ fs_cli connected successfully"
else
    echo "⚠️  Warning: fs_cli connection failed (FreeSWITCH may still be starting)"
fi
echo ""

# Show FreeSWITCH status
echo "============================================="
echo "FreeSWITCH Status"
echo "============================================="
docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "status" || echo "Status command failed"
echo ""

# Show SIP profiles
echo "============================================="
echo "SIP Profiles"
echo "============================================="
docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "sofia status" || echo "Sofia status failed"
echo ""

# Check for transcription modules
echo "============================================="
echo "Transcription Modules (ALL 5)"
echo "============================================="
MODULE_CHECK=$(docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "show modules" 2>/dev/null | grep -E "audio_fork|deepgram|azure|aws|google" || echo "")
if [ -z "$MODULE_CHECK" ]; then
    echo "⚠️  No transcription modules detected"
else
    echo "$MODULE_CHECK"
    echo ""

    # Count loaded modules
    MODULE_COUNT=$(echo "$MODULE_CHECK" | wc -l)

    # Check each module type
    AUDIO_FORK=$(echo "$MODULE_CHECK" | grep "audio_fork" || echo "")
    DEEPGRAM=$(echo "$MODULE_CHECK" | grep "deepgram" || echo "")
    AZURE=$(echo "$MODULE_CHECK" | grep "azure" || echo "")
    AWS=$(echo "$MODULE_CHECK" | grep "aws" || echo "")
    GOOGLE=$(echo "$MODULE_CHECK" | grep "google" || echo "")

    echo "Module Status:"
    if [ -n "$AUDIO_FORK" ]; then
        echo "  ✅ mod_audio_fork loaded"
    fi
    if [ -n "$DEEPGRAM" ]; then
        if [ -n "$DEEPGRAM_API_KEY" ]; then
            echo "  ✅ mod_deepgram_transcribe loaded and configured"
        else
            echo "  ⚠️  mod_deepgram_transcribe loaded (API key not configured)"
        fi
    fi
    if [ -n "$AZURE" ]; then
        if [ -n "$AZURE_SUBSCRIPTION_KEY" ]; then
            echo "  ✅ mod_azure_transcribe loaded and configured"
        else
            echo "  ⚠️  mod_azure_transcribe loaded (API key not configured)"
        fi
    fi
    if [ -n "$AWS" ]; then
        if [ -n "$AWS_ACCESS_KEY_ID" ]; then
            echo "  ✅ mod_aws_transcribe loaded and configured"
        else
            echo "  ⚠️  mod_aws_transcribe loaded (credentials not configured)"
        fi
    fi
    if [ -n "$GOOGLE" ]; then
        if [ -n "$GOOGLE_CREDENTIALS_PATH" ]; then
            echo "  ✅ mod_google_transcribe loaded and configured"
        else
            echo "  ⚠️  mod_google_transcribe loaded (credentials not configured)"
        fi
    fi

    echo ""
    echo "Total transcription modules loaded: $MODULE_COUNT / 5"
fi
echo ""

# Get MacBook IP
echo "============================================="
echo "Network Information"
echo "============================================="
echo "Container IP: $(docker inspect "$CONTAINER_NAME" | grep '"IPAddress"' | head -1 | awk -F'"' '{print $4}')"
echo ""
echo "MacBook IP addresses:"
ifconfig | grep "inet " | grep -v 127.0.0.1 | awk '{print "  - " $2}'
echo ""
echo "For SIP clients on the same MacBook, use: localhost or 127.0.0.1"
echo "For SIP clients on other devices, use one of the MacBook IPs above"
echo ""

echo "============================================="
echo "✅ FreeSWITCH is Ready!"
echo "============================================="
echo ""
echo "Container Name: $CONTAINER_NAME"
echo ""
echo "Extension Credentials:"
echo "  Extension 1000: username=1000, password=1234 (Audio Fork enabled)"
echo "  Extension 1001: username=1001, password=1234 (Deepgram enabled)"
echo "  Extension 1002: username=1002, password=1234 (Azure enabled)"
echo "  Extension 1003: username=1003, password=1234 (AWS enabled)"
echo "  Extension 1004: username=1004, password=1234 (Google enabled)"
echo ""
echo "SIP Server: localhost:5060 (UDP)"
echo ""
echo "Useful Commands:"
echo "  - Access fs_cli:       docker exec -it $CONTAINER_NAME fs_cli"
echo "  - View logs:           docker logs -f $CONTAINER_NAME"
echo "  - Stop container:      docker stop $CONTAINER_NAME"
echo "  - Restart container:   docker restart $CONTAINER_NAME"
echo "  - Remove container:    docker rm -f $CONTAINER_NAME"
echo ""
echo "Next Steps:"
echo "  1. Install a SIP client (Zoiper, Linphone, etc.)"
echo "  2. Register an extension (1000-1004)"
echo "  3. Call from one extension to another"
echo "  4. Test echo service (dial: 9196)"
echo "  5. Test transcription with configured extensions"
echo ""
echo "For detailed instructions, see:"
echo "  docs/DIALPLAN_CHANNEL_VARIABLES_PUSHER.md"
echo "  docs/SPEAKER_DETECTION_COMPARISON.md"
echo ""
