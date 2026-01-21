# Ubuntu build & run (Google Speech Service v2)

## Install
From repo root:

```bash
sudo chmod +x sidecar/scripts/install_ubuntu.sh
sudo bash sidecar/scripts/install_ubuntu.sh --repo-dir "$(pwd)" --branch main
```

## Configure
Edit:
```bash
sudo nano /opt/google-speech-service-v2/.env
```

Copy your service account json:
```bash
sudo cp /path/to/sa.json /opt/google-speech-service-v2/sa.json
sudo chown google-speech-v2:google-speech-v2 /opt/google-speech-service-v2/sa.json
sudo chmod 0640 /opt/google-speech-service-v2/sa.json
```

Restart:
```bash
sudo systemctl restart google-speech-service-v2
```

## Verify
Health:
```bash
curl -s http://127.0.0.1:8089/healthz && echo
```

Metrics:
```bash
curl -s http://127.0.0.1:8089/metrics | head
```

Logs:
```bash
sudo journalctl -u google-speech-service-v2 -f
```

## FreeSWITCH expectations
Service expects:
- first WS message: JSON metadata (text frame)
- subsequent messages: PCM16 binary audio (stereo interleaved preferred)

WS URL used by FreeSWITCH:
- `ws://127.0.0.1:8088/audio-fork`

## Scaling notes
For 1000–3000 concurrent WS connections:
- Ensure `LimitNOFILE=200000` (systemd unit does this)
- Ensure host has sufficient CPU (resampling + TLS + gRPC)
- Ensure Google STT quotas allow your concurrency

Tune:
- `GOOGLE_SPEECH_V2_MAX_BUFFER_MS` (lower = lower latency, higher = more tolerance)
- `GCP_STREAM_IDLE_SECS`

## Troubleshooting
### Pusher channel fails
You asked for raw `sip_call_id` as channel. If Call-ID contains invalid chars for Pusher, you will see warnings:
`pusher channel may contain invalid characters`
In that case, update frontend or add a mapping later.

### Google channel tags
If you are not receiving `channel_tag` values, you may need to update proto fields in:
- `internal/google/client.go`
Based on the exact `speechpb` version.