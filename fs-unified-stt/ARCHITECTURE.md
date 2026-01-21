# Architecture: FreeSWITCH + Unified Sidecar STT

This diagram illustrates the high-level architecture of the real-time speech-to-text integration.

```mermaid
graph TD
    %% Nodes
    Caller[Caller (Phone)]
    Callee[Callee (Phone)]
    FS[FreeSWITCH]
    MAF[mod_audio_fork]
    Sidecar[Unified Sidecar (Go)]
    
    %% STT Providers (Select One)
    subgraph Cloud STT Providers
        GCP_V2[Google Cloud V2]
        GCP_V1[Google Cloud V1]
        AWS[AWS Transcribe]
    end
    
    %% External Services
    Pusher[Pusher (Real-time Msg)]
    ClientApp[Web Client / Dashboard]

    %% Connections
    Caller -->|RTP Audio| FS
    Callee -->|RTP Audio| FS
    
    FS -->|Internal Audio| MAF
    
    %% WebSocket Connection
    MAF -->|WebSocket (Start/Audio/Stop)| Sidecar
    MAF -.->|Metadata (JSON)| Sidecar
    
    %% Sidecar Processing
    Sidecar -->|gRPC / HTTP2 Stream| GCP_V2
    Sidecar -->|gRPC Stream| GCP_V1
    Sidecar -->|HTTP2 Stream| AWS
    
    %% Results
    GCP_V2 -->|Transcripts| Sidecar
    GCP_V1 -->|Transcripts| Sidecar
    AWS -->|Transcripts| Sidecar
    
    %% Publishing
    Sidecar -->|Publish JSON| Pusher
    Pusher -->|Subscribe| ClientApp
    
    %% Metadata Flow Note
    note1[Metadata Handling:<br/>1. Dialplan sets vars<br/>2. MAF sends flat JSON<br/>3. Sidecar normalizes to Struct<br/>4. Sidecar maps Channel -> Speaker]
    
    Sidecar -.-> note1
```

## Data Flow Description

1.  **Call Initiation**:
    - A call is established in **FreeSWITCH**.
    - The **Dialplan** triggers `uuid_audio_fork` based on logic (e.g., `enable_audio_fork=true`).

2.  **Audio Forking**:
    - **`mod_audio_fork`** connects to the **Unified Sidecar** via WebSocket (`ws://127.0.0.1:9088/audio-fork`).
    - It sends an initial **Metadata Handshake** (JSON containing Caller/Callee info, Call ID, etc.).
    - It streams raw Audio (Linear PCM, 8kHz, Stereo) in real-time.

3.  **Sidecar Processing**:
    - The **Sidecar** parses the metadata (handling both legacy flat JSON and nested formats).
    - It initializes the selected **Cloud Provider** (Google V2, AWS, or V1) based on config/metadata.
    - It forwards the audio stream to the Cloud Provider.

4.  **Transcription**:
    - The Cloud Provider processes audio and returns interim/final transcripts.
    - The Sidecar receives these results.

5.  **Publishing**:
    - The Sidecar maps the **Channel ID** (0 = Caller, 1 = Callee) to the **Speaker Name** using the initial metadata.
    - It formats a standardized JSON event.
    - It publishes the event to **Pusher**.

6.  **Consumption**:
    - **Client Apps** (Frontend) subscribe to the Pusher channel (using `sip_call_id`) to display real-time transcripts.
