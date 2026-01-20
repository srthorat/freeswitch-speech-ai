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
-   `Dockerfile`: **[NEW]** Added multi-stage build to support cross-platform development (handles `libspeexdsp` dependency).

## 3. Current Status
-   **Build**: ✅ The Go code compiles successfully.
    -   *Note*: On macOS, you might see errors about `speexdsp` if running locally without `brew install speexdsp`. This is expected.
    -   *Docker*: The new `Dockerfile` builds perfectly and is the recommended way to run on Mac.
-   **Features**: All three providers are implemented and wired.

## 4. Next Steps (To Resume)

### A. Deploy to Ubuntu (End-to-End Test)
Since FreeSWITCH runs on Ubuntu, moving this service there is the logical next step.

1.  **Sync Code**: Push this repo to your git server.
2.  **Install on Ubuntu**:
    ```bash
    # Run the updated install script
    sudo bash sidecar/scripts/install_ubuntu.sh --repo-dir . --branch main
    ```
3.  **Configure**:
    Edit `/opt/google-speech-service-v2/.env`:
    ```bash
    DEFAULT_PROVIDER=aws  # or google-v2
    AWS_ACCESS_KEY_ID=...
    AWS_SECRET_ACCESS_KEY=...
    ```
4.  **Restart**:
    ```bash
    sudo systemctl restart google-speech-service-v2
    ```

### B. Verification
1.  **Check Logs**: `sudo journalctl -u google-speech-service-v2 -f`
2.  **Make a Call**: Call your FreeSWITCH number that forks audio to `ws://127.0.0.1:8088/audio-fork`.
3.  **Verify Transcription**:
    -   If AWS, check logs for `aws stream started`.
    -   If Google, check logs for `google stream started`.

## 5. Useful Commands

**Run Locally (Docker):**
```bash
docker build -t sidecar-unified .
docker run --env-file .env -p 8088:8088 sidecar-unified
```

**Run Locally (Mac Native - requires speexdsp):**
```bash
go build -o sidecar ./cmd/sidecar
./sidecar
```
