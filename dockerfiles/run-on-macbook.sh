#!/bin/bash
# ============================================================================
# Run FreeSWITCH Docker Image on MacBook
# ============================================================================
#
# Usage:
#   ./run-on-macbook.sh <docker-image-name> [DEEPGRAM_KEY] [AZURE_KEY] [AZURE_REGION] [AWS_ACCESS_KEY_ID] [AWS_SECRET_ACCESS_KEY] [AWS_REGION] [AWS_SESSION_TOKEN]
#
# Examples:
#   ./run-on-macbook.sh srt2011/freeswitch-base:latest
#   ./run-on-macbook.sh srt2011/freeswitch-mod-audio-fork:latest
#   ./run-on-macbook.sh srt2011/freeswitch-mod-deepgram-transcribe:latest
#   ./run-on-macbook.sh srt2011/freeswitch-mod-azure-transcribe:latest
#   ./run-on-macbook.sh srt2011/freeswitch-mod-aws-transcribe:latest
#
# With API keys for transcription:
#   ./run-on-macbook.sh srt2011/freeswitch-mod-deepgram-transcribe:latest YOUR_DEEPGRAM_KEY
#   ./run-on-macbook.sh srt2011/freeswitch-mod-azure-transcribe:latest "" YOUR_AZURE_KEY eastus
#   ./run-on-macbook.sh srt2011/freeswitch-mod-aws-transcribe:latest "" "" "" YOUR_AWS_ACCESS_KEY_ID YOUR_AWS_SECRET_ACCESS_KEY us-east-1
#   ./run-on-macbook.sh srt2011/freeswitch-mod-aws-transcribe:latest "" "" "" YOUR_AWS_ACCESS_KEY_ID YOUR_AWS_SECRET_ACCESS_KEY us-east-1 YOUR_SESSION_TOKEN
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
CONTAINER_NAME="freeswitch"

# Validation
if [ -z "$REMOTE_IMAGE" ]; then
    echo "❌ Error: Docker image name is required"
    echo ""
    echo "Usage: $0 <docker-image-name> [DEEPGRAM_KEY] [AZURE_KEY] [AZURE_REGION] [AWS_ACCESS_KEY_ID] [AWS_SECRET_ACCESS_KEY] [AWS_REGION] [AWS_SESSION_TOKEN]"
    echo ""
    echo "Examples:"
    echo "  $0 srt2011/freeswitch-base:latest"
    echo "  $0 srt2011/freeswitch-mod-audio-fork:latest"
    echo "  $0 srt2011/freeswitch-mod-deepgram-transcribe:latest"
    echo "  $0 srt2011/freeswitch-mod-azure-transcribe:latest"
    echo "  $0 srt2011/freeswitch-mod-aws-transcribe:latest"
    echo ""
    echo "With API keys:"
    echo "  $0 srt2011/freeswitch-mod-deepgram-transcribe:latest YOUR_DEEPGRAM_KEY"
    echo "  $0 srt2011/freeswitch-mod-azure-transcribe:latest \"\" YOUR_AZURE_KEY eastus"
    echo ""
    echo "AWS Transcribe - Permanent credentials (AKIA*):"
    echo "  $0 srt2011/freeswitch-mod-aws-transcribe:latest \"\" \"\" \"\" YOUR_AWS_ACCESS_KEY_ID YOUR_AWS_SECRET_ACCESS_KEY us-east-1"
    echo ""
    echo "AWS Transcribe - Temporary credentials (ASIA*):"
    echo "  $0 srt2011/freeswitch-mod-aws-transcribe:latest \"\" \"\" \"\" YOUR_AWS_ACCESS_KEY_ID YOUR_AWS_SECRET_ACCESS_KEY us-east-1 YOUR_SESSION_TOKEN"
    echo ""
    echo "AWS Transcribe - IAM Role (on EC2/ECS):"
    echo "  $0 srt2011/freeswitch-mod-aws-transcribe:latest"
    exit 1
fi

echo "============================================="
echo "FreeSWITCH MacBook Runner"
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

