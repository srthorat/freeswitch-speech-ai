# FS-Unified-STT: FreeSWITCH Unified Speech-to-Text Service

## Purpose
- Accept WebSocket connections from FreeSWITCH `mod_audio_fork`
- Receive initial metadata text frame (JSON)
- Receive binary audio frames (PCM16, stereo recommended)
- Resample audio to 16 kHz using **speexdsp** (cgo)
- Stream to multiple Speech-to-Text providers:
  - **Google V2** (Speech-to-Text v2 API) - Recommended for multi-channel
  - **Google V1** (Speech-to-Text v1 API) - Legacy support
  - **AWS** (Amazon Transcribe Streaming) - Multi-channel supported
- Publish transcripts to Pusher using channel name = `sip_call_id` (as-is)

## Supported Providers

| Provider    | Multi-Channel | Model         | Notes                                      |
|-------------|---------------|---------------|--------------------------------------------|
| `google-v2` | ✅ Yes        | `telephony`   | Best for telephony, use `global` location  |
| `google-v1` | ⚠️ Partial   | `phone_call`  | May only return single channel             |
| `aws`       | ✅ Yes        | N/A           | Requires `AWS_SESSION_TOKEN` for temp creds|

## Environment Variables

### Required (All Providers)
```bash
SIDECAR_WS_LISTEN=127.0.0.1:9088      # WebSocket listen address
SIDECAR_ADMIN_LISTEN=127.0.0.1:9089   # Admin/metrics endpoint
DEFAULT_PROVIDER=google-v2            # Default: google-v2, google-v1, aws

# Pusher (Required)
PUSHER_APP_ID=your_app_id
PUSHER_KEY=your_key
PUSHER_SECRET=your_secret
PUSHER_CLUSTER=ap2
```

### Google Providers
```bash
GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account.json
GCP_PROJECT=your-project-id
GCP_LOCATION_DEFAULT=global           # Use 'global' for telephony model
GCP_MODEL=telephony                   # Optional, can be set per-session
GCP_INTERIM_RESULTS=true
```

### AWS Provider
```bash
AWS_ACCESS_KEY_ID=your_access_key
AWS_SECRET_ACCESS_KEY=your_secret_key
AWS_SESSION_TOKEN=your_session_token  # Required for temporary credentials
AWS_REGION=us-east-1
```

## Metadata Format (First WS Text Frame)
```json
{
  "uuid": "FS_UUID",
  "sip_call_id": "sip-call-id-from-sofia",
  "sample_rate": 16000,
  "channels": 2,
  "lang": "en-US",
  "model": "telephony",
  "provider": "google-v2",
  "location": "global",
  "stereo_swap": false,
  "caller": { "name": "Customer", "phone": "+91..." },
  "callee": { "name": "Agent", "phone": "+91..." }
}
```

## Pusher Events
- `session-started` - When session begins
- `session-stopped` - When session ends
- `transcript-interim` - Partial transcription
- `transcript-final` - Final transcription

All transcript events contain:
- `uuid`, `sip_call_id`
- `channel` (0/1 normalized)
- `role` (caller/callee)
- `caller` and `callee` objects
- `text`, `confidence`, `is_final`

## Build

```bash
cd fs-unified-stt
go build -o fs-unified-stt ./cmd/sidecar
```

## Run

### Google V2 (Recommended)
```bash
GOOGLE_APPLICATION_CREDENTIALS=/path/to/creds.json \
GCP_PROJECT=your-project \
GCP_LOCATION_DEFAULT=global \
DEFAULT_PROVIDER=google-v2 \
PUSHER_APP_ID=xxx PUSHER_KEY=xxx PUSHER_SECRET=xxx PUSHER_CLUSTER=ap2 \
SIDECAR_WS_LISTEN=127.0.0.1:9088 \
SIDECAR_ADMIN_LISTEN=127.0.0.1:9089 \
./fs-unified-stt
```

### AWS
```bash
AWS_ACCESS_KEY_ID=xxx \
AWS_SECRET_ACCESS_KEY=xxx \
AWS_SESSION_TOKEN=xxx \
AWS_REGION=us-east-1 \
DEFAULT_PROVIDER=aws \
PUSHER_APP_ID=xxx PUSHER_KEY=xxx PUSHER_SECRET=xxx PUSHER_CLUSTER=ap2 \
SIDECAR_WS_LISTEN=127.0.0.1:9088 \
SIDECAR_ADMIN_LISTEN=127.0.0.1:9089 \
./fs-unified-stt
```

## Testing

Test scripts and sample audio files are located in `test/` directory.

### Test Files
- `response_call_60s.wav` - 60-second stereo recording (default)
- `response_call.wav` - Full recording (~18 minutes)

### Setup Test Environment
```bash
cd test
python3 -m venv .venv
source .venv/bin/activate
pip install websockets
```

### Test Google V2 (Recommended)
```bash
cd test
source .venv/bin/activate
python test_stt_google_v2.py
```

### Test Google V1
```bash
python test_stt_google_v1.py --model phone_call
```

### Test AWS
```bash
python test_stt_aws.py
```

### Generic Test (uses --provider flag)
```bash
python test_stt.py --provider google-v2
python test_stt.py --provider aws
python test_stt.py --provider google-v1
```

