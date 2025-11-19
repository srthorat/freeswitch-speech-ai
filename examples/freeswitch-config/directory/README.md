# FreeSWITCH User Directory Examples

This directory contains example user directory files demonstrating per-user service configurations.

## Files

### Complete Working Examples (Ready to Deploy)
- **`1000.xml.complete`** - Audio Fork (WebSocket) only
- **`1001.xml.complete`** - Deepgram transcription only
- **`1003.xml.complete`** - Azure transcription only

### Additional Examples
- **`1000-audio-fork.xml`** - Audio Fork (WebSocket) only
- **`1001-deepgram.xml`** - Deepgram transcription only
- **`1003-azure.xml`** - Azure transcription only
- **`1004-all-services.xml`** - ALL services enabled (demo/testing)

## Quick Installation

Use the complete working examples:

```bash
# Copy complete examples (ready to use)
cp examples/freeswitch-config/directory/1000.xml.complete /usr/local/freeswitch/conf/directory/default/1000.xml
cp examples/freeswitch-config/directory/1001.xml.complete /usr/local/freeswitch/conf/directory/default/1001.xml
cp examples/freeswitch-config/directory/1003.xml.complete /usr/local/freeswitch/conf/directory/default/1003.xml

# Reload configuration
fs_cli -x 'reloadxml'
```

See: [Per-User Multi-Service Configuration Guide](../PER_USER_MULTI_SERVICE.md)

## Configuration Examples

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

- [Per-User Multi-Service Configuration Guide](../PER_USER_MULTI_SERVICE.md) - Complete guide with dialplan setup
- [Complete Dialplan Example](../dialplan/default.xml.complete-example) - Ready-to-use dialplan configuration
