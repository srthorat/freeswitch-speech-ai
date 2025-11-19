# FreeSWITCH User Directory Examples

This directory contains example user directory files demonstrating per-user audio fork configuration.

## Files

- **`1000.xml`** - Example user WITH audio fork enabled
- **`1001.xml`** - Example user WITHOUT audio fork enabled

## Usage

Copy these files to your FreeSWITCH user directory:

```bash
# Copy example files
cp examples/freeswitch-config/directory/1000.xml /usr/local/freeswitch/conf/directory/default/
cp examples/freeswitch-config/directory/1001.xml /usr/local/freeswitch/conf/directory/default/

# Set permissions
chmod 644 /usr/local/freeswitch/conf/directory/default/1000.xml
chmod 644 /usr/local/freeswitch/conf/directory/default/1001.xml

# Reload FreeSWITCH
fs_cli -x 'reloadxml'
```

## Configuration

### Enable Audio Fork for a User

Set these variables in the user's directory file:

```xml
<variables>
  <!-- Enable flag -->
  <variable name="enable_audio_fork" value="true"/>

  <!-- Audio fork settings -->
  <variable name="audio_fork_ws_url" value="ws://20.244.30.42:8077/stream"/>
  <variable name="audio_fork_mix_type" value="stereo"/>
  <variable name="audio_fork_sampling_rate" value="16k"/>
</variables>
```

### Disable Audio Fork for a User

Either set to false or don't include the variable:

```xml
<variables>
  <!-- Option 1: Explicitly disable -->
  <variable name="enable_audio_fork" value="false"/>

  <!-- Option 2: Simply don't set the variable -->
</variables>
```

## Related Documentation

- [Per-User Flag-Based Configuration Guide](../AUDIO_FORK_PER_USER_FLAG.md)
- [Main Dialplan Configuration Guide](../FREESWITCH_DIALPLAN_AUDIO_FORK.md)

## Testing

Verify user settings:

```bash
# Check if user 1000 has audio fork enabled
fs_cli -x 'user_data 1000@default var enable_audio_fork'
# Should return: true

# Check user 1001
fs_cli -x 'user_data 1001@default var enable_audio_fork'
# Should return: false
```