### Test Options
```bash
python test_stt.py --help
  --provider   # google-v2, google-v1, aws
  --wav        # Path to WAV file (default: response_call_60s.wav)
  --lang       # Language code (default: en-US)
  --model      # Model name (e.g., telephony, phone_call)
  --max-duration # Limit streaming duration in seconds
```

## Python Environment Setup
Before running any tests, you must set up the Python environment and install dependencies.

```bash
cd test
# Create virtual environment
python3 -m venv .venv
# Activate environment
source .venv/bin/activate
# Install dependencies
pip install websockets psutil
```

## Load & Performance Testing

A comprehensive load testing suite is included to validate system performance, stability, and concurrency.

### Prerequisite: Start Server with Mock Provider
This enables the mock provider (no API costs) and mock pusher (local file logging).

```bash
cd fs-unified-stt
DEFAULT_PROVIDER=mock \
PUSHER_APP_ID=xxx PUSHER_KEY=xxx PUSHER_SECRET=xxx PUSHER_CLUSTER=ap2 \
SIDECAR_WS_LISTEN=127.0.0.1:9088 \
SIDECAR_ADMIN_LISTEN=127.0.0.1:9089 \
./fs-unified-stt
```

### 1. Scale Test (Burst Capacity)
Tests how the system handles immediate concurrent connection spikes.

```bash
cd test
source .venv/bin/activate

# 100 concurrent connections for 60 seconds
python load_test.py --mode scale --connections 100 --duration 60

# 500 concurrent connections for 2 minutes
python load_test.py --mode scale --connections 500 --duration 120
```

### 2. Longevity Test (Stability)
Tests system stability over long periods with sustained load.

```bash
# 50 sustained connections for 1 hour
python load_test.py --mode longevity --connections 50 --duration 3600

# 100 sustained connections for 4 hours
python load_test.py --mode longevity --connections 100 --duration 14400
```

### 3. Simulation Test (Realistic Traffic)
Simulates real-world traffic patterns using:
- **Poisson Arrival Process**: Random call start times (avg rate = target / avg duration)
- **Variable Call Durations**: Mix of Short (5-15s), Medium (30-90s), and Long (2-5m) calls
- **Traffic Spikes**: Configurable spike factor (e.g., 1.5x load in middle of test)

```bash
# Target 50 concurrent calls, randomize arrivals, 1.5x traffic spike
python load_test.py --mode simulation --connections 50 --duration 300 --spike 1.5
```

### Analysis & Validation
The load tester automatically provides a detailed summary including:
- **Connection stats**: Success/fail rates, connect latency
- **Throughput**: Messages/sec, Bytes/sec
- **System Metrics**: CPU, Memory, Disk I/O, Network utilization
- **Validation**: Verifies that all started sessions completed and generated transcripts (via `mock_pusher_events.jsonl` analysis)

### Command Options
```bash
python load_test.py --help
  --mode         # scale, longevity, or simulation
  --connections  # Target concurrent connections
  --duration     # Test duration in seconds
  --spike        # Spike factor for simulation (default: 1.5)
  --provider     # Provider to use (default: mock)
  --url          # WebSocket URL
  --wav          # WAV file to stream (default: response_call_60s.wav)
```

### High Scale Testing (10k+ Concurrent)

To test **3k-10k concurrent calls** (approx 50-150 CPS with 60s duration):

1. **Calculate CPS**: `Concurrency = CPS * AvgDuration`
   - 3000 Concurrent @ 60s calls = 50 CPS
   - 9000 Concurrent @ 60s calls = 150 CPS

2. **OS Tuning (Important)**
   Ensure file descriptor limits are raised on both client and server:
   ```bash
   ulimit -n 100000
   sysctl -w net.ipv4.ip_local_port_range="10000 65000"
   ```

3. **Running the Test**
   
   **For 1k - 3k Concurrent (30-60 CPS):**
   Can be run from a single terminal:
   ```bash
   # Target 3000 concurrent (approx 50 CPS)
   python load_test.py --mode simulation --connections 3000 --duration 600
   ```

   **For 10k+ Concurrent:**
   Split the load across multiple processes to avoid Python client bottlenecks:

   ```bash
   # Terminal 1 (2500 calls)
   python load_test.py --mode simulation --connections 2500 --duration 1200
   
   # Terminal 2 (2500 calls)
   python load_test.py --mode simulation --connections 2500 --duration 1200
   # ... repeat for desired total load
   ```


Start streaming:
```
uuid_audio_fork <uuid> start ws://127.0.0.1:9088/audio-fork stereo <sampling> <metadata-json>
```

Stop streaming:
```
uuid_audio_fork <uuid> stop
```

## Concurrency Notes
This service is designed for high density:
- WS read path is minimal (drains into ring buffer)
- Separate sender loop drains ring buffer and sends to provider
- Receiver loop publishes interim/final transcripts
- Ring buffer is bounded (drop-oldest) to preserve real-time latency at scale

Plan for:
- Provider quotas (concurrent streams)
- CPU/network per host
- Linux `nofile` limits

## Documentation
- `docs/SESSION_SUMMARY.md` - Current session status and verification results
- `docs/UBUNTU_BUILD_RUN.md` - Ubuntu deployment guide