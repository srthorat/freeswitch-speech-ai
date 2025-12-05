# Performance & Scalability Analysis: mod_deepgram_transcribe

This document provides a comprehensive analysis of thread model, memory usage, network I/O, latency, and scalability for `mod_deepgram_transcribe` with projections for **500 concurrent calls**.

---

## Table of Contents

1. [Thread Model Per Call](#1-thread-model-per-call)
2. [Memory Usage Per Call](#2-memory-usage-per-call)
3. [Network I/O Requirements](#3-network-io-requirements)
4. [Latency & Jitter Analysis](#4-latency--jitter-analysis)
5. [Scalability Bottlenecks & Recommendations](#5-scalability-bottlenecks--recommendations)
6. [Summary Table: 500 Concurrent Calls](#6-summary-table-500-concurrent-calls)
7. [Architecture Diagram](#7-architecture-diagram)
8. [System Tuning Guide](#8-system-tuning-guide)

---

## 1. Thread Model Per Call

### Thread Breakdown

| Component | Threads | Scope | Notes |
|-----------|---------|-------|-------|
| **FreeSWITCH Core** | 1 | Per call | Session thread handles SIP signaling |
| **Media Bug Callback** | 0 | Shared | Runs in FS media thread pool (not per-call) |
| **LWS Service Threads** | 1-5 | **Global** | Configurable via `MOD_AUDIO_FORK_SERVICE_THREADS` (default=1) |
| **Async HTTP Pump** | 1 | **Global** | Background thread for Pusher HTTP |
| **Reaper Thread** | 1 | Per disconnect | Detached, short-lived for cleanup |

### Thread Count for 500 Calls

```
FreeSWITCH session threads:     500  (1 per call)
FreeSWITCH media threads:       ~16  (shared pool, configurable)
LWS WebSocket service:          1-5  (global, handles ALL 500 WS connections)
Async HTTP pump:                1    (global)
Reaper threads:                 ~5   (transient, short-lived)
────────────────────────────────────────────
TOTAL:                          ~522-527 threads
```

### Key Design Decision: Lock-Free Architecture

The lock-free ring buffer design means the audio frame callback **never blocks** - it returns immediately after a `memcpy`. This is critical for high-scale performance:

```cpp
// From lockfree_ring_buffer.hpp
// SPSC (Single Producer Single Consumer) design
// - Exactly ONE thread may call push() (producer - frame callback)
// - Exactly ONE thread may call pop() (consumer - LWS callback)
// - No external synchronization needed
```

---

## 2. Memory Usage Per Call

### Per-Call Memory Breakdown

| Component | Size | Notes |
|-----------|------|-------|
| **private_t struct** | ~9 KB | Session data, resampler stats |
| **Lock-free Ring Buffer** | 64 KB | `AUDIO_RING_BUFFER_SIZE = 65536` |
| **Speex Resampler** | ~4 KB | If 8kHz→16kHz conversion needed |
| **AudioPipe object** | ~2 KB | WebSocket state, buffers |
| **LWS per-connection** | ~8 KB | libwebsockets internal state |
| **WebSocket recv buffer** | ~64 KB | `MAX_RECV_BUF_SIZE = 650 KB` max |

### Memory Projection for 500 Calls

```
Per-call baseline:              ~88 KB
Per-call with resampler:        ~92 KB
Per-call peak (large response): ~150 KB

500 calls @ 92 KB average:      46 MB
500 calls @ 150 KB peak:        75 MB

Global structures:
  - LWS contexts (5 threads):   ~5 MB
  - Async HTTP queue (10K max): ~40 MB (worst case)
  - CURL connection pool:       ~10 MB
────────────────────────────────────────────
TOTAL MEMORY:                   ~100-130 MB
```

### Ring Buffer Sizing

The ring buffer is sized for approximately 2 seconds of audio:

```cpp
// From audio_pipe.hpp
// Buffer size: 64KB = ~2 seconds of 16kHz mono audio (or ~1 sec stereo)
// Power of 2 for efficient modulo operations
static constexpr size_t AUDIO_RING_BUFFER_SIZE = 65536;
```

If you see "ring buffer full" warnings, increase via environment variable:
```bash
Environment="MOD_AUDIO_FORK_BUFFER_SECS=3"  # Increase to 3 seconds
```

---

## 3. Network I/O Requirements

### Per Call (Stereo/Multichannel)

```
Audio Upload to Deepgram:
  - Sample rate: 16 kHz
  - Bits per sample: 16-bit
  - Channels: 2 (stereo)
  - Bandwidth: 16000 × 2 × 2 = 64 KB/s = 512 kbps per call

Deepgram Responses:
  - Interim results: ~100-500 bytes every 500ms
  - Final results: ~500-2000 bytes per utterance
  - Average: ~5-10 KB/minute per call
```

### For 500 Concurrent Calls

```
UPLOAD (Audio to Deepgram):
  500 × 512 kbps = 256 Mbps

DOWNLOAD (Transcription results):
  500 × ~10 KB/min = ~83 KB/s ≈ 0.7 Mbps

PUSHER HTTP (if enabled):
  - Per transcription: ~2 KB POST
  - ~5 transcriptions/min/call: 500 × 5 × 2 KB/min = ~83 KB/s
  
────────────────────────────────────────────
TOTAL BANDWIDTH:
  Upload:   ~260 Mbps sustained
  Download: ~2 Mbps
```

### WebSocket Connections

- **500 persistent WSS connections** to Deepgram
- TCP keepalive: 55 seconds (configurable via `MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS`)

### Connection Limits

```cpp
// From async_http.h
#define ASYNC_HTTP_MAX_PARALLEL_REQUESTS 100  // Concurrent HTTP requests
#define ASYNC_HTTP_MAX_QUEUE_SIZE 10000       // Queued HTTP requests
```

---

## 4. Latency & Jitter Analysis

### Audio Processing Latency Breakdown

```
1. Frame arrival (20ms packetization):        0 ms
2. Media bug callback execution:              <1 ms (lock-free push)
3. Ring buffer queuing:                       0 ms (atomic operations)
4. LWS service picks up data:                 0-10 ms (event-driven)
5. WebSocket write:                           1-5 ms
6. Network to Deepgram:                       20-100 ms (depends on region)
7. Deepgram processing:                       100-300 ms
8. Response over WebSocket:                   20-100 ms
────────────────────────────────────────────
TOTAL END-TO-END:                             150-500 ms typical
```

### Jitter Tolerance

| Factor | Impact |
|--------|--------|
| **Ring Buffer (64KB)** | Absorbs ~1-2 seconds of audio jitter |
| **LWS Event Loop** | May add 0-10ms variance |
| **Network Jitter** | Primary variable (handle with buffer) |

### Under 500 Call Load

```
Expected Impact:
  - LWS service threads handling 100+ connections each
  - Event loop latency: 1-5ms → 5-20ms
  - Ring buffer fill level: 2% → 10-20%
  
Recommendation: Set MOD_AUDIO_FORK_SERVICE_THREADS=3-5
```

### Latency Optimization Tips

1. **Use Deepgram's nearest region** - Reduces network RTT
2. **Enable `interim_results=true`** - Get faster feedback
3. **Set `endpointing=300`** - Reduce silence detection delay
4. **Increase LWS threads** - Better event loop responsiveness

---

## 5. Scalability Bottlenecks & Recommendations

### Current Architecture Limits

| Resource | Limit | 500 Calls Impact |
|----------|-------|------------------|
| LWS Service Threads | 1-5 (configurable) | **Increase to 5** |
| Ring Buffer Size | 64 KB | OK - ~2 sec buffer |
| Async HTTP Queue | 10,000 requests | OK |
| Max Parallel HTTP | 100 | OK |
| File Descriptors | System default (~1024) | **Increase to 65535** |

### Bottleneck Priority

1. 🔴 **File Descriptors** - Most critical, will cause connection failures
2. 🟡 **LWS Threads** - Affects latency under load
3. 🟡 **Network Bandwidth** - Need 500+ Mbps uplink
4. 🟢 **Memory** - Unlikely to be an issue (~130MB)
5. 🟢 **CPU** - Lock-free design minimizes contention

### Environment Variables for 500 Calls

Add to `/etc/systemd/system/freeswitch.service.d/freeswitch.conf`:

```ini
[Service]
# High-scale optimization for 500+ calls
Environment="MOD_AUDIO_FORK_SERVICE_THREADS=5"
Environment="MOD_AUDIO_FORK_BUFFER_SECS=3"
Environment="MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS=30"

# Increase file descriptor limit
LimitNOFILE=100000
```

---

## 6. Summary Table: 500 Concurrent Calls

| Metric | Value | Risk Level |
|--------|-------|------------|
| **Total Threads** | ~525 | 🟡 Medium |
| **Memory Usage** | ~100-130 MB | 🟢 Low |
| **Network Upload** | ~260 Mbps | 🟡 Medium |
| **Network Download** | ~2 Mbps | 🟢 Low |
| **WebSocket Connections** | 500 | 🟢 Low |
| **File Descriptors** | ~1500 | 🔴 High (needs tuning) |
| **End-to-End Latency** | 200-500ms | 🟢 Acceptable |
| **Audio Jitter Buffer** | 1-2 seconds | 🟢 Good |

### Minimum Server Specs for 500 Calls

```
CPU:      4+ cores (8 recommended)
RAM:      4 GB minimum (8 GB recommended)  
Network:  500 Mbps uplink
Storage:  SSD for FreeSWITCH logs
OS:       Ubuntu 22.04 with tuned kernel
```

### Recommended Server Specs for 500 Calls

```
CPU:      8+ cores
RAM:      16 GB
Network:  1 Gbps uplink
Storage:  NVMe SSD
OS:       Ubuntu 22.04 LTS with performance kernel
```

---

## 7. Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        FreeSWITCH Server                            │
│                                                                     │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  500 Call Sessions                                          │   │
│  │  ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐        ┌─────┐           │   │
│  │  │Call1│ │Call2│ │Call3│ │Call4│  ...   │C500 │           │   │
│  │  └──┬──┘ └──┬──┘ └──┬──┘ └──┬──┘        └──┬──┘           │   │
│  │     │       │       │       │              │               │   │
│  │     ▼       ▼       ▼       ▼              ▼               │   │
│  │  ┌──────────────────────────────────────────────────────┐  │   │
│  │  │  Media Thread Pool (16 threads)                      │  │   │
│  │  │  - Frame callback runs here                          │  │   │
│  │  │  - Lock-free push to ring buffer (<1ms)              │  │   │
│  │  └──────────────────────────────────────────────────────┘  │   │
│  │                        │                                   │   │
│  │                        ▼                                   │   │
│  │  ┌──────────────────────────────────────────────────────┐  │   │
│  │  │  Lock-Free Ring Buffers (64KB × 500 = 32MB)          │  │   │
│  │  │  - SPSC design (no mutex)                            │  │   │
│  │  │  - Cache-line aligned                                │  │   │
│  │  └──────────────────────────────────────────────────────┘  │   │
│  │                        │                                   │   │
│  │                        ▼                                   │   │
│  │  ┌──────────────────────────────────────────────────────┐  │   │
│  │  │  LWS Service Threads (5 threads)                     │  │   │
│  │  │  - Each handles ~100 WebSocket connections           │  │   │
│  │  │  - Event-driven (epoll)                              │  │   │
│  │  └──────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                        │                                           │
└────────────────────────┼───────────────────────────────────────────┘
                         │
                         ▼ 260 Mbps
              ┌──────────────────────┐
              │   Deepgram Cloud     │
              │   (500 WSS streams)  │
              └──────────────────────┘
```

### Data Flow

1. **RTP Audio** → FreeSWITCH media thread receives 20ms frames
2. **Media Bug** → `capture_callback()` pushes to ring buffer (lock-free)
3. **Ring Buffer** → SPSC buffer decouples producer/consumer
4. **LWS Thread** → Pops from buffer, writes to WebSocket
5. **Deepgram** → Processes audio, returns transcription JSON
6. **Callback** → `responseHandler()` fires FreeSWITCH event
7. **Pusher** (optional) → Async HTTP POST to Pusher.com

---

## 8. System Tuning Guide

### Kernel Parameters

Add to `/etc/sysctl.conf`:

```bash
# Network buffer sizes
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.core.rmem_default = 1048576
net.core.wmem_default = 1048576

# TCP tuning
net.ipv4.tcp_rmem = 4096 1048576 16777216
net.ipv4.tcp_wmem = 4096 1048576 16777216
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_fin_timeout = 15
net.ipv4.tcp_keepalive_time = 300
net.ipv4.tcp_keepalive_probes = 5
net.ipv4.tcp_keepalive_intvl = 15

# Connection tracking
net.netfilter.nf_conntrack_max = 262144
net.core.netdev_max_backlog = 65535
net.core.somaxconn = 65535

# File descriptors
fs.file-max = 2097152
fs.nr_open = 2097152
```

Apply with: `sudo sysctl -p`

### File Descriptor Limits

Add to `/etc/security/limits.conf`:

```bash
# File descriptor limits for high-scale operation
*               soft    nofile          65535
*               hard    nofile          100000
root            soft    nofile          100000
root            hard    nofile          100000
freeswitch      soft    nofile          100000
freeswitch      hard    nofile          100000
```

### systemd Service Limits

Add to `/etc/systemd/system/freeswitch.service.d/freeswitch.conf`:

```ini
[Service]
# File descriptor limit
LimitNOFILE=100000

# Core dump size (for debugging)
LimitCORE=infinity

# Memory lock limit
LimitMEMLOCK=infinity

# Real-time priority (optional, for audio quality)
LimitRTPRIO=99
LimitRTTIME=infinity
```

### Verify Limits

After reboot or service restart:

```bash
# Check system-wide file descriptor limit
cat /proc/sys/fs/file-max

# Check FreeSWITCH process limits
cat /proc/$(pgrep -f freeswitch)/limits | grep "Max open files"

# Monitor current usage
lsof -p $(pgrep -f freeswitch) | wc -l
```

---

## Monitoring Commands

### Real-time Call Statistics

```bash
# Active calls
fs_cli -x "show calls count"

# Module status
fs_cli -x "show modules" | grep deepgram

# Channel count
fs_cli -x "show channels count"
```

### System Monitoring

```bash
# CPU and memory
htop

# Network connections
ss -s
ss -tnp | grep freeswitch | wc -l

# File descriptors
ls /proc/$(pgrep -f freeswitch)/fd | wc -l

# Network throughput
iftop -i eth0
```

### Deepgram Connection Health

Watch for these log patterns:

```bash
# Successful connections
journalctl -u freeswitch | grep "CONNECT_SUCCESS"

# Connection failures
journalctl -u freeswitch | grep "CONNECT_FAIL\|CONNECTION_DROPPED"

# Ring buffer status
journalctl -u freeswitch | grep "ring buffer"
```

---

## Scaling Beyond 500 Calls

For 1000+ concurrent calls, consider:

1. **Horizontal Scaling** - Multiple FreeSWITCH servers behind SIP proxy
2. **Dedicated Deepgram Connections** - On-premise Deepgram deployment
3. **Increased LWS Threads** - Up to 10 threads (`MOD_AUDIO_FORK_SERVICE_THREADS=10`)
4. **Larger Ring Buffers** - Increase `MOD_AUDIO_FORK_BUFFER_SECS=5`
5. **Dedicated NICs** - Separate network interfaces for SIP and transcription traffic

---

## Appendix: Configuration Reference

### Environment Variables

| Variable | Default | Description |
|----------|---------|-------------|
| `MOD_AUDIO_FORK_SERVICE_THREADS` | 1 | Number of LWS service threads (1-5) |
| `MOD_AUDIO_FORK_BUFFER_SECS` | 2 | Ring buffer size in seconds (1-5) |
| `MOD_AUDIO_FORK_TCP_KEEPALIVE_SECS` | 55 | TCP keepalive interval |
| `DEEPGRAM_API_KEY` | (required) | Deepgram API key |
| `PUSHER_APP_ID` | (optional) | Pusher app ID for real-time delivery |
| `PUSHER_KEY` | (optional) | Pusher key |
| `PUSHER_SECRET` | (optional) | Pusher secret |
| `PUSHER_CLUSTER` | ap2 | Pusher cluster region |

### Dialplan Channel Variables

| Variable | Example | Description |
|----------|---------|-------------|
| `DEEPGRAM_SPEECH_MODEL` | nova-2 | Deepgram model |
| `DEEPGRAM_SPEECH_LANGUAGE` | en-US | Recognition language |
| `DEEPGRAM_SPEECH_DIARIZE` | true | Enable speaker diarization |
| `DEEPGRAM_SPEECH_ENDPOINTING` | 300 | Silence detection threshold (ms) |
| `DEEPGRAM_SPEECH_UTTERANCE_END_MS` | 1000 | Utterance end timeout (ms) |

---

*Last updated: December 2024*
*Module version: mod_deepgram_transcribe with lock-free ring buffer*
