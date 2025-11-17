# Installation Guide

This guide explains how to build and run FreeSWITCH with transcription modules on different platforms.

## Quick Start: Choose Your Platform

| Platform | Build Time | Recommended For | Link |
|----------|-----------|-----------------|------|
| **Linux Standalone** | 90-150 min | Production, servers, Linux desktops | [→ Option 1](#option-1-linux-standalone-ubuntudebian) |
| **GitHub Codespaces** | 60-120 min | Apple Silicon Macs, quick testing | [→ Option 2](#option-2-github-codespaces-recommended-for-apple-silicon) |
| **MacBook with Docker** | 60-120 min (Intel)<br>2-3 hours (Apple Silicon) | Intel Macs, local development | [→ Option 3](#option-3-macbook-with-docker-local-build) |

**Recommendation:**
- 🍎 **Apple Silicon (M1/M2/M3)?** → Use **GitHub Codespaces** (faster, easier)
- 🐧 **Linux server/desktop?** → Use **Linux Standalone** (best performance)
- 💻 **Intel Mac?** → Use **MacBook with Docker** (native performance)

---

## Option 1: Linux Standalone (Ubuntu/Debian)

### Prerequisites
- Ubuntu 20.04 or later / Debian 10 or later
- Root access (sudo)
- At least 16GB RAM
- At least 20GB free disk space

### Step 1: Clone Repository

```bash
# Clone the repository
git clone https://github.com/srthorat/freeswitch_modules.git
cd freeswitch_modules

# Checkout your desired branch (optional)
# git checkout <branch-name>
```

### Step 2: Review Configuration

Check the `.env` file for version settings:

```bash
cat .env
```

The file contains versions for all dependencies (CMake, gRPC, AWS SDK, FreeSWITCH, etc).

### Step 3: Build All Dependencies and FreeSWITCH

**Full build (all 7 batches):**

```bash
sudo ./build-batch.sh all
```

**Or build in stages** (recommended for first-time build):

```bash
# Batch 1: System Dependencies + CMake (5-10 min)
sudo ./build-batch.sh 1

# Batch 2: gRPC + Protocol Buffers (15-30 min)
sudo ./build-batch.sh 2

# Batch 3: googleapis + libwebsockets (5-10 min)
sudo ./build-batch.sh 3

# Batch 4: Azure Speech SDK (1-2 min)
sudo ./build-batch.sh 4

# Batch 5: spandsp + sofia-sip + libfvad (10-15 min)
sudo ./build-batch.sh 5

# Batch 6: AWS SDK C++ + AWS C Common (20-40 min)
sudo ./build-batch.sh 6

# Batch 7: FreeSWITCH + Modules (20-30 min)
sudo ./build-batch.sh 7
```

**Total build time:** ~90-150 minutes (varies by CPU)

### Step 4: View Build Logs

All build output is saved to `build-logs/` directory:

```bash
ls -lh build-logs/
cat build-logs/batch-7.log  # View FreeSWITCH build log
```

### Step 5: Verify Installation

Check that FreeSWITCH and modules are installed:

```bash
# Check FreeSWITCH binary
/usr/local/freeswitch/bin/freeswitch -version

# Check transcription modules
ls -lh /usr/local/freeswitch/mod/mod_*_transcribe.so
ls -lh /usr/local/freeswitch/mod/mod_audio_fork.so
```

You should see:
- `mod_audio_fork.so`
- `mod_aws_transcribe.so`
- `mod_azure_transcribe.so`
- `mod_deepgram_transcribe.so`
- `mod_google_transcribe.so`

### Step 6: Run FreeSWITCH

```bash
# Start FreeSWITCH in foreground (for testing)
sudo /usr/local/freeswitch/bin/freeswitch -nc -nonat

# Or start in background
sudo /usr/local/freeswitch/bin/freeswitch -nc -nonat -u freeswitch -g freeswitch
```

---

## Option 2: GitHub Codespaces (Recommended for Apple Silicon)

GitHub Codespaces provides a cloud-based Linux development environment, perfect for building Docker images without local emulation overhead.

### Prerequisites
- GitHub account
- Repository access to srthorat/freeswitch_modules

### Step 1: Create Codespace

1. **Go to the repository:** https://github.com/srthorat/freeswitch_modules
2. **Click the green "Code" button**
3. **Click "Codespaces" tab**
4. **Click "Create codespace on [branch-name]"**
   - Or click the "+" to create on current branch

GitHub will provision a cloud VM with:
- 4 cores, 8GB RAM (free tier)
- Ubuntu Linux (x86_64)
- Docker pre-installed
- VS Code in browser

### Step 2: Build Docker Image in Codespace

Once your Codespace is ready:

```bash
# You're already in the repository directory
# Build the Docker image (60-120 minutes)
./docker-build.sh

# Monitor the build
# (Codespaces won't timeout during active builds)
```

### Step 3: Test the Container

```bash
# Run FreeSWITCH in the Codespace
docker run -d \
  --name freeswitch \
  -p 5060:5060/tcp \
  -p 5060:5060/udp \
  -p 8021:8021 \
  freeswitch-transcribe:latest

# Check logs
docker logs -f freeswitch

# Access fs_cli
docker exec -it freeswitch fs_cli
```

### Step 4: Export Docker Image (Optional)

If you want to use the image locally:

**Option A: Push to Docker Hub**
```bash
# Login to Docker Hub
docker login

# Tag the image
docker tag freeswitch-transcribe:latest yourusername/freeswitch-transcribe:latest

# Push to Docker Hub
docker push yourusername/freeswitch-transcribe:latest

# Then pull on your local machine
docker pull yourusername/freeswitch-transcribe:latest
```

**Option B: Export to tar file (not recommended - very large)**
```bash
# Export image to tar
docker save freeswitch-transcribe:latest -o freeswitch-transcribe.tar

# Download via browser or gh CLI
# Note: File will be ~1.5GB+
```

**Option C: Push to GitHub Container Registry**
```bash
# Login to GitHub Container Registry
echo $GITHUB_TOKEN | docker login ghcr.io -u USERNAME --password-stdin

# Tag the image
docker tag freeswitch-transcribe:latest ghcr.io/srthorat/freeswitch-transcribe:latest

# Push to GHCR
docker push ghcr.io/srthorat/freeswitch-transcribe:latest

# Pull on your local machine
docker pull ghcr.io/srthorat/freeswitch-transcribe:latest
```

### Codespace Tips

**Resource Limits:**
- Free tier: 120 core-hours/month (4-core = 30 hours of usage)
- Build takes ~1.5-2 hours
- Codespaces auto-pause after 30 min of inactivity

**Keep Codespace Active:**
```bash
# Run this in a terminal to prevent auto-pause during long builds
while true; do echo "keepalive: $(date)"; sleep 300; done &
```

**Check Build Progress:**
```bash
# In another terminal, monitor Docker
docker ps
docker stats

# Check build logs
tail -f build-logs/batch-*.log
```

**Delete Codespace When Done:**
- Go to https://github.com/codespaces
- Click "..." next to your Codespace
- Click "Delete"

---

## Option 3: MacBook with Docker (Local Build)

### Prerequisites
- macOS 10.15 or later
- Docker Desktop for Mac installed
- At least 16GB RAM allocated to Docker
- At least 20GB free disk space

**Important for Apple Silicon (M1/M2/M3) Macs:**
- The build uses x86_64 architecture (linux/amd64) with emulation
- Build time will be significantly longer (~2-3 hours due to emulation)
- The `docker-build.sh` script automatically handles platform detection
- If you encounter Rosetta errors, ensure Docker Desktop's "Use Rosetta for x86/amd64 emulation" is enabled in Settings > Features in Development

### Step 1: Clone Repository

```bash
# Clone the repository
git clone https://github.com/srthorat/freeswitch_modules.git
cd freeswitch_modules

# Checkout your desired branch (optional)
# git checkout <branch-name>
```

### Step 2: Build Docker Image

The Dockerfile uses local files (modules, patches, configs) from your current directory, so make sure you're building from the repository root.

```bash
# Build the Docker image (this takes 60-120 minutes)
./docker-build.sh

# Or specify a custom image name
./docker-build.sh my-freeswitch:v1.0
```

The `docker-build.sh` script automatically reads version configuration from the `.env` file and passes them as build arguments to Docker.

**Advanced: Manual build** (if you prefer not to use the script):
```bash
docker build \
  --build-arg CMAKE_VERSION=3.26.4 \
  --build-arg GRPC_VERSION=1.56.2 \
  --build-arg LIBWEBSOCKETS_VERSION=4.3.2 \
  --build-arg SPEECH_SDK_VERSION=1.37.0 \
  --build-arg SPANDSP_VERSION=3.0.0 \
  --build-arg SOFIA_VERSION=1.13.17 \
  --build-arg AWS_SDK_CPP_VERSION=1.11.160 \
  --build-arg FREESWITCH_VERSION=1.10.11 \
  -t freeswitch-transcribe:latest .
```

The Dockerfile:
- Uses multi-stage build for optimization
- Builds all dependencies from source
- Uses local modules/patches (no git clone needed)
- Includes module validation
- Final image is Debian-based (~1.5GB)

### Step 3: Run Docker Container

**Interactive mode (for testing):**

```bash
docker run -it --rm \
  --name freeswitch \
  -p 5060:5060/tcp \
  -p 5060:5060/udp \
  -p 5080:5080/tcp \
  -p 5080:5080/udp \
  -p 8021:8021 \
  -p 16384-16394:16384-16394/udp \
  freeswitch-transcribe:latest
```

**Background mode (daemon):**

```bash
docker run -d \
  --name freeswitch \
  --restart unless-stopped \
  -p 5060:5060/tcp \
  -p 5060:5060/udp \
  -p 5080:5080/tcp \
  -p 5080:5080/udp \
  -p 8021:8021 \
  -p 16384-16394:16384-16394/udp \
  freeswitch-transcribe:latest
```

**With custom configuration (mount volumes):**

```bash
docker run -d \
  --name freeswitch \
  --restart unless-stopped \
  -v $(pwd)/custom-config:/etc/freeswitch \
  -p 5060:5060/tcp \
  -p 5060:5060/udp \
  -p 5080:5080/tcp \
  -p 5080:5080/udp \
  -p 8021:8021 \
  -p 16384-16394:16384-16394/udp \
  freeswitch-transcribe:latest
```

### Step 4: Access FreeSWITCH Console

```bash
# View logs
docker logs -f freeswitch

# Access fs_cli
docker exec -it freeswitch fs_cli

# Execute commands
docker exec freeswitch fs_cli -x "show modules" | grep transcribe
```

### Step 5: Stop Container

```bash
# Stop gracefully
docker stop freeswitch

# Remove container
docker rm freeswitch
```

---

## Docker on Linux

The Docker instructions above also work on Linux. Just install Docker on your Linux machine:

```bash
# Install Docker on Ubuntu/Debian
curl -fsSL https://get.docker.com -o get-docker.sh
sudo sh get-docker.sh

# Add your user to docker group (optional, to run without sudo)
sudo usermod -aG docker $USER
newgrp docker

# Then follow the same Docker steps as MacBook
```

---

## Clean Rebuild

If you need to rebuild from scratch:

### Linux Standalone

```bash
# Clean all batches and rebuild
sudo ./build-batch.sh all --clean

# Or clean specific batch
sudo ./build-batch.sh 7 --clean  # Clean only FreeSWITCH
```

### Docker

```bash
# Remove old image
docker rmi freeswitch-transcribe:latest

# Build fresh (no cache)
# Note: Add --no-cache flag to docker-build.sh if needed
docker build --no-cache \
  --build-arg CMAKE_VERSION=$(grep cmakeVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg GRPC_VERSION=$(grep grpcVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg LIBWEBSOCKETS_VERSION=$(grep libwebsocketsVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg SPEECH_SDK_VERSION=$(grep speechSdkVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg SPANDSP_VERSION=$(grep spandspVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg SOFIA_VERSION=$(grep sofiaVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg AWS_SDK_CPP_VERSION=$(grep awsSdkCppVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  --build-arg FREESWITCH_VERSION=$(grep freeswitchVersion .env | awk -F '=' '{print $2}' | awk '{print $1}') \
  -t freeswitch-transcribe:latest .

# Or simply rebuild with the script (uses cache)
./docker-build.sh
```

---

## Complete Cleanup

To completely remove FreeSWITCH from your system:

### Linux Standalone

```bash
# Use the cleanup script
sudo ./cleanup-freeswitch.sh
```

This removes:
- FreeSWITCH installation (`/usr/local/freeswitch`)
- FreeSWITCH binaries
- FreeSWITCH libraries
- System services

---

## Ports Reference

| Port | Protocol | Description |
|------|----------|-------------|
| 5060 | TCP/UDP | SIP signaling |
| 5080 | TCP/UDP | SIP signaling (alternative) |
| 8021 | TCP | Event Socket (mod_event_socket) |
| 16384-16394 | UDP | RTP media (can be expanded) |

---

## Troubleshooting

### Build Failures

1. **Check build logs:**
   ```bash
   cat build-logs/batch-N.log
   ```

2. **Clean and retry:**
   ```bash
   sudo ./build-batch.sh N --clean
   ```

3. **Check disk space:**
   ```bash
   df -h /usr/local/src
   ```

### Module Loading Issues

1. **Check module dependencies:**
   ```bash
   ldd /usr/local/freeswitch/mod/mod_aws_transcribe.so
   ```

2. **Check library path:**
   ```bash
   echo $LD_LIBRARY_PATH
   ldconfig -p | grep libaws
   ```

### Docker Issues

1. **Build fails with "404 Not Found" or empty versions:**

   This means build arguments aren't being passed. Always use `docker-build.sh`:
   ```bash
   ./docker-build.sh
   ```

   Or manually pass all build args (see "Advanced: Manual build" section above).

2. **Apple Silicon: "rosetta error: failed to open elf" or Trace/breakpoint trap:**

   This occurs on M1/M2/M3 Macs when Rosetta emulation fails. Fix:

   a. Enable Rosetta in Docker Desktop:
      - Open Docker Desktop → Settings → Features in Development
      - Enable "Use Rosetta for x86/amd64 emulation on Apple Silicon"
      - Restart Docker Desktop

   b. Or use Docker's built-in emulation (slower):
      - The `docker-build.sh` script already sets `--platform linux/amd64`
      - Build time will be 2-3x longer

   c. Alternative: Use a cloud VM or Intel Mac for faster builds

3. **Check Docker logs:**
   ```bash
   docker logs freeswitch
   ```

4. **Inspect container:**
   ```bash
   docker exec -it freeswitch bash
   ```

5. **Rebuild without cache:**

   First remove the old image, then rebuild:
   ```bash
   docker rmi freeswitch-transcribe:latest
   ./docker-build.sh
   ```

---

## Help

For help with the build script:

```bash
./build-batch.sh --help
```

For FreeSWITCH help:

```bash
/usr/local/freeswitch/bin/freeswitch -help
```
