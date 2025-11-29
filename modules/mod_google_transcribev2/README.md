# mod_google_transcribev2

FreeSWITCH module for **Google Cloud Speech-to-Text v2** streaming API with **high-performance async architecture** supporting **2000+ concurrent calls**.

## Features

✅ **Google Speech-to-Text v2 API**
- Streaming recognition via gRPC (async CompletionQueue)
- Real-time transcription with partial and final results
- Word-level timestamps and confidence scores

✅ **High-Performance Architecture**
- Async gRPC with shared CompletionQueue (no per-session threads)
- Lock-free audio queues for zero-contention audio path
- Non-blocking Pusher delivery with retry
- Pre-connection audio buffering (~1 second)

✅ **Multi-Channel Support**
- Automatic multichannel processing when stereo mode enabled
- Separate recognition per channel with SEPARATE_RECOGNITION_PER_CHANNEL
- Channel-based speaker identification (0=caller, 1=callee)

✅ **Pusher Integration**
- Async Pusher API integration with HMAC SHA256 signing
- Non-blocking event delivery
- Automatic retry with bounded queue

## Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│  For 2000 calls = ~16 threads total (not 2000+)                     │
│                                                                     │
│  FreeSWITCH Audio Callbacks (per-call)                              │
│         │                                                           │
│         ▼ (lock-free enqueue)                                       │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ AudioQueue (per-session)                                     │   │
│  │ - Lock-free SPSC queue                                       │   │
│  │ - Pre-connection buffer (~1 second)                          │   │
│  └──────────────────────────────────────────────────────────────┘   │
│         │                                                           │
│         ▼                                                           │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ GrpcManager (Singleton)                                      │   │
│  │ - 1 CompletionQueue                                          │   │
│  │ - 8-16 Worker Threads (auto-detected from CPU cores)         │   │
│  │ - Handles ALL sessions via async state machines              │   │
│  └──────────────────────────────────────────────────────────────┘   │
│         │                                                           │
│         ▼                                                           │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ AsyncPusherClient (Singleton)                                │   │
│  │ - 2-4 Worker Threads                                         │   │
│  │ - Bounded queue (10000 events)                               │   │
│  │ - Automatic retry with backoff                               │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | File | Purpose |
|-----------|------|---------|
| GrpcManager | `grpc_manager.h/cpp` | Shared gRPC worker pool |
| AsyncPusherClient | `async_pusher_client.h/cpp` | Non-blocking HTTP delivery |
| AudioQueue | `audio_queue.h/cpp` | Lock-free audio buffering |
| GoogleStreamerSession | `google_streamer_session.h/cpp` | Async state machine per call |
| C Glue Layer | `google_transcribe_glue.cpp` | FreeSWITCH C interface |

## Performance

| Metric | Legacy | v2 Async |
|--------|--------|----------|
| Threads (2000 calls) | 4000+ | ~16 |
| Audio callback latency | 10-100ms | <1ms |
| Pusher delivery latency | 0-2000ms | <50ms |
| Memory per session | ~1MB | ~50KB |
| Max concurrent calls | ~500 | 2000+ |

## Prerequisites

### System Requirements
- **FreeSWITCH** 1.10.11+
- **C++17** compatible compiler (GCC 8+ or Clang 6+)
- **gRPC** and **protobuf** development libraries

### Dependencies

```bash
# Debian/Ubuntu
sudo apt-get install -y \
    libgrpc++-dev \
    libgrpc-dev \
    protobuf-compiler \
    protobuf-compiler-grpc \
    libprotobuf-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    libspeex-dev \
    libspeexdsp-dev
```

## Installation

```bash
cd modules/mod_google_transcribev2

# Build
make

# Install
sudo make install
```

## Configuration

### Google Cloud Authentication

```bash
# Service Account Key
export GOOGLE_APPLICATION_CREDENTIALS="/path/to/service-account-key.json"

# Project and Location (choose one naming convention)
export GCP_PROJECT_ID="your-project-id"
export GCP_LOCATION="us-central1"
# OR
export GOOGLE_PROJECT_ID="your-project-id"
export GOOGLE_LOCATION_ID="us-central1"
```

### Systemd Configuration

For FreeSWITCH running as a systemd service, add to `/etc/systemd/system/freeswitch.service.d/freeswitch.conf`:

```ini
[Service]
Environment="GOOGLE_APPLICATION_CREDENTIALS=/path/to/service-account-key.json"
Environment="GCP_PROJECT_ID=your-project-id"
Environment="GCP_LOCATION=us-central1"
```

Then reload: `sudo systemctl daemon-reload && sudo systemctl restart freeswitch`

### Supported Environment Variables

