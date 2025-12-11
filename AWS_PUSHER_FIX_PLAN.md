# AWS Pusher Fix - Implementation Plan

## Problem Statement
AWS module Pusher integration failing with:
```
2025-12-11 14:40:13.592160 86.20% [ERR] async_http.cpp:109 Pusher HTTP 400: Invalid JSON provided (could not parse)
```

Deepgram module works perfectly with identical Pusher configuration.

## Current State Analysis

### Deepgram (WORKING)
- **Language**: C (async_pusher.c / async_http.c)
- **Body logged**: `{"name":"session-start","channels":["call-XXX"],"data":"{\"type\":\"session_start\",...}"}`
- **Result**: ✅ HTTP 200

### AWS (BROKEN)
- **Language**: C++ (async_pusher.cpp / async_http.cpp)
- **Body logged**: `{"name":"session-start","channels":["call-XXX"],"data":"{\"type\":\"session_start\",...}"}`
- **Result**: ❌ HTTP 400 "Invalid JSON provided"

### Key Observation
**The logged JSON bodies appear IDENTICAL**, yet Pusher rejects AWS but accepts Deepgram!

This suggests the issue is NOT in JSON construction, but in **HOW** the body is sent over HTTP.

## Root Cause Hypothesis

Based on code review, potential differences:

1. **Character Encoding**
   - C++ std::string vs C char* might have encoding differences
   - UTF-8 BOM or encoding headers

2. **String Length Calculation**
   - C++ `.length()` vs C `strlen()`
   - Possible mismatch with Content-Length header

3. **HTTP Headers**
   - Deepgram: `"Content-Type: application/json"`
   - AWS: `"Content-Type: application/json"` + `"User-Agent: ..."` + `"Connection: keep-alive"`
   - Extra headers might cause issues

4. **CURL Options**
   - Different curl_easy_setopt() calls between C and C++ versions
   - Possible POSTFIELDSIZE mismatch

## Implementation Options

### Option 1: Replace AWS C++ with Deepgram C Pattern (RECOMMENDED)
**Pros**: Guaranteed to work (proven implementation)
**Cons**: Requires rewriting async_pusher.cpp

### Option 2: Debug Exact HTTP Difference
**Pros**: Minimal changes to AWS code
**Cons**: Time-consuming, may miss subtle issues

### Option 3: Use tcpdump/Wireshark to Compare
**Pros**: See exact bytes on wire
**Cons**: Requires network capture access

## Recommended Fix

**Copy Deepgram's working pattern to AWS module**:

1. Keep existing async_http.cpp (HTTP engine is fine)
2. Rewrite async_pusher.cpp to match Deepgram's logic EXACTLY:
   - Use same escape function (C-style)
   - Use same body construction (snprintf vs string concatenation)
   - Use same header setup
   - Remove extra headers (User-Agent, Connection)

3. Test with exact same Pusher credentials

## Implementation Steps

```bash
# 1. Backup current AWS pusher
cp modules/mod_aws_transcribe/async_pusher.cpp modules/mod_aws_transcribe/async_pusher.cpp.backup

# 2. Copy Deepgram pattern structure
# 3. Adapt for C++ (keep std::string for compatibility)
# 4. Match Deepgram's exact JSON construction
# 5. Remove extra HTTP headers
# 6. Test
```

## Code Changes Needed

### async_pusher.cpp - Use C-style string building

**BEFORE** (AWS - BROKEN):
```cpp
std::string escaped_data;
for (char c : data) {
    switch (c) {
        case '"':  escaped_data += "\\\""; break;
        ...
    }
}
std::string body = "{\"name\":\"" + event + "\",\"channels\":[\"" + channel + "\"],\"data\":\"" + escaped_data + "\"}";
```

**AFTER** (Match Deepgram - WORKING):
```cpp
// Use snprintf like Deepgram instead of string concatenation
char body[8192];
char* escaped = escape_json_string_c_style(data.c_str());
snprintf(body, sizeof(body),
    "{\"name\":\"%s\",\"channels\":[\"%s\"],\"data\":\"%s\"}",
    event.c_str(), channel.c_str(), escaped);
free(escaped);
```

### Remove Extra Headers

**BEFORE** (AWS):
```cpp
headers.push_back("Content-Type: application/json");
headers.push_back("User-Agent: FreeSWITCH-AWS-Transcribe/1.0");  // REMOVE
headers.push_back("Connection: keep-alive");  // REMOVE
```

**AFTER** (Match Deepgram):
```cpp
headers.push_back("Content-Type: application/json");
// That's it - nothing else
```

## Testing Plan

1. Deploy fix
2. Make test call
3. Check logs for:
   - ✅ Pusher HTTP 200 (not 400)
   - ✅ No "Invalid JSON" errors
4. Verify events arrive in Pusher dashboard
5. Compare with Deepgram behavior

## Success Criteria

- ✅ AWS module sends Pusher events successfully (HTTP 200)
- ✅ No HTTP 400 errors in logs
- ✅ Events visible in Pusher dashboard
- ✅ Same behavior as Deepgram module

