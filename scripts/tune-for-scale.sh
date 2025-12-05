#!/bin/bash
# ============================================================================
# FreeSWITCH Speech AI - System Tuning for High Scale (2000+ calls)
# ============================================================================
# Run as root: sudo ./tune-for-scale.sh
#
# This script configures Linux kernel parameters for high-concurrency
# real-time transcription workloads.
# ============================================================================

set -e

echo "============================================="
echo "FreeSWITCH Speech AI - High Scale Tuning"
echo "============================================="
echo ""

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Error: This script must be run as root"
    echo "Please run: sudo $0"
    exit 1
fi

# Backup existing sysctl.conf
cp /etc/sysctl.conf /etc/sysctl.conf.backup.$(date +%Y%m%d_%H%M%S)

# Create FreeSWITCH-specific sysctl configuration
cat > /etc/sysctl.d/99-freeswitch-speech-ai.conf <<'EOF'
# ============================================================================
# FreeSWITCH Speech AI - Kernel Tuning for 2000+ Concurrent Calls
# ============================================================================

# =============================================================================
# FILE DESCRIPTORS & LIMITS
# =============================================================================
# Each call needs: 1 RTP socket + 1 WebSocket + 1 HTTP connection = ~3-5 FDs
# 2000 calls × 5 FDs = 10,000 minimum, use 65535 for headroom
fs.file-max = 2097152
fs.nr_open = 2097152

# =============================================================================
# NETWORK STACK - TCP/IP TUNING
# =============================================================================
# Increase socket buffer sizes for WebSocket and HTTP traffic
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.core.rmem_default = 1048576
net.core.wmem_default = 1048576
net.ipv4.tcp_rmem = 4096 1048576 16777216
net.ipv4.tcp_wmem = 4096 1048576 16777216

# Increase connection tracking for high connection count
net.netfilter.nf_conntrack_max = 1048576
net.nf_conntrack_max = 1048576

# Increase local port range for outbound connections (WebSocket to Deepgram)
net.ipv4.ip_local_port_range = 1024 65535

# Allow socket reuse (important for rapid reconnections)
net.ipv4.tcp_tw_reuse = 1

# Increase connection backlog
net.core.somaxconn = 65535
net.ipv4.tcp_max_syn_backlog = 65535

# Reduce TCP keepalive time (faster detection of dead connections)
net.ipv4.tcp_keepalive_time = 60
net.ipv4.tcp_keepalive_intvl = 10
net.ipv4.tcp_keepalive_probes = 6

# Enable TCP Fast Open
net.ipv4.tcp_fastopen = 3

# Increase max queued connections
net.core.netdev_max_backlog = 65535

# =============================================================================
# MEMORY MANAGEMENT
# =============================================================================
# Disable swap (real-time audio can't tolerate swap latency)
vm.swappiness = 0

# Allow more memory for network buffers
net.ipv4.tcp_mem = 786432 1048576 1572864

# Don't cache too many negative dentries
vm.vfs_cache_pressure = 50

# =============================================================================
# UDP/RTP TUNING (for FreeSWITCH media)
# =============================================================================
net.core.netdev_budget = 600
net.core.netdev_budget_usecs = 8000

# Increase UDP buffer sizes for RTP
net.ipv4.udp_mem = 786432 1048576 1572864
net.ipv4.udp_rmem_min = 16384
net.ipv4.udp_wmem_min = 16384

# =============================================================================
# PROCESS LIMITS
# =============================================================================
# Max threads per process (FreeSWITCH + transcription threads)
kernel.threads-max = 4194304
kernel.pid_max = 4194304

EOF

# Apply sysctl settings
echo "Applying kernel parameters..."
sysctl -p /etc/sysctl.d/99-freeswitch-speech-ai.conf

# Configure systemd limits for FreeSWITCH
echo "Configuring systemd limits..."
mkdir -p /etc/systemd/system/freeswitch.service.d

cat > /etc/systemd/system/freeswitch.service.d/limits.conf <<'EOF'
# High-scale limits for FreeSWITCH
[Service]
# File descriptors: 2000 calls × 5 FDs + headroom
LimitNOFILE=1048576
LimitNOFILESoft=1048576

# Processes/threads
LimitNPROC=unlimited

# Core dumps (disable for production, enable for debugging)
LimitCORE=0

# Memory locking (for real-time audio)
LimitMEMLOCK=infinity

# Real-time priority
LimitRTPRIO=99

# Stack size
LimitSTACK=8388608

# Nice priority
Nice=-10
EOF

# Configure system-wide limits
echo "Configuring system-wide limits..."
cat > /etc/security/limits.d/99-freeswitch.conf <<'EOF'
# FreeSWITCH Speech AI - User Limits for High Scale
*               soft    nofile          1048576
*               hard    nofile          1048576
*               soft    nproc           unlimited
*               hard    nproc           unlimited
root            soft    nofile          1048576
root            hard    nofile          1048576
root            soft    nproc           unlimited
root            hard    nproc           unlimited
EOF

# Increase inotify watches (for file monitoring)
echo "fs.inotify.max_user_watches = 524288" >> /etc/sysctl.d/99-freeswitch-speech-ai.conf
echo "fs.inotify.max_user_instances = 8192" >> /etc/sysctl.d/99-freeswitch-speech-ai.conf
sysctl -p /etc/sysctl.d/99-freeswitch-speech-ai.conf 2>/dev/null || true

# Reload systemd
systemctl daemon-reload

echo ""
echo "============================================="
echo "✓ System Tuning Complete!"
echo "============================================="
echo ""
echo "Changes applied:"
echo "  - Kernel parameters: /etc/sysctl.d/99-freeswitch-speech-ai.conf"
echo "  - Systemd limits: /etc/systemd/system/freeswitch.service.d/limits.conf"
echo "  - User limits: /etc/security/limits.d/99-freeswitch.conf"
echo ""
echo "Recommended next steps:"
echo "  1. Restart FreeSWITCH: sudo systemctl restart freeswitch"
echo "  2. Verify limits: cat /proc/\$(pgrep freeswitch)/limits"
echo "  3. Monitor during load test: watch -n1 'ss -s && cat /proc/net/sockstat'"
echo ""
echo "Hardware recommendations for 2000 calls:"
echo "  - CPU: 16-32 cores"
echo "  - RAM: 32-64 GB"
echo "  - Network: 10 Gbps or bonded 2×1Gbps"
echo ""
