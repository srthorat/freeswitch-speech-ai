# Google Speech Service v2: FreeSWITCH + Google STT v2 (Stereo) + Pusher

## Purpose
- Accept WebSocket connections from FreeSWITCH `mod_audio_fork`
- Receive initial metadata text frame (JSON)
- Receive binary audio frames (PCM16, stereo recommended)
- Resample audio to 16 kHz using **speexdsp** (cgo)
- Stream to Google Speech-to-Text v2 (streaming)
- Publish transcripts to Pusher using channel name = `sip_call_id` (as-is)

## Concurrency notes
This service is designed for high density:
- WS read path is minimal (drains into ring buffer)
- Separate sender loop drains ring buffer and sends to Google
- Receiver loop publishes interim/final transcripts
- Ring buffer is bounded (drop-oldest) to preserve real-time latency at scale

You still need to plan:
- Google STT quotas (concurrent streams)
- CPU/network per host
- Linux `nofile` limits

## Metadata format (first WS text frame)
```json
{
  "uuid": "FS_UUID",
  "sip_call_id": "sip-call-id-from-sofia",
  "sample_rate": 8000,
  "channels": 2,
  "lang": "en-US",
  "location": "us-central1",
  "stereo_swap": false,
  "caller": { "name": "Customer", "phone": "+91..." },
  "callee": { "name": "Agent", "phone": "+91..." }
}
```

## Pusher events
- `transcript-interim`
- `transcript-final`

Both contain:
- `uuid`, `sip_call_id`
- `channel` (0/1 normalized)
- `role` (caller/callee)
- `caller` and `callee` objects
- `text`, `confidence`, `is_final`

## Build (Ubuntu)
See: `docs/UBUNTU_BUILD_RUN.md`

## FreeSWITCH usage
Start:
```
uuid_audio_fork <uuid> start ws://127.0.0.1:8088/audio-fork stereo <sampling> <metadata-json>
```

Stop:
- WS closes when you call `uuid_audio_fork <uuid> stop ...` (your module closes the socket).