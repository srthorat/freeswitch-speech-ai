#!/bin/bash
# ============================================================================
# Build Script for FreeSWITCH with mod_google_transcribev2
# ============================================================================
#
# This script builds the FreeSWITCH Docker image with mod_google_transcribev2.
#
# Prerequisites:
#   1. Docker installed and running
#   2. freeswitch-base image built (or pulled from registry)
#
# Usage:
#   ./dockerfiles/docker-build-mod-google-transcribev2.sh [OPTIONS]
#
# Options:
#   --base-image IMAGE       Base image to use (default: srt2011/freeswitch-base:latest)
#   --google-version VER     Google Cloud C++ version (default: 2.30.0)
#   --cpus N                 Number of CPUs for build (default: 4)
#   --tag TAG                Docker image tag (default: freeswitch:mod-google-transcribev2)
#   --no-cache               Build without using cache
#   --help                   Show this help message
#
# Examples:
#   # Basic build with defaults
#   ./dockerfiles/docker-build-mod-google-transcribev2.sh
#
#   # Build with 8 CPUs and custom tag
#   ./dockerfiles/docker-build-mod-google-transcribev2.sh --cpus 8 --tag myrepo/freeswitch-google:v1.0
#
#   # Build with different Google Cloud C++ version
#   ./dockerfiles/docker-build-mod-google-transcribev2.sh --google-version 2.31.0
#
# ============================================================================

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default values
BASE_IMAGE="srt2011/freeswitch-base:latest"
GOOGLE_VERSION="2.30.0"
UBUNTU_VERSION="24.04"
BUILD_CPUS=4
IMAGE_TAG="freeswitch:mod-google-transcribev2"
NO_CACHE=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --base-image)
            BASE_IMAGE="$2"
            shift 2
            ;;
        --google-version)
            GOOGLE_VERSION="$2"
            shift 2
            ;;
        --cpus)
            BUILD_CPUS="$2"
            shift 2
            ;;
        --tag)
            IMAGE_TAG="$2"
            shift 2
            ;;
        --no-cache)
            NO_CACHE="--no-cache"
            shift
            ;;
        --help)
            head -n 35 "$0" | tail -n +2 | sed 's/^# //'
            exit 0
            ;;
        *)
            echo -e "${RED}Unknown option: $1${NC}"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

