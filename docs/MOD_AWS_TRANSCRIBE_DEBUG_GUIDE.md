# mod_aws_transcribe - Complete Debugging Guide

**Document Version:** 1.0  
**Date:** December 10, 2025  
**Module Status:** ✅ FULLY OPERATIONAL - PRODUCTION READY

---

## Related Documentation

**For debugging other transcription services in this project:**

| Service | Debug Guide | Module Docs | Status |
|---------|------------|-------------|--------|
| **AWS Transcribe** | 📖 **This Document** | [mod_aws_transcribe/](../modules/mod_aws_transcribe/) | ✅ Production Ready |
| **Deepgram** | [Debug Guide](MOD_DEEPGRAM_TRANSCRIBE_DEBUG_GUIDE.md) | [mod_deepgram_transcribe/](../modules/mod_deepgram_transcribe/) | ✅ Production Ready |
| **Google** | See module README | [mod_google_transcribev2/](../modules/mod_google_transcribev2/) | ✅ Available |
| **Azure** | See module README | [mod_azure_transcribe/](../modules/mod_azure_transcribe/) | ✅ Available |

**General Documentation:**
- 📘 [Installation Guide](INSTALLATION.md) - Setup all modules
- 📘 [Module Comparison](MODULE_COMPARISON.md) - Feature comparison across services
- 📘 [Stereo Channel Assignment](STEREO_CHANNEL_ASSIGNMENT.md) - Multi-channel audio

---

## Executive Summary

This document provides a comprehensive guide for debugging `mod_aws_transcribe`. The module is designed for high-scale enterprise deployments and includes specific handling for AWS authentication, audio resampling, and channel identification.

### Key Features & Fixes
- **Automatic Resampling**: Automatically upsamples 8kHz audio to 16kHz (AWS standard) to prevent "chipmunk" audio effects.
- **Channel Identification**: Full support for separating Caller (Channel 0) and Callee (Channel 1) in stereo recordings.
- **Robust Authentication**: Supports Environment Variables, Channel Variables, and AWS Instance Profiles (IAM Roles).

---

## Quick Issue Reference

**Use this table to quickly find information about any issue encountered.**

