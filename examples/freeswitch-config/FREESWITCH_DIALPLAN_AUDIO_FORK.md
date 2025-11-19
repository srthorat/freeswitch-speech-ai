# FreeSWITCH Dialplan Configuration for mod_audio_fork

This guide provides step-by-step instructions for configuring FreeSWITCH dialplan to automatically fork audio for all calls to a WebSocket server.

## Quick Start

### Working Configuration

Add this to your `/usr/local/freeswitch/conf/dialplan/default.xml` inside the `<context name="default">` section:

```xml
<context name="default">
  <!-- GLOBAL AUDIO FORK - STARTS AFTER ANSWER -->
  <extension name="audio_fork_all_calls" continue="true">
    <condition field="destination_number" expression="^(.+)$">
      <action application="log" data="INFO [AUDIO_FORK] Setting up auto-fork for: ${caller_id_number} → ${destination_number}"/>

      <!-- Use api_on_answer for API commands (uuid_audio_fork is an API) -->
      <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}'}"/>

      <!-- Stop audio fork on hangup -->
      <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
    </condition>
  </extension>

  <!-- Your other extensions follow here -->
```

**Important:** Update the WebSocket URL to match your server:

| Environment | WebSocket URL |
|------------|---------------|
| Local development | `ws://localhost:8077/stream` |
| Docker container | `ws://host.docker.internal:8077/stream` |
| Remote server | `ws://20.244.30.42:8077/stream` |
| Secure WebSocket | `wss://audio.yourcompany.com/stream` |

### Reload FreeSWITCH Configuration

```bash
fs_cli -x 'reloadxml'
```

You should see:
```
+OK [Success]
```

### Test with a Call

1. Make a call from any extension to any destination
2. Check the FreeSWITCH console for log messages:
   ```
   [INFO] [AUDIO_FORK] Setting up auto-fork for: 1000 → 1001
   ```

3. **After the call is answered**, audio fork starts:
   ```
   mod_audio_fork: streaming 16000 sampling to 20.244.30.42:8077/stream
   ```

4. Verify audio fork is active:
   ```bash
   fs_cli -x 'show channels'  # Get the UUID
   fs_cli -x 'uuid_buglist <uuid>'
   ```

   You should see:
   ```
   1 media bug attached
   Function: audio_fork
   ```

## Per-User Control (Flag-Based)

The above configuration enables audio fork for **ALL calls**. If you want to enable audio fork **only for specific users**, use the flag-based approach:

**Example:**
- Extension 1000: `enable_audio_fork=true` → Audio fork ENABLED
- Extension 1001: `enable_audio_fork=false` → Audio fork DISABLED

**See detailed guide:** [Per-User Flag-Based Audio Fork Configuration](AUDIO_FORK_PER_USER_FLAG.md)

**Quick overview:**

1. **Set flag in user directory** (`/usr/local/freeswitch/conf/directory/default/1000.xml`):
   ```xml
   <variables>
     <!-- Only the flag is needed in user file -->
     <variable name="enable_audio_fork" value="true"/>
   </variables>
   ```

2. **Use conditional dialplan** (`/usr/local/freeswitch/conf/dialplan/default/00_audio_fork_conditional.xml`):
   ```xml
   <extension name="audio_fork_conditional" continue="true">
     <condition field="${enable_audio_fork}" expression="^true$">
       <condition field="destination_number" expression="^(.+)$">
         <!-- Audio fork settings are in dialplan, not in user files -->
         <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}'}"/>
         <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
       </condition>
     </condition>
   </extension>
   ```

This approach gives you **per-user control** over audio fork without modifying dialplan for each user.

## Common Issues and Fixes

### Issue 1: Dialplan Not Executing (No Log Messages)

**Symptom:** No `[AUDIO_FORK]` log messages appear in FreeSWITCH console, even though the dialplan file exists.

**Root Cause:** The `<condition>` tag is missing the `field` attribute.

**WRONG ❌:**
```xml
<extension name="audio_fork_all_calls" continue="true">
  <condition>  <!-- Missing field attribute! -->
    <action application="log" data="INFO [AUDIO_FORK] Starting..."/>
  </condition>
</extension>
```

