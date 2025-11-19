# FreeSWITCH User Directory Examples

This directory contains example user directory files demonstrating different per-user service configurations.

## Files

### Single-Flag Examples (Simple)
- **`1000.xml`** - Audio fork enabled
- **`1001.xml`** - Audio fork disabled

### Multi-Flag Examples (Advanced)
- **`1000-audio-fork.xml`** - Audio Fork (WebSocket) only
- **`1001-deepgram.xml`** - Deepgram transcription only
- **`1003-azure.xml`** - Azure transcription only
- **`1004-all-services.xml`** - ALL services enabled

## Usage

### For Single Service (Audio Fork Only)

Copy the basic example files:

```bash
cp examples/freeswitch-config/directory/1000.xml /usr/local/freeswitch/conf/directory/default/
cp examples/freeswitch-config/directory/1001.xml /usr/local/freeswitch/conf/directory/default/
```

See: [Single-Flag Configuration](../AUDIO_FORK_PER_USER_FLAG.md)

### For Multiple Services (Audio Fork + Deepgram + Azure)

Copy the multi-flag example files:

```bash
cp examples/freeswitch-config/directory/1000-audio-fork.xml /usr/local/freeswitch/conf/directory/default/1000.xml
cp examples/freeswitch-config/directory/1001-deepgram.xml /usr/local/freeswitch/conf/directory/default/1001.xml
cp examples/freeswitch-config/directory/1003-azure.xml /usr/local/freeswitch/conf/directory/default/1003.xml
cp examples/freeswitch-config/directory/1004-all-services.xml /usr/local/freeswitch/conf/directory/default/1004.xml
```

See: [Per-User Multi-Service Configuration](../PER_USER_MULTI_SERVICE.md)

## Configuration Summary

### Single-Flag Approach

**Enable Audio Fork:**
```xml
<variables>
  <variable name="enable_audio_fork" value="true"/>
</variables>
```

**Disable Audio Fork:**
```xml
<variables>
  <variable name="enable_audio_fork" value="false"/>
  <!-- Or simply omit the variable -->
</variables>
```

### Multi-Flag Approach

**Audio Fork Only:**
```xml
<variables>
  <variable name="enable_audio_fork" value="true"/>
</variables>
```

**Deepgram Only:**
```xml
<variables>
  <!-- Flag only - all Deepgram settings in dialplan -->
  <variable name="enable_deepgram" value="true"/>
</variables>
```

**Azure Only:**
```xml
<variables>
  <!-- Flag only - all Azure settings in dialplan -->
  <variable name="enable_azure" value="true"/>
</variables>
```

**Multiple Services:**
```xml
<variables>
  <!-- Flags only - all service settings in dialplan -->
  <variable name="enable_audio_fork" value="true"/>
  <variable name="enable_deepgram" value="true"/>
</variables>
```

**Key Point:** With the multi-flag approach, user files contain **ONLY flags**. All service configuration (API keys, WebSocket URLs, etc.) is centralized in dialplan for easy management.

## Reload

After copying files:

```bash
chmod 644 /usr/local/freeswitch/conf/directory/default/*.xml
fs_cli -x 'reloadxml'
```

## Testing

Verify user settings:

```bash
# Check audio fork flag
fs_cli -x 'user_data 1000@default var enable_audio_fork'

# Check Deepgram flag
fs_cli -x 'user_data 1001@default var enable_deepgram'

# Check Azure flag
fs_cli -x 'user_data 1003@default var enable_azure'
```

## Related Documentation

- [Per-User Multi-Service Configuration](../PER_USER_MULTI_SERVICE.md)
- [Single-Flag Configuration Guide](../AUDIO_FORK_PER_USER_FLAG.md)
- [Main Dialplan Configuration](../FREESWITCH_DIALPLAN_AUDIO_FORK.md)