| Symptom | Probable Cause | Fix |
|---------|---------------|-----|
| **"Chipmunk" or Fast Audio** | Sample rate mismatch (8k sent as 16k) | [Fix: Resampling](#1-audio-quality-speed-issues-resampling) |
| **"The security token included in the request is invalid"** | Missing `AWS_SESSION_TOKEN` for temporary creds | [Fix: Authentication](#2-authentication-failures) |
| **"InvalidSignatureException"** | System clock skew or wrong Secret Key | [Fix: Clock Sync](#3-connection-errors) |
| **No Channel ID in results** | Feature not enabled in config | [Fix: Channel ID](#4-missing-channel-identification) |
| **Connection Timeout** | Firewall or Wrong Region | [Fix: Network](#3-connection-errors) |

---

## Common Issues & Fixes

### 1. Audio Quality / Speed Issues (Resampling)

**Symptom:**
- Transcribed text is gibberish.
- Audio sounds extremely fast (like chipmunks) or slow.
- Logs show `rate 16000` but source audio is 8000Hz.

**Root Cause:**
AWS Transcribe Streaming generally expects 16kHz audio (or 8kHz if explicitly requested, but 16kHz is preferred for quality). If 8kHz audio is sent to a 16kHz stream without resampling, it plays at 2x speed.

**Solution:**
The module now includes **automatic Speex resampling**.
- **Verify:** Check logs for `Created resampler: 8000 -> 16000`.
- **Configuration:** No manual configuration needed. The module detects the mismatch and initializes the resampler automatically.

### 2. Authentication Failures

**Symptom:**
- Logs show `The security token included in the request is invalid`.
- Logs show `The security token is not present in the request`.

**Root Cause:**
Using temporary AWS credentials (starting with `ASIA...`) without providing the Session Token.

**Solution:**
Ensure `AWS_SESSION_TOKEN` is provided.

**Environment Variables (Docker/Systemd):**
```bash
export AWS_ACCESS_KEY_ID=ASIA...
export AWS_SECRET_ACCESS_KEY=...
export AWS_SESSION_TOKEN=...  # REQUIRED for ASIA keys
```

**Channel Variables (Dialplan):**
```xml
<action application="set" data="AWS_ACCESS_KEY_ID=${user_data(...)}"/>
<action application="set" data="AWS_SECRET_ACCESS_KEY=${user_data(...)}"/>
<action application="set" data="AWS_SESSION_TOKEN=${user_data(...)}"/>
```

### 3. Connection Errors

**Symptom:**
- `InvalidSignatureException`
- `Connection timed out`

**Root Causes:**
1.  **Clock Skew:** AWS uses SigV4 signing which requires the system clock to be within 5 minutes of AWS servers.
    *   **Fix:** Run `sudo ntpdate pool.ntp.org` or check `date`.
2.  **Region Mismatch:** The client is connecting to a region different from where the credentials are valid.
    *   **Fix:** Verify `AWS_REGION` matches your infrastructure (e.g., `us-east-1`).

### 4. Missing Channel Identification

**Symptom:**
- Transcription results do not contain `"channel_id": "ch_0"` or `"ch_1"`.
- Speakers are mixed together.

**Root Cause:**
Channel identification is not enabled by default to save costs/bandwidth.

**Solution:**
Enable it explicitly in the dialplan or application logic.

```xml
<action application="set" data="AWS_ENABLE_CHANNEL_IDENTIFICATION=true"/>
<action application="set" data="AWS_NUMBER_OF_CHANNELS=2"/>
<action application="set" data="api_on_answer=uuid_aws_transcribe ${uuid} start en-US interim stereo"/>
```

---

## Debugging Steps

### 1. Enable Debug Logging

To see detailed AWS SDK logs and module internal logs:

1.  **FreeSWITCH Console:**
    ```bash
    fs_cli -x "console loglevel debug"
    fs_cli -x "fsctl loglevel debug"
    ```

2.  **Check Logs:**
    ```bash
    tail -f /usr/local/freeswitch/log/freeswitch.log | grep aws
    ```

### 2. Verify Audio Stream

If you suspect audio issues, you can capture the raw audio being sent to the module using `mod_audio_fork`'s debugging or by recording the call in FreeSWITCH.

**Record the call to verify source quality:**
```xml
<action application="record_session" data="/tmp/debug_recording.wav"/>
```
Listen to `/tmp/debug_recording.wav`. If it sounds bad, the issue is upstream (SIP provider, codec).

### 3. Test Network Connectivity

Verify you can reach the AWS Transcribe endpoint:

```bash
# For us-east-1
nc -zv transcribestreaming.us-east-1.amazonaws.com 443
```

### 4. Validate Credentials with AWS CLI

If the module fails to authenticate, try the same credentials with the AWS CLI on the same machine:

```bash
export AWS_ACCESS_KEY_ID=...
export AWS_SECRET_ACCESS_KEY=...
export AWS_SESSION_TOKEN=...
aws transcribe list-transcription-jobs --region us-east-1
```
If this fails, the credentials are invalid.

---

## Architecture Overview

`mod_aws_transcribe` uses a high-performance, non-blocking architecture:

1.  **FreeSWITCH Media Bug:** Captures audio frames (READ/WRITE) from the session.
2.  **Ring Buffer:** Audio is pushed into a lock-free ring buffer.
3.  **Worker Thread:** A dedicated thread (managed by AWS SDK's async executor) pulls audio from the buffer and sends it to AWS via HTTP/2.
4.  **Response Handler:** AWS SDK callbacks receive transcription events and fire FreeSWITCH events (`aws_transcribe::transcription`).

**Key Components:**
- `aws_transcribe_glue.cpp`: Interface between FreeSWITCH and C++ logic. Handles Resampling.
- `audio_pipe.cpp`: Manages the AWS SDK Client, Stream, and Authentication.
- `mod_aws_transcribe.cpp`: Module entry point and API command handling.