# Print build configuration
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}FreeSWITCH mod_google_transcribev2 Docker Build${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "Build Configuration:"
echo "  Base Image:           ${BASE_IMAGE}"
echo "  Google Cloud Version: ${GOOGLE_VERSION}"
echo "  Build CPUs:           ${BUILD_CPUS}"
echo "  Image Tag:            ${IMAGE_TAG}"
echo "  No Cache:             ${NO_CACHE:-disabled}"
echo "  Project Root:         ${PROJECT_ROOT}"
echo ""
echo -e "${BLUE}=============================================${NC}"
echo ""

# Check if Docker is running
if ! docker info > /dev/null 2>&1; then
    echo -e "${RED}❌ ERROR: Docker is not running${NC}"
    echo "Please start Docker and try again"
    exit 1
fi
echo -e "${GREEN}✅ Docker is running${NC}"
echo ""

# Check if base image exists
echo -e "${BLUE}Checking for base image: ${BASE_IMAGE}${NC}"
if docker image inspect "${BASE_IMAGE}" > /dev/null 2>&1; then
    echo -e "${GREEN}✅ Base image found${NC}"
    docker image inspect "${BASE_IMAGE}" --format '  Created: {{.Created}}' | head -1
    BASE_SIZE=$(docker image inspect "${BASE_IMAGE}" --format '{{.Size}}')
    echo "  Size: $(echo $BASE_SIZE | numfmt --to=iec)"
else
    echo -e "${YELLOW}⚠️  Base image not found locally${NC}"
    echo ""
    echo "The base image ${BASE_IMAGE} was not found."
    echo ""
    echo "Options:"
    echo "  1. Pull from registry:"
    echo "     docker pull ${BASE_IMAGE}"
    echo ""
    echo "  2. Build freeswitch-base first:"
    echo "     docker build -f dockerfiles/Dockerfile.freeswitch-base -t srt2011/freeswitch-base:latest ."
    echo ""
    read -p "Do you want to pull the base image now? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Pulling ${BASE_IMAGE}..."
        docker pull "${BASE_IMAGE}"
    else
        echo -e "${RED}❌ Cannot proceed without base image${NC}"
        exit 1
    fi
fi
echo ""

# Check if required files exist
echo -e "${BLUE}Checking required files...${NC}"
REQUIRED_FILES=(
    "${PROJECT_ROOT}/dockerfiles/Dockerfile.mod_google_transcribev2"
    "${PROJECT_ROOT}/modules/mod_google_transcribev2/Makefile"
    "${PROJECT_ROOT}/modules/mod_google_transcribev2/mod_google_transcribev2.c"
    "${PROJECT_ROOT}/modules/mod_google_transcribev2/google_transcribe_glue.cpp"
    "${PROJECT_ROOT}/modules/mod_google_transcribev2/speech.proto"
)

ALL_FILES_EXIST=true
for file in "${REQUIRED_FILES[@]}"; do
    if [ -f "$file" ]; then
        echo -e "${GREEN}✅${NC} $(basename "$file")"
    else
        echo -e "${RED}❌${NC} $(basename "$file") - NOT FOUND"
        ALL_FILES_EXIST=false
    fi
done

if [ "$ALL_FILES_EXIST" = false ]; then
    echo ""
    echo -e "${RED}❌ ERROR: Required files are missing${NC}"
    echo "Please ensure all module files exist before building"
    exit 1
fi
echo ""

# Confirm build
echo -e "${YELLOW}Ready to build Docker image${NC}"
echo ""
read -p "Continue with build? (Y/n) " -n 1 -r
echo
if [[ $REPLY =~ ^[Nn]$ ]]; then
    echo "Build cancelled"
    exit 0
fi
echo ""

# Start build
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}Starting Docker build...${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "Build command:"
echo "  docker build \\"
echo "    -f dockerfiles/Dockerfile.mod_google_transcribev2 \\"
echo "    -t ${IMAGE_TAG} \\"
echo "    --build-arg BASE_IMAGE=${BASE_IMAGE} \\"
echo "    --build-arg GOOGLE_CLOUD_CPP_VERSION=${GOOGLE_VERSION} \\"
echo "    --build-arg UBUNTU_VERSION=${UBUNTU_VERSION} \\"
echo "    --build-arg BUILD_CPUS=${BUILD_CPUS} \\"
echo "    ${NO_CACHE} \\"
echo "    ."
echo ""

# Record start time
START_TIME=$(date +%s)

# Run docker build
cd "${PROJECT_ROOT}"
docker build \
    -f dockerfiles/Dockerfile.mod_google_transcribev2 \
    -t "${IMAGE_TAG}" \
    --build-arg BASE_IMAGE="${BASE_IMAGE}" \
    --build-arg GOOGLE_CLOUD_CPP_VERSION="${GOOGLE_VERSION}" \
    --build-arg UBUNTU_VERSION="${UBUNTU_VERSION}" \
    --build-arg BUILD_CPUS="${BUILD_CPUS}" \
    ${NO_CACHE} \
    .

# Record end time
END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
MINUTES=$((DURATION / 60))
SECONDS=$((DURATION % 60))

# Build complete
echo ""
echo -e "${BLUE}=============================================${NC}"
echo -e "${GREEN}✅ Build Complete!${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "Image: ${IMAGE_TAG}"
echo "Build time: ${MINUTES}m ${SECONDS}s"
echo ""

# Show image details
echo "Image details:"
docker image inspect "${IMAGE_TAG}" --format '  Created: {{.Created}}'
IMAGE_SIZE=$(docker image inspect "${IMAGE_TAG}" --format '{{.Size}}')
echo "  Size: $(echo $IMAGE_SIZE | awk '{printf "%.2f GB", $1/1024/1024/1024}')"
docker image inspect "${IMAGE_TAG}" --format '  Google Cloud C++ Version: {{index .Config.Labels "google.cloud.cpp.version"}}'
echo ""

# Next steps
echo -e "${BLUE}=============================================${NC}"
echo -e "${BLUE}Next Steps${NC}"
echo -e "${BLUE}=============================================${NC}"
echo ""
echo "1. Test the image:"
echo "   docker run -it --rm ${IMAGE_TAG} fs_cli -x \"version\""
echo ""
echo "2. Start the container:"
echo "   docker run -d --name freeswitch-google \\"
echo "     -p 5060:5060/udp -p 8021:8021 \\"
echo "     ${IMAGE_TAG}"
echo ""
echo "3. Configure Google Cloud credentials (without restarting container):"
echo ""
echo "   a. Copy your credentials file to the container:"
echo "      docker cp /path/to/google-creds.json freeswitch-google:/etc/freeswitch/google-creds.json"
echo ""
echo "   b. Edit supervisor config inside container:"
echo "      docker exec -it freeswitch-google bash"
echo "      vi /etc/supervisor/conf.d/freeswitch-google.conf"
echo ""
echo "   c. Update the environment line with your Google Cloud settings:"
echo "      environment=LD_LIBRARY_PATH=\"/usr/local/lib:/usr/local/freeswitch/lib\",GOOGLE_APPLICATION_CREDENTIALS=\"/etc/freeswitch/google-creds.json\",GOOGLE_PROJECT_ID=\"your-project-id\",GCP_LOCATION=\"us-central1\""
echo ""
echo "   d. Restart FreeSWITCH process (not container):"
echo "      supervisorctl restart freeswitch"
echo "      exit"
echo ""
echo "4. Verify the module is loaded:"
echo "   docker exec -it freeswitch-google fs_cli -x \"show modules\" | grep google"
echo ""
echo "5. Test transcription (replace <uuid> with actual call UUID):"
echo "   docker exec -it freeswitch-google fs_cli"
echo "   freeswitch@internal> uuid_google_transcribev2 <uuid> start en-US"
echo ""
echo -e "${BLUE}=============================================${NC}"