**CORRECT ✅:**
```xml
<extension name="audio_fork_all_calls" continue="true">
  <condition field="destination_number" expression="^.*$">  <!-- Has field and expression -->
    <action application="log" data="INFO [AUDIO_FORK] Starting..."/>
  </condition>
</extension>
```

**Fix:**
```bash
# Edit the dialplan file
nano /usr/local/freeswitch/conf/dialplan/default/00_audio_fork_all_calls.xml

# Change:
#   <condition>
# To:
#   <condition field="destination_number" expression="^.*$">

# Reload
fs_cli -x 'reloadxml'
```

### Issue 2: Audio Fork Starts But No Stream

**Symptom:** `[AUDIO_FORK]` logs appear, but WebSocket server receives no data.

**Possible Causes:**

1. **WebSocket server not running**
   ```bash
   # Test connection
   curl -v ws://20.244.30.42:8077/stream
   ```

2. **Incorrect URL format**
   ```xml
   <!-- WRONG: Missing space between URL and mix-type -->
   <action application="uuid_audio_fork" data="${uuid} start ws://server.com/streamstereo 16k"/>

   <!-- CORRECT: Space after URL -->
   <action application="uuid_audio_fork" data="${uuid} start ws://server.com/stream stereo 16k"/>
   ```

3. **Firewall blocking WebSocket connection**
   ```bash
   # Check from FreeSWITCH server
   telnet 20.244.30.42 8077
   ```

### Issue 3: Dialplan Executes But Stops Other Extensions

**Symptom:** Audio fork works, but call doesn't complete (doesn't ring).

**Root Cause:** Missing `continue="true"` attribute.

**Fix:**
```xml
<!-- Add continue="true" to allow call processing to continue -->
<extension name="audio_fork_all_calls" continue="true">
  <condition field="destination_number" expression="^.*$">
    ...
  </condition>
</extension>
```

### Issue 4: Works Manually But Not Automatically

**Symptom:**
- Manual command works: `fs_cli -x "uuid_audio_fork <uuid> start ws://..."`
- Automatic dialplan doesn't work

**Debug Steps:**

1. **Enable debug logging:**
   ```bash
   fs_cli -x 'console loglevel 7'
   ```

2. **Check dialplan execution order:**
   ```bash
   fs_cli -x 'xml_locate dialplan' | grep -B 5 "extension name"
   ```

   Files execute **alphabetically**. If another extension (like `Local_Extension`) runs first without `continue="true"`, it will stop processing.

3. **Solution:** Use `00_` prefix to run first:
   ```bash
   mv /usr/local/freeswitch/conf/dialplan/default/audio_fork_all_calls.xml \
      /usr/local/freeswitch/conf/dialplan/default/00_audio_fork_all_calls.xml
   fs_cli -x 'reloadxml'
   ```

### Issue 5: XML Syntax Errors

**Check for XML validation errors:**

```bash
fs_cli -x 'reloadxml' 2>&1 | grep -i error
```

**Common XML errors:**

1. **Unescaped special characters in metadata:**
   ```xml
   <!-- WRONG: Unescaped quotes -->
   <action application="uuid_audio_fork" data="${uuid} start ws://server/stream stereo 16k {"test":"value"}"/>

   <!-- CORRECT: Use single quotes for JSON -->
   <action application="uuid_audio_fork" data="${uuid} start ws://server/stream stereo 16k {'test':'value'}"/>
   ```

2. **Missing closing tags:**
   ```xml
   <extension name="test">
     <condition field="destination_number" expression="^.*$">
       <action application="log" data="test"/>
     <!-- Missing </condition> -->
   </extension>
   ```

### Issue 6: Audio Fork Starts During Early Media (Before Call is Answered)

**Symptom:** Audio fork starts during call routing/early media phase instead of after the call is answered. Logs show:
```
[INFO] mod_audio_fork.c:84 Sending early media
```

