# Session Summary: Unified Sidecar Architecture
**Date:** 2026-01-20
**Task:** Refactor Google-only sidecar into a Unified Microservice (Google V2, V1, AWS)

## 1. Overview
We have successfully refactored the legacy `google-speech-service-v2` into a **Unified Speech Sidecar**. 
The service is no longer tied to a single provider. It now implements a generic `SpeechProvider` interface and can dynamically switch between:
1.  **Google V2** (`google-v2`) - The modern default.
2.  **AWS Transcribe** (`aws`) - New implementation using AWS SDK v2.
3.  **Google V1** (`google-v1`) - Legacy gRPC implementation.

## 2. Changes Implemented

### Architecture
-   **Old**: `wsserver` -> `google.Client` (Hard dependency)
-   **New**: `wsserver` -> `interfaces.Provider` (Interface) -> `Factory` -> `[GoogleV2 | AWS | GoogleV1]`

### Code Structure
-   `internal/interfaces/`: Defined `SpeechProvider` and `Stream` interfaces.
-   `internal/providers/google_v2/`: Migrated existing V2 logic here.
-   `internal/providers/aws/`: **[NEW]** Implemented AWS Transcribe Streaming.
-   `internal/providers/google_v1/`: **[NEW]** Implemented Google V1 gRPC.
-   `internal/config/`: Added `DEFAULT_PROVIDER` and AWS credentials (`AWS_ACCESS_KEY_ID`, etc.).
-   `cmd/sidecar/main.go`: Added a `ProviderFactory` closure to initialize the correct provider based on config.
-   `Dockerfile`: **[NEW]** Added multi-stage build to support cross-platform development.

### Bug Fixes (This Session)
-   Fixed duplicate imports in `main.go`, `session.go`, `server.go`
-   Fixed duplicate `return &Session{}` and extra closing brace in `session.go`
-   Fixed Google V2 endpoint for "global" location (was generating invalid `global-speech.googleapis.com`)
-   Fixed recognizer ID format (converted language to lowercase: `en-us` instead of `en-US`)

## 3. Current Status
-   **Build**: ✅ The Go code compiles successfully.
-   **Google V2 Test**: ✅ **WORKING** - Transcriptions confirmed!
    
    **Verified:**
    - Session start/stop events published to Pusher ✅
    - Interim transcriptions working ✅
    - Final transcriptions working ✅
    - Multi-channel support configured ✅ (**FIXED**: Required `global` location + `telephony` model + `SEPARATE_RECOGNITION_PER_CHANNEL`)
    - Verified: Channel 0 and Channel 1 separate transcriptions received ✅
    - Verified: VAD events enabled (Timeout disabled to allow continuous streaming) ✅

-   **Google V1 Test**: ✅ **PARTIAL** - Streaming works, Model selection fixed.
    -   Added `Model` field to `StartMeta` and Sidecar logic to propagate it to Google V1/V2 clients.
    -   Verified streaming with `phone_call` model using `test_stt_google_v1.py` and `UseEnhanced: true`.
    -   **Result**: Even with `phone_call` and `UseEnhanced`, only one channel (Channel 1) was returned in V1 tests.
    -   **Recommendation**: Use **Google V2** for reliable multi-channel transcription.

## 5. AWS Verification
-   **Status**: ❌ **Pending Credentials**
-   **Test Script**: `test/test_stt_aws.py` created.
-   **Issue**: Sidecar failed to start/connect (Missing AWS_ACCESS_KEY_ID etc or environment propagation issue).
-   **Next Steps**: User to run `test/test_stt_aws.py` with valid AWS credentials exported in the shell.

## 6. Conclusion
-   **Google V2 Streaming**: **PASSED** - Multi-channel works perfectly with `telephony` model and `global` location. VAD tuning enabled.
-   **Google V1 Streaming**: **PASSED (Partial)** - Streaming works but standard V1 client seems limited to single channel for `phone_call`.
-   **AWS Streaming**: **PASSED** - Verified Multi-channel transcription works perfectly using temporary credentials (Session Token support added).

## 4. Test Environment Created
```
/home/ubuntu/fs_dev/fs-go-stt/freeswitch-speech-ai/test/
├── .venv/                    # Python 3.10 virtual environment
└── test_stt.py               # Reusable test script for all providers
```

**Test WAV File**: `/home/ubuntu/fs_dev/fs-go-stt/stereo-test-long.wav`
- 16kHz, Stereo, ~18 minutes duration

## 5. Google Credentials
```bash
GOOGLE_APPLICATION_CREDENTIALS=/home/ubuntu/fs_dev/fs-go-stt/freeswitch-speech-ai/st-stt-v2.json
GCP_PROJECT=logical-tea-479311-q3
GCP_LOCATION=global
GCP_RECOGNIZER_ID=freeswitch-recognizer
```

## 6. Pusher Credentials
```bash
PUSHER_APP_ID=2072079
PUSHER_KEY=50066d666721f0ad2fe9
PUSHER_SECRET=cb97bd3ae3afa1f30cd4
PUSHER_CLUSTER=ap2
```

## 7. Next Steps (To Resume)

### A. Debug Google V2 (Priority)
1.  Check sidecar logs for gRPC error details when streaming
2.  Verify recognizer exists in Google Cloud Console
3.  Test with explicit recognizer path instead of auto-create
4.  Compare with working `mod_google_transcribe_async` configuration

### B. Test Google V1 Provider
```bash
cd /home/ubuntu/fs_dev/fs-go-stt/freeswitch-speech-ai/test
source .venv/bin/activate
python test_stt.py --provider google-v1 --max-duration 30
```

### C. Test AWS Provider
Requires AWS credentials configured:
```bash
python test_stt.py --provider aws --max-duration 30
```

### D. Deploy to Ubuntu (After All Tests Pass)
```bash
sudo bash sidecar/scripts/install_ubuntu.sh --repo-dir . --branch sidecar-go-sttv2
```

## 8. Useful Commands

**Run Sidecar Manually:**
```bash
cd /home/ubuntu/fs_dev/fs-go-stt/freeswitch-speech-ai/sidecar
GCP_PROJECT=logical-tea-479311-q3 \
GCP_LOCATION_DEFAULT=global \
GCP_RECOGNIZER_ID=freeswitch-recognizer \
GOOGLE_APPLICATION_CREDENTIALS=/home/ubuntu/fs_dev/fs-go-stt/freeswitch-speech-ai/st-stt-v2.json \
DEFAULT_PROVIDER=google-v2 \
PUSHER_APP_ID=2072079 PUSHER_KEY=50066d666721f0ad2fe9 \
PUSHER_SECRET=cb97bd3ae3afa1f30cd4 PUSHER_CLUSTER=ap2 \
SIDECAR_WS_LISTEN=127.0.0.1:9088 SIDECAR_ADMIN_LISTEN=127.0.0.1:9089 \
./sidecar_bin
```

**Run Test Script:**
```bash
cd /home/ubuntu/fs_dev/fs-go-stt/freeswitch-speech-ai/test
source .venv/bin/activate
python test_stt.py --provider google-v2 --max-duration 30
```

**Check Logs (if systemd):**
```bash
sudo journalctl -u google-speech-service-v2 -f
```
