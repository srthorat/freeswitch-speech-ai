# Per-User Flag-Based Audio Fork Configuration

This guide shows how to enable audio fork on a **per-user basis** using a flag in user directory files.

## Overview

**Goal:** Only enable audio fork for specific users who have the `enable_audio_fork` flag set in their user directory file.

**Example:**
- Extension 1000 has `enable_audio_fork=true` → Audio fork ENABLED
- Extension 1001 has `enable_audio_fork=false` (or not set) → Audio fork DISABLED

## Configuration Steps

### Step 1: Configure User Directory Files

#### For User with Audio Fork ENABLED (1000.xml)

**File:** `/usr/local/freeswitch/conf/directory/default/1000.xml`

```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
      <param name="vm-password" value="1000"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="accountcode" value="1000"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1000"/>
      <variable name="effective_caller_id_number" value="1000"/>

      <!-- AUDIO FORK ENABLED -->
      <variable name="enable_audio_fork" value="true"/>

      <!-- Audio Fork Settings -->
      <variable name="audio_fork_ws_url" value="ws://20.244.30.42:8077/stream"/>
      <variable name="audio_fork_mix_type" value="stereo"/>
      <variable name="audio_fork_sampling_rate" value="16k"/>
    </variables>
  </user>
</include>
```

#### For User with Audio Fork DISABLED (1001.xml)

**File:** `/usr/local/freeswitch/conf/directory/default/1001.xml`

```xml
<include>
  <user id="1001">
    <params>
      <param name="password" value="1234"/>
      <param name="vm-password" value="1001"/>
    </params>
    <variables>
      <variable name="toll_allow" value="domestic,international,local"/>
      <variable name="accountcode" value="1001"/>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1001"/>
      <variable name="effective_caller_id_number" value="1001"/>

      <!-- AUDIO FORK DISABLED -->
      <!-- Option 1: Explicitly set to false -->
      <variable name="enable_audio_fork" value="false"/>

      <!-- Option 2: Simply don't set the variable at all -->
      <!-- If the variable is not set, audio fork will not start -->
    </variables>
  </user>
</include>
```

### Step 2: Create Conditional Dialplan

**File:** `/usr/local/freeswitch/conf/dialplan/default/00_audio_fork_conditional.xml`

```xml
<include>
  <!-- CONDITIONAL AUDIO FORKING BASED ON USER FLAG -->
  <extension name="audio_fork_conditional" continue="true">
    <!-- Check if enable_audio_fork is set to "true" -->
    <condition field="${enable_audio_fork}" expression="^true$">
      <!-- Match all destination numbers -->
      <condition field="destination_number" expression="^(.+)$">

        <!-- Log for debugging -->
        <action application="log" data="INFO [AUDIO_FORK] User ${caller_id_number} has audio fork enabled"/>

        <!-- Build metadata -->
        <action application="set" data="audio_fork_metadata={'caller':'${caller_id_number}','callee':'${destination_number}','timestamp':'${strftime(%Y-%m-%d %H:%M:%S)}'}"/>

        <!-- Start audio forking AFTER call is answered -->
        <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ${audio_fork_ws_url} ${audio_fork_mix_type} ${audio_fork_sampling_rate} ${audio_fork_metadata}"/>

        <!-- Stop audio fork on hangup -->
        <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>

      </condition>
    </condition>
  </extension>
</include>
```

**Key Points:**
- Uses `${enable_audio_fork}` to check the flag from user directory
- Only executes if flag is set to `"true"`
- Uses user's configured settings: `${audio_fork_ws_url}`, `${audio_fork_mix_type}`, `${audio_fork_sampling_rate}`
- Starts audio fork AFTER call is answered using `api_on_answer`
- `continue="true"` allows call to proceed normally

### Step 3: Reload Configuration

```bash
fs_cli -x 'reloadxml'
```

## Testing

### Test 1: User with Audio Fork Enabled (1000)

1. Extension 1000 calls 1001
2. You should see in logs:
   ```
   [INFO] [AUDIO_FORK] User 1000 has audio fork enabled
   ```
3. After call is answered:
   ```
   mod_audio_fork: streaming 16000 sampling to 20.244.30.42:8077/stream
   ```

### Test 2: User with Audio Fork Disabled (1001)

1. Extension 1001 calls 1000
2. You should NOT see any `[AUDIO_FORK]` log messages
3. Audio fork does NOT start
4. Call proceeds normally without audio streaming

## Verification Commands

### Check if user has flag enabled

```bash
# For user 1000
fs_cli -x 'user_data 1000@default var enable_audio_fork'
# Should return: true

# For user 1001
fs_cli -x 'user_data 1001@default var enable_audio_fork'
# Should return: false (or empty if not set)
```