**Root Cause:** `uuid_audio_fork` is an API command, not a dialplan application. Must use `api_on_answer` (not `execute_on_answer` or inline execution).

**WRONG ❌:**
```xml
<extension name="audio_fork_all_calls" continue="true">
  <condition field="destination_number" expression="^.*$">
    <!-- WRONG: This executes during ROUTING -->
    <action application="uuid_audio_fork" data="${uuid} start ws://server/stream stereo 16k"/>

    <!-- ALSO WRONG: execute_on_answer is for applications, not APIs -->
    <action application="export" data="nolocal:execute_on_answer=uuid_audio_fork ${uuid} start..."/>
  </condition>
</extension>
```

**CORRECT ✅:**
```xml
<extension name="audio_fork_all_calls" continue="true">
  <condition field="destination_number" expression="^.*$">
    <action application="log" data="INFO [AUDIO_FORK] Setting up auto-fork for: ${caller_id_number} → ${destination_number}"/>

    <!-- CORRECT: Use api_on_answer for API commands -->
    <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://server/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}'}"/>

    <!-- Stop audio fork on hangup -->
    <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
  </condition>
</extension>
```

**Key Points:**
- **`uuid_audio_fork` is an API command** - use `api_on_answer` (NOT `execute_on_answer`)
- `api_on_answer` = for API commands (run with fs_cli -x)
- `execute_on_answer` = for dialplan applications only
- `api_on_answer` executes after the called party answers
- `api_hangup_hook` automatically stops audio fork when call ends

**Verification:**
```bash
# During routing phase, you should see:
[INFO] [AUDIO_FORK] Setting up auto-fork for: 1000 → 1001

# After call is answered, you should see:
mod_audio_fork: streaming 16000 sampling to server...
# But NO "Sending early media" message
```

## Advanced Configuration

### Configuration 1: Fork Audio Only for Specific Extensions

Match only extensions 1000-1099 (starts after answer):

```xml
<extension name="audio_fork_specific_exts" continue="true">
  <condition field="caller_id_number" expression="^(10\d{2})$">
    <condition field="destination_number" expression="^(10\d{2})$">
      <action application="log" data="INFO [AUDIO_FORK] Setting up fork for ext: ${caller_id_number}"/>
      <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}'}"/>
      <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
    </condition>
  </condition>
</extension>
```

### Configuration 2: Fork Audio Only for External Calls

Match 10-digit numbers (external calls, starts after answer):

```xml
<extension name="audio_fork_external" continue="true">
  <condition field="destination_number" expression="^(\d{10})$">
    <action application="log" data="INFO [AUDIO_FORK] External call to ${destination_number}"/>
    <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'type':'external','number':'${destination_number}'}"/>
    <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
  </condition>
</extension>
```

### Configuration 3: Fork Audio with Different Mix Types

**Note:** Use `api_on_answer` to start after call is answered. Examples below show the URL format only.

#### Mono (Caller Audio Only)
```xml
<action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://server/stream mono 16k {'mode':'mono'}"/>
```

#### Mixed (Both Parties, Single Channel)
```xml
<action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://server/stream mixed 16k {'mode':'mixed'}"/>
```

#### Stereo (Caller + Callee, Separate Channels)
```xml
<action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://server/stream stereo 16k {'mode':'stereo'}"/>
```

**Stereo Channel Layout:**
- **Channel 0:** Caller audio
- **Channel 1:** Callee audio

### Configuration 4: Different Sampling Rates

#### 8kHz (Telephony Quality, Lower Bandwidth)
```xml
<action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://server/stream stereo 8k {'rate':'8000'}"/>
```
- Bandwidth: ~128 kbps (stereo)
- Use case: Basic speech recognition

#### 16kHz (High Quality, Better Transcription)
```xml
<action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://server/stream stereo 16k {'rate':'16000'}"/>
```
- Bandwidth: ~256 kbps (stereo)
- Use case: Production transcription, quality analysis

## Debugging Commands

### Check Active Calls
```bash
fs_cli -x 'show channels'
```

### Check Media Bugs on a Call
```bash
fs_cli -x 'uuid_buglist <uuid>'
```

