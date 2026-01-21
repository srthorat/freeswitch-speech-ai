#!/usr/bin/env bash
set -euo pipefail

# Install/build/run helper for google-speech-service-v2.
# Run from repo root:
#   sudo bash sidecar/scripts/install_ubuntu.sh --repo-dir "$(pwd)" --branch main

REPO_DIR=""
BRANCH="main"
INSTALL_DIR="/opt/google-speech-service-v2"
USER_NAME="google-speech-v2"
GROUP_NAME="google-speech-v2"

print_usage() {
  cat <<EOF
Usage: sudo bash $0 --repo-dir <path> [--branch <name>] [--install-dir <path>] [--user <name>] [--group <name>]
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --repo-dir) REPO_DIR="$2"; shift 2 ;;
    --branch) BRANCH="$2"; shift 2 ;;
    --install-dir) INSTALL_DIR="$2"; shift 2 ;;
    --user) USER_NAME="$2"; shift 2 ;;
    --group) GROUP_NAME="$2"; shift 2 ;;
    -h|--help) print_usage; exit 0 ;;
    *) echo "Unknown arg: $1"; print_usage; exit 1 ;;
  esac
done

if [[ -z "$REPO_DIR" ]]; then
  echo "ERROR: --repo-dir is required"
  exit 1
fi

echo "==============================================================="
echo "Installing Unified Speech Sidecar"
echo "Repo:        $REPO_DIR"
echo "Branch:      $BRANCH"
echo "Install dir: $INSTALL_DIR"
echo "User:Group:  $USER_NAME:$GROUP_NAME"
echo "==============================================================="

export DEBIAN_FRONTEND=noninteractive
apt-get update -y
apt-get install -y --no-install-recommends \
  ca-certificates curl git unzip \
  build-essential pkg-config \
  libspeexdsp-dev \
  jq \
  && rm -rf /var/lib/apt/lists/*

# Increase OS limits recommendations (does not force apply, but prints guidance)
echo ""
echo "Recommended sysctl for many outbound connections (FS client side):"
echo "  net.ipv4.ip_local_port_range = 10240 65535"
echo "Recommended NOFILE:"
echo "  200000"
echo ""

# Install Go if needed
if command -v go >/dev/null 2>&1; then
  echo "Go already installed: $(go version)"
else
  GO_VERSION="${GO_VERSION:-1.22.8}"
  ARCH="$(dpkg --print-architecture)"
  case "$ARCH" in
    amd64) GO_ARCH="amd64" ;;
    arm64) GO_ARCH="arm64" ;;
    *) echo "Unsupported arch: $ARCH"; exit 1 ;;
  esac
  curl -fsSL "https://go.dev/dl/go${GO_VERSION}.linux-${GO_ARCH}.tar.gz" -o /tmp/go.tgz
  rm -rf /usr/local/go
  tar -C /usr/local -xzf /tmp/go.tgz
  rm -f /tmp/go.tgz
  echo 'export PATH=/usr/local/go/bin:$PATH' >/etc/profile.d/go.sh
  export PATH=/usr/local/go/bin:$PATH
  echo "Installed: $(go version)"
fi

# Create user/group
if ! getent group "$GROUP_NAME" >/dev/null 2>&1; then
  groupadd --system "$GROUP_NAME"
fi
if ! id "$USER_NAME" >/dev/null 2>&1; then
  useradd --system --home "$INSTALL_DIR" --shell /usr/sbin/nologin --gid "$GROUP_NAME" "$USER_NAME"
fi

cd "$REPO_DIR"
git fetch --all --prune
git checkout "$BRANCH"

if [[ ! -d "sidecar" ]]; then
  echo "ERROR: sidecar directory not found. Did you add the sidecar project?"
  exit 1
fi

cd sidecar
go mod tidy

GIT_SHA="$(git -C "$REPO_DIR" rev-parse --short HEAD || true)"
BUILD_TIME="$(date -u +%Y-%m-%dT%H:%M:%SZ)"

mkdir -p /tmp/google-speech-v2-build
go build -trimpath -ldflags "-s -w -X main.gitSha=${GIT_SHA} -X main.buildTime=${BUILD_TIME}" -o /tmp/google-speech-v2-build/google-speech-service-v2 ./cmd/sidecar || \
  go build -trimpath -o /tmp/google-speech-v2-build/google-speech-service-v2 ./cmd/sidecar

mkdir -p "$INSTALL_DIR"/{bin,systemd}
cp /tmp/google-speech-v2-build/google-speech-service-v2 "$INSTALL_DIR/bin/google-speech-service-v2"
chmod 0755 "$INSTALL_DIR/bin/google-speech-service-v2"

cp "$REPO_DIR/sidecar/.env.example" "$INSTALL_DIR/.env.example"

if [[ ! -f "$INSTALL_DIR/.env" ]]; then
  cp "$INSTALL_DIR/.env.example" "$INSTALL_DIR/.env"
fi

cat >"$INSTALL_DIR/systemd/google-speech-service-v2.service" <<EOF
[Unit]
Description=FreeSWITCH Audio Fork - Google Speech Service v2
After=network.target
Wants=network-online.target

[Service]
Type=simple
User=${USER_NAME}
Group=${GROUP_NAME}
WorkingDirectory=${INSTALL_DIR}
EnvironmentFile=${INSTALL_DIR}/.env
ExecStart=${INSTALL_DIR}/bin/google-speech-service-v2
Restart=always
RestartSec=1

LimitNOFILE=200000
StandardOutput=journal
StandardError=journal

NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=${INSTALL_DIR}

[Install]
WantedBy=multi-user.target
EOF

chown -R "${USER_NAME}:${GROUP_NAME}" "$INSTALL_DIR"
chmod 0640 "$INSTALL_DIR/.env" || true

cp "$INSTALL_DIR/systemd/google-speech-service-v2.service" /etc/systemd/system/google-speech-service-v2.service
systemctl daemon-reload
systemctl enable google-speech-service-v2.service
systemctl restart google-speech-service-v2.service

echo "==============================================================="
echo "Installed and started google-speech-service-v2"
echo "Edit env:"
echo "  sudo nano $INSTALL_DIR/.env"
echo "Logs:"
echo "  sudo journalctl -u google-speech-service-v2 -f"
echo "Health:"
echo "  curl -s http://127.0.0.1:8089/healthz && echo"
echo "==============================================================="