# Pull image from Docker Hub
echo "Step 1: Pulling image from Docker Hub..."
echo "This may take 5-10 minutes depending on your internet speed..."
docker pull "$REMOTE_IMAGE"
echo "✅ Image pulled successfully"
echo ""

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
echo "Transcription Modules"
echo "============================================="
MODULE_CHECK=$(docker exec "$CONTAINER_NAME" /usr/local/freeswitch/bin/fs_cli -x "show modules" 2>/dev/null | grep -E "audio_fork|deepgram|azure|aws" || echo "")
if [ -z "$MODULE_CHECK" ]; then
    echo "No transcription modules detected (base image)"
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

    echo ""
    echo "Total transcription modules loaded: $MODULE_COUNT"
fi
echo ""

# Module verification command
echo "============================================="
echo "Module Verification Command"
echo "============================================="
echo "To verify all modules are loaded, run:"
echo "  docker exec $CONTAINER_NAME fs_cli -x 'show modules' | grep -E 'audio_fork|deepgram|aws'"
echo ""
echo "Expected output (for all-modules image):"
echo "  api,uuid_audio_fork,mod_audio_fork,/usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so"
echo "  api,uuid_aws_transcribe,mod_aws_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so"
echo "  api,uuid_deepgram_transcribe,mod_deepgram_transcribe,/usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so"
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
echo "  Extension 1000: username=1000, password=1234"
echo "  Extension 1001: username=1001, password=1234"
echo "  Extension 1002: username=1002, password=1234 (Azure Transcribe enabled)"
echo "  Extension 1003: username=1003, password=1234 (AWS Transcribe enabled)"
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
echo "  2. Register extension 1000, 1001, 1002, or 1003"
echo "  3. Call from one extension to another (e.g., dial: 1001 from 1000)"
echo "  4. Test echo service (dial: 9196)"
echo "  5. Test transcription (use extension 1002 for Azure or 1003 for AWS)"

# Add transcription-specific instructions if modules are present
if [ -n "$MODULE_CHECK" ]; then
    echo ""
    echo "Transcription Module Usage:"
    if echo "$MODULE_CHECK" | grep -q "deepgram"; then
        echo "  Deepgram:"
        echo "    docker exec -it $CONTAINER_NAME fs_cli"
        echo "    freeswitch@internal> uuid_setvar <uuid> DEEPGRAM_API_KEY your-key"
        echo "    freeswitch@internal> uuid_deepgram_transcribe <uuid> start en-US interim"
    fi
    if echo "$MODULE_CHECK" | grep -q "azure"; then
        echo "  Azure:"
        echo "    docker exec -it $CONTAINER_NAME fs_cli"
        echo "    freeswitch@internal> uuid_setvar <uuid> AZURE_SUBSCRIPTION_KEY your-key"
        echo "    freeswitch@internal> uuid_setvar <uuid> AZURE_REGION eastus"
        echo "    freeswitch@internal> uuid_azure_transcribe <uuid> start en-US interim"
    fi
    if echo "$MODULE_CHECK" | grep -q "aws"; then
        echo "  AWS Transcribe:"
        if [ -n "$AWS_ACCESS_KEY_ID" ]; then
            # Already configured via environment variables
            if [[ "$AWS_ACCESS_KEY_ID" == ASIA* ]] && [ -z "$AWS_SESSION_TOKEN" ]; then
                echo "    ⚠️  WARNING: ASIA* credentials require AWS_SESSION_TOKEN!"
                echo "    Restart container with session token parameter"
            else
                echo "    ✓ Credentials configured via environment variables"
            fi
            echo "    docker exec -it $CONTAINER_NAME fs_cli"
            echo "    freeswitch@internal> uuid_aws_transcribe <uuid> start en-US interim stereo"
        else
            # Need to set credentials manually
            echo "    docker exec -it $CONTAINER_NAME fs_cli"
            echo ""
            echo "    For permanent credentials (AKIA*):"
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_ACCESS_KEY_ID AKIA..."
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_SECRET_ACCESS_KEY ..."
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_REGION us-east-1"
            echo ""
            echo "    For temporary credentials (ASIA*):"
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_ACCESS_KEY_ID ASIA..."
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_SECRET_ACCESS_KEY ..."
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_SESSION_TOKEN IQoJ..."
            echo "    freeswitch@internal> uuid_setvar <uuid> AWS_REGION us-east-1"
            echo ""
            echo "    Then start transcription:"
            echo "    freeswitch@internal> uuid_aws_transcribe <uuid> start en-US interim stereo"
        fi
    fi
fi

echo ""
echo "For detailed instructions, see:"
echo "  dockerfiles/DOCKER_HUB_DEPLOYMENT.md"
echo "  dockerfiles/README.md"
echo ""