### Check All Variables for a Call
```bash
fs_cli -x 'uuid_dump <uuid>' | grep -i audio
```

### View Dialplan Processing
```bash
fs_cli -x 'console loglevel 7'
# Make a call, watch for dialplan execution
```

### Manually Control Audio Fork (Testing)
```bash
# Start
fs_cli -x "uuid_audio_fork <uuid> start ws://20.244.30.42:8077/stream stereo 16k {'test':'manual'}"

# Send text frame
fs_cli -x "uuid_audio_fork <uuid> send_text {'event':'dtmf','digit':'5'}"

# Stop
fs_cli -x "uuid_audio_fork <uuid> stop {'reason':'manual_stop'}"
```

### Test WebSocket Server Connection
```bash
# From FreeSWITCH server
curl -v --no-buffer -H "Connection: Upgrade" -H "Upgrade: websocket" \
  -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: test" \
  ws://20.244.30.42:8077/stream
```

## File Locations

| File | Path |
|------|------|
| Dialplan files | `/usr/local/freeswitch/conf/dialplan/default/*.xml` |
| User directory | `/usr/local/freeswitch/conf/directory/default/*.xml` |
| Main config | `/usr/local/freeswitch/conf/freeswitch.xml` |
| Logs | `/usr/local/freeswitch/log/freeswitch.log` |

## Reload vs Restart

| Operation | Command | When to Use |
|-----------|---------|-------------|
| Reload XML | `fs_cli -x 'reloadxml'` | After changing dialplan, directory, or configuration files |
| Reload mod_audio_fork | `fs_cli -x 'reload mod_audio_fork'` | After updating module binary |
| Full restart | `systemctl restart freeswitch` | After changing core configuration or loading new modules |

## Complete Working Example

Here's the complete, tested configuration for forking audio on all calls **AFTER they are answered**:

**File:** `/usr/local/freeswitch/conf/dialplan/default/00_audio_fork_all_calls.xml`

```xml
<include>
  <!--
    AUTOMATIC AUDIO FORKING FOR ALL CALLS (AFTER ANSWER)
    - Runs first (00_ prefix)
    - Matches all calls (destination_number=^.*$)
    - Continues to other extensions (continue="true")
    - Audio fork starts AFTER call is answered (api_on_answer)
  -->
  <extension name="audio_fork_all_calls" continue="true">
    <condition field="destination_number" expression="^.*$">

      <!-- Log for debugging -->
      <action application="log" data="INFO [AUDIO_FORK] Setting up auto-fork for: ${caller_id_number} → ${destination_number}"/>

      <!-- Start audio forking AFTER call is answered (API command, use api_on_answer) -->
      <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}','timestamp':'${strftime(%Y-%m-%d %H:%M:%S)}'}"/>

      <!-- Auto-cleanup on hangup -->
      <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop {'end':'${strftime(%Y-%m-%d %H:%M:%S)}'}"/>

    </condition>
  </extension>
</include>
```

**Installation:**
```bash
# 1. Copy file
cp examples/freeswitch-config/dialplan/00_audio_fork_all_calls.xml \
   /usr/local/freeswitch/conf/dialplan/default/

# 2. Set permissions
chmod 644 /usr/local/freeswitch/conf/dialplan/default/00_audio_fork_all_calls.xml

# 3. Reload
fs_cli -x 'reloadxml'

# 4. Test
# Make any call and check logs
```

**Verification:**
```bash
# Make a call, then check:
fs_cli -x 'show channels'  # Get UUID
fs_cli -x 'uuid_buglist <uuid>'  # Should show: Function: audio_fork
```

## Support

For more information:
- [mod_audio_fork README](../../modules/mod_audio_fork/README.md)
- [mod_deepgram_transcribe README](../../modules/mod_deepgram_transcribe/README.md)
- [FreeSWITCH Dialplan Documentation](https://freeswitch.org/confluence/display/FREESWITCH/XML+Dialplan)

## License

See [LICENSE](../../LICENSE) in the repository root.
