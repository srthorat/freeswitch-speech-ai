#!/bin/bash
# ============================================================================
# Push FreeSWITCH Base Image to Docker Hub
# ============================================================================
#
# Usage:
#   ./push-to-dockerhub.sh <dockerhub-username> [image-version]
#
# Example:
#   ./push-to-dockerhub.sh johndoe 1.10.11
#
# ============================================================================

set -e

DOCKERHUB_USERNAME=${1}
IMAGE_VERSION=${2:-1.10.11}
LOCAL_IMAGE="freeswitch-base:${IMAGE_VERSION}"
REMOTE_IMAGE="${DOCKERHUB_USERNAME}/freeswitch-base"

# Validation
if [ -z "$DOCKERHUB_USERNAME" ]; then
    echo "Error: Docker Hub username is required"
    echo ""
    echo "Usage: $0 <dockerhub-username> [image-version]"
    echo "Example: $0 johndoe 1.10.11"
    exit 1
fi

echo "============================================="
echo "Docker Hub Push Script"
echo "============================================="
echo ""
echo "Docker Hub Username: $DOCKERHUB_USERNAME"
echo "Local Image: $LOCAL_IMAGE"
echo "Remote Image: $REMOTE_IMAGE:$IMAGE_VERSION"
echo "Remote Image (latest): $REMOTE_IMAGE:latest"
echo ""

# Check if local image exists
if ! docker images | grep -q "freeswitch-base.*$IMAGE_VERSION"; then
    echo "❌ Error: Local image '$LOCAL_IMAGE' not found!"
    echo ""
    echo "Please build the image first:"
    echo "  ./dockerfiles/build-freeswitch-base.sh"
    exit 1
fi

echo "✅ Local image found"
echo ""

# Check Docker login status
echo "Checking Docker Hub authentication..."
if ! docker info 2>/dev/null | grep -q "Username"; then
    echo "⚠️  Not logged into Docker Hub"
    echo ""
    echo "Logging in to Docker Hub..."
    docker login
else
    echo "✅ Already logged into Docker Hub"
fi
echo ""

# Tag for Docker Hub
echo "Step 1: Tagging image for Docker Hub..."
docker tag "$LOCAL_IMAGE" "${REMOTE_IMAGE}:${IMAGE_VERSION}"
docker tag "$LOCAL_IMAGE" "${REMOTE_IMAGE}:latest"
echo "✅ Tagged as:"
echo "  - ${REMOTE_IMAGE}:${IMAGE_VERSION}"
echo "  - ${REMOTE_IMAGE}:latest"
echo ""

# Get image size
IMAGE_SIZE=$(docker images "$LOCAL_IMAGE" --format "{{.Size}}")
echo "Image size: $IMAGE_SIZE"
echo ""

# Push versioned tag
echo "Step 2: Pushing versioned tag (${REMOTE_IMAGE}:${IMAGE_VERSION})..."
echo "This may take 5-15 minutes depending on your upload speed..."
docker push "${REMOTE_IMAGE}:${IMAGE_VERSION}"
echo "✅ Pushed: ${REMOTE_IMAGE}:${IMAGE_VERSION}"
echo ""

# Push latest tag
echo "Step 3: Pushing latest tag (${REMOTE_IMAGE}:latest)..."
docker push "${REMOTE_IMAGE}:latest"
echo "✅ Pushed: ${REMOTE_IMAGE}:latest"
echo ""

echo "============================================="
echo "✅ Successfully pushed to Docker Hub!"
echo "============================================="
echo ""
echo "View your image at:"
echo "  https://hub.docker.com/r/${DOCKERHUB_USERNAME}/freeswitch-base"
echo ""
echo "To pull on another machine:"
echo "  docker pull ${REMOTE_IMAGE}:${IMAGE_VERSION}"
echo ""
echo "To run on MacBook:"
echo "  docker run -d \\"
echo "      --name freeswitch \\"
echo "      --platform linux/amd64 \\"
echo "      -p 5060:5060/tcp \\"
echo "      -p 5060:5060/udp \\"
echo "      -p 5080:5080/tcp \\"
echo "      -p 5080:5080/udp \\"
echo "      -p 8021:8021/tcp \\"
echo "      -p 16384-16484:16384-16484/udp \\"
echo "      ${REMOTE_IMAGE}:${IMAGE_VERSION}"
echo ""
echo "For detailed testing instructions, see:"
echo "  dockerfiles/DOCKER_HUB_DEPLOYMENT.md"
echo ""