### Check active calls with audio fork

```bash
# Get active calls
fs_cli -x 'show channels'

# Check if audio fork is attached to a call
fs_cli -x 'uuid_buglist <uuid>'
# Should show: Function: audio_fork (only for enabled users)
```

### Check user variables during a call

```bash
# During an active call from extension 1000
fs_cli -x 'uuid_getvar <uuid> enable_audio_fork'
# Should return: true

fs_cli -x 'uuid_getvar <uuid> audio_fork_ws_url'
# Should return: ws://20.244.30.42:8077/stream
```

## Configuration Options

### Per-User WebSocket URLs

You can use different WebSocket servers for different users:

**User 1000 (Sales department):**
```xml
<variable name="enable_audio_fork" value="true"/>
<variable name="audio_fork_ws_url" value="ws://sales-analytics.company.com/stream"/>
```

**User 1002 (Support department):**
```xml
<variable name="enable_audio_fork" value="true"/>
<variable name="audio_fork_ws_url" value="ws://support-qa.company.com/stream"/>
```

### Different Mix Types Per User

**User for call center (stereo - separate channels):**
```xml
<variable name="audio_fork_mix_type" value="stereo"/>
```

**User for voice assistant (mono - caller only):**
```xml
<variable name="audio_fork_mix_type" value="mono"/>
```

### Different Sampling Rates

**High quality for compliance recording:**
```xml
<variable name="audio_fork_sampling_rate" value="16k"/>
```

**Lower bandwidth for basic monitoring:**
```xml
<variable name="audio_fork_sampling_rate" value="8k"/>
```

## Advanced: Conditional by Department

You can also enable audio fork for all users in a specific department:

**User directory with department variable:**
```xml
<variable name="department" value="sales"/>
<variable name="enable_audio_fork" value="true"/>
<variable name="audio_fork_ws_url" value="ws://sales.company.com/stream"/>
```

**Dialplan to check department:**
```xml
<extension name="audio_fork_sales_dept" continue="true">
  <!-- Check if user is in sales department AND has audio fork enabled -->
  <condition field="${department}" expression="^sales$">
    <condition field="${enable_audio_fork}" expression="^true$">
      <condition field="destination_number" expression="^(.+)$">
        <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ${audio_fork_ws_url} stereo 16k {'dept':'sales','user':'${caller_id_number}'}"/>
        <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
      </condition>
    </condition>
  </condition>
</extension>
```

## Troubleshooting

### Issue 1: Audio Fork Not Starting for Enabled User

**Check:**
1. Verify flag is set correctly:
   ```bash
   fs_cli -x 'user_data 1000@default var enable_audio_fork'
   ```

2. Check dialplan is loaded:
   ```bash
   fs_cli -x 'xml_locate dialplan' | grep -A 10 "audio_fork_conditional"
   ```

3. Enable debug logging:
   ```bash
   fs_cli -x 'console loglevel 7'
   ```
   Make a call and watch for `[AUDIO_FORK]` messages

### Issue 2: Variable Not Found

If you see errors like "variable not found":
1. Check user directory file syntax (valid XML)
2. Reload: `fs_cli -x 'reloadxml'`
3. Verify user exists: `fs_cli -x 'user_exists 1000@default'`

### Issue 3: Audio Fork Starts for All Users

If audio fork starts even for disabled users:
1. Check if you have another dialplan extension without flag check
2. Look for duplicate extensions:
   ```bash
   fs_cli -x 'xml_locate dialplan' | grep -i "audio_fork"
   ```
3. Make sure old configuration files are removed

## File Locations

| File Type | Location |
|-----------|----------|
| User directory files | `/usr/local/freeswitch/conf/directory/default/*.xml` |
| Dialplan files | `/usr/local/freeswitch/conf/dialplan/default/*.xml` |
| Example configs | `examples/freeswitch-config/directory/` |
| Example dialplan | `examples/freeswitch-config/dialplan/` |

## Benefits of Flag-Based Approach

✅ **Granular Control** - Enable/disable per user
✅ **Flexible Configuration** - Different settings per user
✅ **Easy Management** - Just edit user file and reload
✅ **Department Support** - Group users by department
✅ **No Code Changes** - Pure XML configuration
✅ **Scalable** - Works with 1 user or 10,000 users

## Related Documentation

- [mod_audio_fork README](../../modules/mod_audio_fork/README.md)
- [FreeSWITCH Dialplan Guide](FREESWITCH_DIALPLAN_AUDIO_FORK.md)
- [User Directory Documentation](https://freeswitch.org/confluence/display/FREESWITCH/XML+User+Directory)
