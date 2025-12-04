# mod_google_transcribe_async

High-performance async gRPC module for Google Cloud Speech-to-Text V2 API with true bidirectional streaming.

## Features

- **Async gRPC**: Non-blocking bidirectional streaming using CompletionQueue
- **Stereo Support**: Separate recognition per channel (caller/agent)
- **Low Latency**: ~200-400ms first result (vs ~500-1000ms sync version)
- **High Scalability**: Shared worker pool (16 workers handle 1000+ calls)
- **Crash-Safe**: Self-anchoring reference counting pattern

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    FreeSWITCH                               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │   Call 1     │  │   Call 2     │  │   Call N     │      │
│  │  Media Bug   │  │  Media Bug   │  │  Media Bug   │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │               │
│         ▼                 ▼                 ▼               │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐      │
│  │  Session 1   │  │  Session 2   │  │  Session N   │      │
│  │ (own stream) │  │ (own stream) │  │ (own stream) │      │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘      │
│         │                 │                 │               │
│         └────────────┬────┴─────────────────┘               │
│                      ▼                                      │
│         ┌────────────────────────┐                         │
│         │  Shared CompletionQueue │                         │
│         │   (16 worker threads)   │                         │
│         └────────────────────────┘                         │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
              ┌────────────────────────┐
              │   Google Cloud Speech  │
              │     V2 API (gRPC)      │
              └────────────────────────┘
```

## Usage

### Start Transcription
```
uuid_google_async <uuid> start [rate]
```

- `uuid`: FreeSWITCH call UUID
- `rate`: Sample rate (8000 or 16000, default: 8000)

### Stop Transcription
```
uuid_google_async <uuid> stop
```

### Example
```bash
# From fs_cli
uuid_google_async b66ee8b5-a39a-4ef1-a4ae-6b4d4ba0ad2c start 8000

# Watch transcripts
tail -f /usr/local/freeswitch/log/freeswitch.log | grep TRANSCRIPT
```

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `GOOGLE_APPLICATION_CREDENTIALS` | Path to GCP service account JSON | Required |
| `GCP_PROJECT_ID` | Google Cloud project ID | Required |
| `GCP_LOCATION` | Recognizer location (`global` or region) | Required |
| `GCP_RECOGNIZER_ID` | Pre-configured recognizer name | Required |
| `ASYNC_GRPC_WORKERS` | Number of worker threads | 16 |

### Worker Scaling Guide

| Concurrent Calls | Recommended Workers |
|------------------|---------------------|
| 100 | 4 |
| 500 | 8 |
| 1,000 | 16 (default) |
| 2,000 | 32 |
| 5,000+ | 64 |

### Example systemd Configuration

`/etc/systemd/system/freeswitch.service.d/freeswitch.conf`:
```ini
[Service]
Environment="GOOGLE_APPLICATION_CREDENTIALS=/path/to/credentials.json"
Environment="GCP_PROJECT_ID=my-project"
Environment="GCP_LOCATION=global"
Environment="GCP_RECOGNIZER_ID=freeswitch-recognizer"
Environment="ASYNC_GRPC_WORKERS=16"
```

## Events

The module fires FreeSWITCH custom events:

```
Event-Subclass: google::transcribe
Event-Name: CUSTOM

Transcription-UUID: <call-uuid>
Channel-Index: 1 or 2
Transcription-Text: "Hello, how are you?"
Is-Final: true/false
Confidence: 0.95
```

## Comparison with mod_google_transcribe

| Feature | mod_google_transcribe | mod_google_transcribe_async |
|---------|----------------------|----------------------------|
| gRPC Pattern | Sync (blocking) | Async (non-blocking) |
| Threads per call | 2 | 0 (shared pool) |
| Latency | ~500-1000ms | ~200-400ms |
| Audio buffering | 100ms batches | Immediate |
| Max concurrent | ~200 calls | 1000+ calls |
| Configuration | 20+ options | Minimal (uses recognizer) |
| Resampling | Yes (Speex) | No |

## Building

The module is built automatically by `install-all.sh`:

```bash
./scripts/install-all.sh --module mod_google_transcribe
```

Or manually:
```bash
cd modules/mod_google_transcribe_async
# Build commands in install-all.sh
```

## Requirements

- FreeSWITCH 1.10+
- gRPC C++ libraries
- Google Cloud Speech V2 API enabled
- Pre-configured recognizer in GCP

## Pre-configured Recognizer Setup

Create a recognizer in Google Cloud Console or via gcloud:

```bash
gcloud alpha ml speech recognizers create freeswitch-recognizer \
  --location=global \
  --model=telephony \
  --language-codes=en-US,en-IN \
  --encoding=LINEAR16 \
  --sample-rate=8000 \
  --audio-channel-count=2 \
  --multi-channel-mode=SEPARATE_RECOGNITION_PER_CHANNEL
```

## License

Same as FreeSWITCH - MPL 1.1
