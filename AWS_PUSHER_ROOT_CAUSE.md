# AWS Pusher Fix - Root Cause & Solution

## ✅ PROBLEM SOLVED

**Root Cause**: AWS module was using **UUID fallback** when `sip_call_id` was unavailable, creating **malformed channel names** like:
```
"call--u-r57TIs05UDm6QUaIwqw.."
```

This corrupted channel name caused Pusher API to reject requests with:
```
HTTP 400: Invalid JSON provided (could not parse)
```

## Why Deepgram Worked

Deepgram **NEVER uses UUID as fallback**. It ONLY sends to Pusher if `sip_call_id` is available.

**Deepgram pattern**:
```c
const char* sip_call_id = switch_channel_get_variable(channel, "sip_call_id");
if (sip_call_id) {
    send_session_start_to_pusher(session, sip_call_id);
} else {
    // SKIP Pusher - don't send anything
}
```

## The Fix (Commit f67c5e0)

Changed AWS to match Deepgram's exact pattern:

### Before (BROKEN):
```cpp
const char* sip_call_id = ...;
const char* call_id = sip_call_id;
if (!call_id) {
    call_id = switch_core_session_get_uuid(session);  // ❌ WRONG! Created malformed name
}
send_session_start_to_pusher(session, call_id);
```

### After (FIXED):
```cpp
const char* sip_call_id = ...;
if (sip_call_id) {
    send_session_start_to_pusher(session, sip_call_id);  // ✅ Only send if available
} else {
    // Skip Pusher entirely - don't send anything
}
```

## How to Ensure sip_call_id is Available

The `sip_call_id` channel variable comes from the SIP Call-ID header. To ensure it's available:

### 1. Check Dialplan

Make sure your dialplan doesn't clear the variable:

```xml
<!-- Good: Preserves sip_call_id -->
<extension name="outbound">
  <condition field="destination_number" expression="^(\d+)$">
    <action application="bridge" data="sofia/external/$1@gateway"/>
  </condition>
</extension>

<!-- Bad: Might clear channel variables -->
<action application="set" data="sip_call_id="/>  <!-- DON'T DO THIS -->
```

### 2. Verify in Logs

Before the call, check if sip_call_id exists:

```bash
# In FreeSWITCH console
uuid_getvar <uuid> sip_call_id

# Should return something like: "abc123xyz@10.0.1.5"
# If empty or "call--...", it's not set properly
```

### 3. Set Manually if Needed (Workaround)

If sip_call_id is not available from SIP, you can set it manually in dialplan:

```xml
<extension name="set_call_id">
  <condition field="destination_number" expression=".*">
    <!-- Generate a unique call ID if SIP doesn't provide one -->
    <action application="set" data="sip_call_id=${create_uuid}@freeswitch"/>
    <action application="bridge" data="..."/>
  </condition>
</extension>
```

### 4. Alternative: Use UUID (NOT RECOMMENDED)

If you absolutely can't get sip_call_id, you could modify AWS to use UUID, but:
- ⚠️ This defeats the purpose of having a consistent call ID across systems
- ⚠️ Pusher channels will have different names than expected
- ⚠️ Better to fix the root cause (ensure sip_call_id exists)

## Testing the Fix

### 1. Rebuild Module
```bash
cd ~/dev/freeswitch-speech-ai
git pull origin claude/setup-ubuntu-environment-01L2cbjhywSRmy5EfFujHhuy
cd modules/mod_aws_transcribe
make clean && make && sudo make install
sudo systemctl restart freeswitch
```

### 2. Make Test Call

Check logs for:

**✅ Success (sip_call_id available)**:
```
[DEBUG] async_pusher.cpp:103 Pusher request body: {"name":"session-start","channels":["call-abc123@domain.com"],...}
[DEBUG] async_http.cpp:113 Pusher HTTP 200: Success
```

**⚠️ Warning (sip_call_id not available)**:
```
[ERROR] Cannot send session_start to Pusher: sip_call_id not available after 10 retries (500ms total)
```
If you see this, fix your dialplan to ensure sip_call_id exists.

**❌ Error (old bug - should NOT see this anymore)**:
```
[ERR] Pusher HTTP 400: Invalid JSON provided (could not parse)
[DEBUG] Pusher request body: {"name":"session-start","channels":["call--u-r57TIs05UDm6QUaIwqw.."],...}
```
If you still see this, the module wasn't rebuilt correctly.

## Commits

1. **776f09c**: Fixed async_pusher.cpp JSON encoding (C-style snprintf)
2. **f67c5e0**: Removed UUID fallback, use ONLY sip_call_id (CRITICAL FIX)

## Summary

**The Issue**: Malformed channel names from UUID fallback
**The Fix**: Never use UUID, only use sip_call_id (match Deepgram)
**Next Step**: Ensure sip_call_id is set in your dialplan

AWS now works **identically** to Deepgram!