| Variable | Aliases | Description |
|----------|---------|-------------|
| `GCP_PROJECT_ID` | `GOOGLE_PROJECT_ID` | Google Cloud project ID |
| `GCP_LOCATION` | `GOOGLE_LOCATION_ID`, `GCP_LOCATION_ID` | Region (e.g., `us-central1`, `eu-west1`) |
| `GOOGLE_APPLICATION_CREDENTIALS` | - | Path to service account JSON key |

### Channel Variables

Channel variables take precedence over environment variables.

```xml
<!-- Google Cloud -->
<action application="set" data="GOOGLE_PROJECT_ID=my-project-id"/>
<action application="set" data="GOOGLE_LOCATION_ID=us-central1"/>
<action application="set" data="GOOGLE_SPEECH_MODEL=long"/>

<!-- Features -->
<action application="set" data="GOOGLE_SPEECH_ENABLE_AUTOMATIC_PUNCTUATION=true"/>
<action application="set" data="GOOGLE_SPEECH_ENABLE_SPEAKER_DIARIZATION=true"/>

<!-- Pusher (optional) -->
<action application="set" data="PUSHER_APP_ID=xxx"/>
<action application="set" data="PUSHER_KEY=xxx"/>
<action application="set" data="PUSHER_SECRET=xxx"/>
<action application="set" data="PUSHER_CLUSTER=us2"/>
```

### API Usage

```xml
<!-- Start transcription -->
<action application="uuid_google_transcribev2" data="${uuid} start en-US interim stereo 16k"/>

<!-- Stop transcription -->
<action application="uuid_google_transcribev2" data="${uuid} stop"/>
```

**Parameters:**
- `lang-code`: Language (e.g., `en-US`, `es-ES`)
- `interim`: Include interim results
- `mono|mixed|stereo`: Audio mode
- `8k|16k`: Sample rate

## Events

| Event | Description |
|-------|-------------|
| `google_transcribev2::connect` | Connected to Google API |
| `google_transcribev2::transcription` | Transcription result |
| `google_transcribev2::disconnect` | Session ended |
| `google_transcribev2::error` | Error occurred (with structured error info) |

## Error Handling

The module provides structured error handling with categorized error codes for better debugging and monitoring.

### Error Categories

| Category | Code Range | Description |
|----------|------------|-------------|
| `configuration` | 100-199 | Invalid config, missing credentials |
| `connection` | 200-299 | Network, DNS, TLS failures |
| `authentication` | 300-399 | Auth failures, expired tokens |
| `grpc` | 400-499 | gRPC-specific errors |
| `api` | 500-599 | Google API errors (quota, invalid request) |
| `audio` | 600-699 | Audio format, encoding issues |
| `internal` | 700-799 | Internal module errors |
| `timeout` | 800-899 | Connection/stream timeouts |
| `resource` | 900-999 | Memory, queue full |
| `pusher` | 1000-1099 | Pusher delivery errors |

### Error Event Format

```json
{
  "error_code": 200,
  "category": "connection",
  "message": "Failed to connect to Google Speech API",
  "session_id": 12345,
  "retryable": true,
  "retry_count": 0
}
```

### Custom Error Handler

You can set a custom error handler callback via `SessionConfig::error_handler`:

```cpp
config.error_handler = [](const ErrorInfo& error) {
    // Handle error (non-blocking!)
    if (error.is_retryable) {
        // Maybe trigger reconnection
    }
};
```

### Error Metrics

Global error metrics are available via `GrpcManager::getInstance().getGlobalMetrics()`:

```cpp
const auto& metrics = GrpcManager::getInstance().getGlobalMetrics();
// Returns JSON: {"total_errors":5,"connection":2,"auth":1,...}
std::string json = metrics.toJson();
```

## Files

```
mod_google_transcribev2/
├── mod_google_transcribev2.c      # FreeSWITCH module entry + C interface
├── mod_google_transcribev2.h      # Types, constants, and C++ glue declarations
├── google_transcribe_glue.cpp     # C/C++ interface (async v2)
├── grpc_manager.h/cpp             # gRPC worker pool singleton
├── async_pusher_client.h/cpp      # Async HTTP client for Pusher
├── audio_queue.h/cpp              # Lock-free SPSC queue
├── google_streamer_session.h/cpp  # Async session state machine
├── error_types.h                  # Structured error codes and metrics
├── speech.proto                   # Google Speech v2 API definition
├── speech.pb.h/cc                 # Generated protobuf (make proto)
├── speech.grpc.pb.h/cc            # Generated gRPC (make proto)
└── Makefile                       # Build file
```

**Total: 12 source files (~3,700 lines)**

## License

MIT License - See LICENSE file for details.
