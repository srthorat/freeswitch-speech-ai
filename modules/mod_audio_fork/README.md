# mod_audio_fork

A Freeswitch module that attaches a bug to a media server endpoint and streams L16 audio via websockets to a remote server.  This module also supports receiving media from the server to play back to the caller, enabling the creation of full-fledged IVR or dialog-type applications.

## Dependencies

- **libwebsockets** - Required for WebSocket connectivity
- FreeSWITCH 1.8 or later

## Building

See the main [repository README](../../README.md) for complete build instructions. This module requires FreeSWITCH to be built with libwebsockets support.

The [ansible-role-fsmrf](https://github.com/drachtio/ansible-role-fsmrf) provides automated builds with all dependencies, or you can install libwebsockets manually:

```bash
apt-get install -y libwebsockets-dev
```

#### Environment variables
- MOD_AUDIO_FORK_SUBPROTOCOL_NAME - optional, name of the [websocket sub-protocol](https://tools.ietf.org/html/rfc6455#section-1.9) to advertise; defaults to "audio.drachtio.org"
- MOD_AUDIO_FORK_SERVICE_THREADS - optional, number of libwebsocket service threads to create; these threads handling sending all messages for all sessions.  Defaults to 1, but can be set to as many as 5.

## API

### Commands
The freeswitch module exposes the following API commands:

```
uuid_audio_fork <uuid> start <wss-url> <mix-type> <sampling-rate> <metadata>
```
Attaches media bug and starts streaming audio stream to the back-end server.  Audio is streamed in linear 16 format (16-bit PCM encoding) with either one or two channels depending on the mix-type requested.
- `uuid` - unique identifier of Freeswitch channel
- `wss-url` - websocket url to connect and stream audio to
- `mix-type` - choice of 
  - "mono" - single channel containing caller's audio
  - "mixed" - single channel containing both caller and callee audio
  - "stereo" - two channels with caller audio in one and callee audio in the other.
- `sampling-rate` - choice of
  - "8k" = 8000 Hz sample rate will be generated
  - "16k" = 16000 Hz sample rate will be generated
- `metadata` - a text frame of arbitrary data to send to the back-end server immediately upon connecting.  Once this text frame has been sent, the incoming audio will be sent in binary frames to the server.

```
uuid_audio_fork <uuid> send_text <metadata>
```
Send a text frame of arbitrary data to the remote server (e.g. this can be used to notify of DTMF events).

```
uuid_audio_fork <uuid> stop <metadata>
```
Closes websocket connection and detaches media bug, optionally sending a final text frame over the websocket connection before closing.

### Events
An optional feature of this module is that it can receive JSON text frames from the server and generate associated events to an application.  The format of the JSON text frames and the associated events are described below.

#### audio
##### server JSON message
The server can provide audio content to be played back to the caller by sending a JSON text frame like this:
```json
{
	"type": "playAudio",
	"data": {
		"audioContentType": "raw",
		"sampleRate": 8000,
		"audioContent": "base64 encoded raw audio..",
		"textContent": "Hi there!  How can we help?"
	}
}
```
The `audioContentType` value can be either `wave` or `raw`.  If the latter, then `sampleRate` must be specified.  The audio content itself is supplied as a base64 encoded string.  The `textContent` attribute can optionally contain the text of the prompt.  This allows an application to choose whether to play the raw audio or to use its own text-to-speech to play the text prompt.

Note that the module does _not_ directly play out the raw audio.  Instead, it writes it to a temporary file and provides the path to the file in the event generated.  It is left to the application to play out this file if it wishes to do so.
##### Freeswitch event generated
**Name**: mod_audio_fork::play_audio
**Body**: JSON string
```
{
  "audioContentType": "raw",
  "sampleRate": 8000,
  "textContent": "Hi there!  How can we help?",
  "file": "/tmp/7dd5e34e-5db4-4edb-a166-757e5d29b941_2.tmp.r8"
}
```
Note the audioContent attribute has been replaced with the path to the file containing the audio.  This temporary file will be removed when the Freeswitch session ends.
#### killAudio
##### server JSON message
The server can provide a request to kill the current audio playback:
```json
{
	"type": "killAudio",
}
```
Any current audio being played to the caller will be immediately stopped.  The event sent to the application is for information purposes only.

##### Freeswitch event generated
**Name**: mod_audio_fork::kill_audio
**Body**: JSON string - the data attribute from the server message


#### transcription
##### server JSON message
The server can optionally provide transcriptions to the application in real-time:
```json
{
	"type": "transcription",
	"data": {
    
	}
}
```
The transcription data can be any JSON object; for instance, a server may choose to return a transcript and an associated confidence level.  Whatever is provided as the `data` attribute will be attached to the generated event.

##### Freeswitch event generated
**Name**: mod_audio_fork::transcription
**Body**: JSON string - the data attribute from the server message

#### transfer
##### server JSON message
The server can optionally provide a request to transfer the call:
```json
{
	"type": "transfer",
	"data": {
    
	}
}
```
The transfer data can be any JSON object and is left for the application to determine how to handle it and accomplish the call transfer.  Whatever is provided as the `data` attribute will be attached to the generated event.

##### Freeswitch event generated
**Name**: mod_audio_fork::transfer
**Body**: JSON string - the data attribute from the server message

#### disconnect
##### server JSON message
The server can optionally request to disconnect the caller:
```json
{
	"type": "disconnect"
}
```
Note that the module _does not_ close the Freeswitch channel when a disconnect request is received.  It is left for the application to determine whether to tear down the call.

##### Freeswitch event generated
**Name**: mod_audio_fork::disconnect
**Body**: none

#### error
##### server JSON message
The server can optionally report an error of some kind.  
```json
{
	"type": "error",
	"data": {
    
	}
}
```
The error data can be any JSON object and is left for the application to the application to determine what, if any, action should be taken in response to an error..  Whatever is provided as the `data` attribute will be attached to the generated event.

##### Freeswitch event generated
**Name**: mod_audio_fork::error
**Body**: JSON string - the data attribute from the server message

## FreeSWITCH Dialplan Configuration

This section shows how to automatically enable mod_audio_fork for specific extensions or call scenarios using FreeSWITCH dialplan XML.

### Use Case 1: Automatic Audio Forking for Specific Extension

**Scenario**: When extension 1000 calls ANY other extension (1002, 1003, etc.), automatically start audio forking.

#### Method 1: Using Dialplan with Caller ID Match

Create `/usr/local/freeswitch/conf/dialplan/default/audio_fork_ext_1000.xml`:

```xml
<include>
  <!-- Automatically fork audio when extension 1000 calls any other extension -->
  <extension name="audio_fork_for_1000" continue="true">
    <condition field="caller_id_number" expression="^1000$">
      <condition field="destination_number" expression="^(10\d{2})$">
        <!-- Set WebSocket server URL -->
        <action application="set" data="AUDIO_FORK_WS_URL=wss://your-backend-server.com/audio"/>

        <!-- Set mix type: mono, mixed, or stereo -->
        <action application="set" data="AUDIO_FORK_MIX_TYPE=stereo"/>

        <!-- Set sampling rate: 8k or 16k -->
        <action application="set" data="AUDIO_FORK_SAMPLING_RATE=16k"/>

        <!-- Set metadata (JSON format) -->
        <action application="set" data="AUDIO_FORK_METADATA={'caller':'1000','callee':'${destination_number}','timestamp':'${strftime(%Y-%m-%d %H:%M:%S)}'}"/>

        <!-- Start audio forking -->
        <action application="uuid_audio_fork" data="${uuid} start ${AUDIO_FORK_WS_URL} ${AUDIO_FORK_MIX_TYPE} ${AUDIO_FORK_SAMPLING_RATE} ${AUDIO_FORK_METADATA}"/>
      </condition>
    </condition>
  </extension>
</include>
```

**Key Points:**
- `continue="true"` - Allows the call to proceed to other dialplan extensions
- `caller_id_number` matches extension 1000
- `destination_number` matches 4-digit extensions starting with 10 (1000-1099)
- Variables are set first, then used in the command
- **Audio forking starts during routing phase** (before call is answered/bridged)

#### Method 2: Start Audio Fork After Call is Answered

To start audio forking **only after the call is answered** (not during routing), use `api_on_answer`:

```xml
<include>
  <!-- Start audio fork AFTER call is answered -->
  <extension name="audio_fork_ext_1000_on_answer" continue="true">
    <condition field="caller_id_number" expression="^1000$">
      <condition field="destination_number" expression="^(10\d{2})$">
        <!-- Log for debugging -->
        <action application="log" data="INFO [AUDIO_FORK] Setting up for ext 1000 → ${destination_number}"/>

        <!-- Start audio forking AFTER call is answered -->
        <!-- Note: uuid_audio_fork is an API command, use api_on_answer (NOT execute_on_answer) -->
        <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start wss://your-server.com/audio stereo 16k {'from':'1000','to':'${destination_number}'}"/>

        <!-- Stop audio forking on hangup -->
        <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
      </condition>
    </condition>
  </extension>
</include>
```

#### Method 3: Per-User Flag-Based Configuration (Recommended for Production)

Configure audio forking at the user level with flags in user files and settings in dialplan.

📖 **See:** [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

**User file** (flag only) `/usr/local/freeswitch/conf/directory/default/1000.xml`:

```xml
<include>
  <user id="1000">
    <params>
      <param name="password" value="1234"/>
    </params>
    <variables>
      <variable name="user_context" value="default"/>
      <variable name="effective_caller_id_name" value="Extension 1000"/>

      <!-- Audio Fork Flag (settings are in dialplan) -->
      <variable name="enable_audio_fork" value="true"/>
    </variables>
  </user>
</include>
```

**Dialplan** (centralized settings) `/usr/local/freeswitch/conf/dialplan/default.xml`:

```xml
<include>
  <!-- Check if audio fork is enabled for this user -->
  <extension name="audio_fork_conditional" continue="true">
    <condition field="${user_data(${caller_id_number}@${domain_name} var enable_audio_fork)}" expression="^true$">
      <condition field="destination_number" expression="^(.+)$">
        <action application="log" data="INFO [AUDIO_FORK] Authorized User ${caller_id_number} calling ${destination_number} -> Starting Stream"/>

        <!-- Start audio forking AFTER call is answered -->
        <!-- All settings centralized here (WebSocket URL, mix type, sampling rate) -->
        <action application="set" data="api_on_answer=uuid_audio_fork ${uuid} start ws://20.244.30.42:8077/stream stereo 16k {'caller':'${caller_id_number}','callee':'${destination_number}'}"/>

        <!-- Stop on hangup -->
        <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop"/>
      </condition>
    </condition>
  </extension>
</include>
```

**Benefits:**
- Uses `user_data()` function for reliable flag checking (works even with auth issues)
- User files contain only flags (clean and simple)
- All settings (WebSocket URL, mix type, sampling rate) centralized in dialplan
- Starts AFTER call is answered (not during routing)
- Easy to manage API keys and settings in one place

**Reload configuration:**
```bash
fs_cli -x 'reloadxml'
```

### Use Case 2: Audio Forking with Different Mix Types

#### Mono Mode (Caller Audio Only)

```xml
<extension name="audio_fork_mono" continue="true">
  <condition field="caller_id_number" expression="^1000$">
    <condition field="destination_number" expression="^(10\d{2})$">
      <!-- Mono: Only caller's audio (extension 1000) -->
      <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio mono 8k {'mode':'mono','caller':'1000'}"/>
    </condition>
  </condition>
</extension>
```

**Use Cases for Mono:**
- Caller-side analytics (speech quality, sentiment)
- Caller voice authentication
- Outbound call monitoring

#### Mixed Mode (Both Parties, Single Channel)

```xml
<extension name="audio_fork_mixed" continue="true">
  <condition field="caller_id_number" expression="^1000$">
    <condition field="destination_number" expression="^(10\d{2})$">
      <!-- Mixed: Both parties in one channel -->
      <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio mixed 8k {'mode':'mixed','caller':'1000','callee':'${destination_number}'}"/>
    </condition>
  </condition>
</extension>
```

**Use Cases for Mixed:**
- Basic call recording
- Simple transcription (when you don't need to separate speakers)
- Lower bandwidth requirements

#### Stereo Mode (Both Parties, Separate Channels)

```xml
<extension name="audio_fork_stereo" continue="true">
  <condition field="caller_id_number" expression="^1000$">
    <condition field="destination_number" expression="^(10\d{2})$">
      <!-- Stereo: Caller (channel 0) and Callee (channel 1) separated -->
      <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k {'mode':'stereo','caller':'1000','callee':'${destination_number}'}"/>
    </condition>
  </condition>
</extension>
```

**Use Cases for Stereo:**
- Accurate speaker separation
- Call center quality monitoring
- Advanced transcription with speaker attribution
- Compliance recording

### Use Case 3: Sampling Rate Selection

#### 8kHz (Telephony Quality)

```xml
<!-- 8kHz = Lower bandwidth, telephony quality (sufficient for most voice) -->
<action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 8k {'rate':'8000'}"/>
```

**Benefits:**
- Lower bandwidth usage (~128 kbps for stereo)
- Sufficient for speech recognition
- Standard telephony quality

#### 16kHz (High Quality)

```xml
<!-- 16kHz = Higher bandwidth, better quality for transcription -->
<action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k {'rate':'16000'}"/>
```

**Benefits:**
- Better audio quality for transcription
- More accurate speech recognition
- Recommended for production use

### Use Case 4: Rich Metadata Examples

#### Basic Metadata

```xml
<action application="set" data="metadata={'caller':'${caller_id_number}','callee':'${destination_number}'}"/>
<action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k ${metadata}"/>
```

#### Extended Metadata with Timestamps and Call Details

```xml
<action application="set" data="metadata={'caller':'${caller_id_number}','caller_name':'${caller_id_name}','callee':'${destination_number}','call_uuid':'${uuid}','timestamp':'${strftime(%Y-%m-%d %H:%M:%S)}','call_direction':'internal','account':'sales-dept'}"/>
<action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k ${metadata}"/>
```

#### Metadata for External Calls

```xml
<extension name="audio_fork_external">
  <condition field="destination_number" expression="^(\d{10})$">
    <action application="set" data="metadata={'caller':'${caller_id_number}','external_number':'${destination_number}','call_type':'outbound','customer_id':'${customer_id}','campaign':'summer-2025'}"/>
    <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k ${metadata}"/>
    <action application="bridge" data="sofia/external/${destination_number}@gateway"/>
  </condition>
</extension>
```

### Use Case 5: Multiple Extensions with Audio Forking

**Enable audio forking for multiple extensions (1000, 1001, 1002):**

```xml
<include>
  <!-- Audio forking for extensions 1000-1002 -->
  <extension name="audio_fork_multiple_exts" continue="true">
    <condition field="caller_id_number" expression="^(1000|1001|1002)$">
      <condition field="destination_number" expression="^(10\d{2})$">
        <action application="set" data="metadata={'caller':'${caller_id_number}','callee':'${destination_number}','dept':'sales'}"/>
        <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/sales-audio stereo 16k ${metadata}"/>
      </condition>
    </condition>
  </extension>
</include>
```

### Use Case 6: Conditional Audio Forking

#### Fork Audio Only for External Calls

```xml
<extension name="audio_fork_external_only" continue="true">
  <condition field="caller_id_number" expression="^1000$">
    <!-- Match external numbers (10 digits) -->
    <condition field="destination_number" expression="^(\d{10})$">
      <action application="set" data="metadata={'caller':'1000','external':'${destination_number}','type':'outbound'}"/>
      <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/external-audio stereo 16k ${metadata}"/>
    </condition>
  </condition>
</extension>
```

#### Fork Audio Only During Business Hours

```xml
<extension name="audio_fork_business_hours" continue="true">
  <condition field="caller_id_number" expression="^1000$">
    <!-- Check if current time is between 9 AM and 5 PM -->
    <condition hour="9-17" wday="2-6">
      <condition field="destination_number" expression="^(10\d{2})$">
        <action application="set" data="metadata={'caller':'1000','callee':'${destination_number}','business_hours':'true'}"/>
        <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k ${metadata}"/>
      </condition>
    </condition>
  </condition>
</extension>
```

### Use Case 7: Audio Forking with Event Handling

When you want to stop audio forking at call end:

```xml
<extension name="audio_fork_with_cleanup">
  <condition field="caller_id_number" expression="^1000$">
    <condition field="destination_number" expression="^(10\d{2})$">
      <!-- Start audio forking -->
      <action application="set" data="metadata={'caller':'1000','callee':'${destination_number}','start_time':'${strftime(%Y-%m-%d %H:%M:%S)}'}"/>
      <action application="uuid_audio_fork" data="${uuid} start wss://your-server.com/audio stereo 16k ${metadata}"/>

      <!-- Set hangup hook to stop audio forking -->
      <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop {'end_time':'${strftime(%Y-%m-%d %H:%M:%S)}','reason':'call_ended'}"/>

      <!-- Bridge the call -->
      <action application="bridge" data="user/${destination_number}"/>
    </condition>
  </condition>
</extension>
```

### Complete Working Example

**Recommended Approach:** Use the per-user multi-service configuration for production deployments.

See: [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)

**Quick Start:**
- [Complete dialplan example](../../examples/freeswitch-config/dialplan/default.xml.complete-example)
- [User directory examples](../../examples/freeswitch-config/directory/)

---

**Legacy Example:** Single extension with inline audio fork

**/usr/local/freeswitch/conf/dialplan/default/01_audio_fork_ext_1000.xml:**

```xml
<include>
  <!--
    Automatic audio forking for extension 1000
    When 1000 calls any extension 1000-1099, audio is streamed to backend server
  -->
  <extension name="audio_fork_extension_1000" continue="true">
    <!-- Match caller: extension 1000 -->
    <condition field="caller_id_number" expression="^1000$">
      <!-- Match destination: any 4-digit extension 1000-1099 -->
      <condition field="destination_number" expression="^(10\d{2})$">

        <!-- Configuration Variables -->
        <action application="set" data="audio_fork_enabled=true"/>
        <action application="set" data="ws_url=wss://audio.yourcompany.com/stream"/>
        <action application="set" data="mix_type=stereo"/>
        <action application="set" data="sample_rate=16k"/>

        <!-- Build metadata JSON -->
        <action application="set" data="call_metadata={'caller_ext':'${caller_id_number}','caller_name':'${caller_id_name}','callee_ext':'${destination_number}','call_uuid':'${uuid}','timestamp':'${strftime(%Y-%m-%d %H:%M:%S)}','department':'sales','session_id':'${uuid}'}"/>

        <!-- Start audio forking -->
        <action application="log" data="INFO Starting audio fork for call from ${caller_id_number} to ${destination_number}"/>
        <action application="uuid_audio_fork" data="${uuid} start ${ws_url} ${mix_type} ${sample_rate} ${call_metadata}"/>

        <!-- Set cleanup hook to stop audio fork on hangup -->
        <action application="set" data="api_hangup_hook=uuid_audio_fork ${uuid} stop {'session_end':'${strftime(%Y-%m-%d %H:%M:%S)}'}"/>

      </condition>
    </condition>
  </extension>
</include>
```

**Apply configuration:**
```bash
fs_cli -x 'reloadxml'
```

**Test the configuration:**
1. Extension 1000 calls 1002
2. Audio forking starts automatically
3. Both caller and callee audio streams to wss://audio.yourcompany.com/stream
4. When call ends, audio forking stops automatically

### Troubleshooting

#### Common Issue: Dialplan Not Executing (No Log Messages)

**Symptom:** Dialplan file exists and is loaded, but actions never execute (no log messages appear).

**Root Cause:** Empty `<condition>` tag without `field` attribute.

**WRONG ❌:**
```xml
<extension name="audio_fork_all_calls" continue="true">
  <condition>  <!-- Missing field attribute! -->
    <action application="log" data="INFO Starting audio fork"/>
  </condition>
</extension>
```

**CORRECT ✅:**
```xml
<extension name="audio_fork_all_calls" continue="true">
  <condition field="destination_number" expression="^.*$">  <!-- Has field and expression -->
    <action application="log" data="INFO Starting audio fork"/>
  </condition>
</extension>
```

In FreeSWITCH, a `<condition>` tag **must** have a `field` attribute to match against. To match all calls, use:
- `field="destination_number"` with `expression="^.*$"` (matches any destination)

**For detailed configuration examples**, see:
- [Per-User Multi-Service Configuration Guide](../../examples/freeswitch-config/PER_USER_MULTI_SERVICE.md)
- [Complete working dialplan example](../../examples/freeswitch-config/dialplan/default.xml.complete-example)

#### Check if audio forking is active:

```bash
fs_cli -x 'uuid_buglist <call-uuid>'
```

#### View audio fork status:

```bash
fs_cli -x 'uuid_dump <call-uuid>' | grep -i audio
```

#### Manual control (for testing):

```bash
# Start manually
fs_cli -x "uuid_audio_fork <uuid> start wss://your-server.com/audio stereo 16k {'test':'manual'}"

# Send text frame
fs_cli -x "uuid_audio_fork <uuid> send_text {'event':'dtmf','digit':'5'}"

# Stop manually
fs_cli -x "uuid_audio_fork <uuid> stop {'reason':'manual_stop'}"
```

---

## Usage
```js
const url = 'https://70f21a76.ngrok.io';
const callerData = {to: '6173333456', from: '2061236666', callid: req.get('Call-Id')};
ep.api('uuid_audio_fork', `${ep.uuid} start ${url} mono 8k ${JSON.stringify(callerData)}`);
```
or, from version 1.4.1 on, by using the Endpoint convenience methods:
```js
await ep.forkAudioStart({
  wsUrl,
  mixType: 'stereo',
  sampling: '16k',
  metadata
});
..
ep.forkAudioSendText(moremetadata);
..
ep.forkAudioStop(evenmoremetadata);
```
Each of the methods above returns a promise that resolves when the api command has been executed, or throws an error.
## Examples
[audio_fork.js](../../examples/audio_fork.js) provides an example of an application that connects an incoming call to Freeswitch and then forks the audio to a remote websocket server.

To run this app, you can run [the simple websocket server provided](../../examples/ws_server.js) in a separate terminal.  It will listen on port 3001 and will simply write the incoming raw audio to `/tmp/audio.raw` in linear16 format with no header or file container.

So in the first terminal window run:
```
node ws_server.js
```
And in the second window run:
```
node audio_fork.js http://localhost:3001
```
The app uses text-to-speech to play prompts, so you will need mod_google_tts loaded as well, and configured to use your GCS cloud credentials to access Google Cloud Text-to-Speech.  (If you don't want to run mod_google_tts you can of course simply modify the application remove the prompt, just be aware that you will hear silence when you connect, and should simply begin speaking after the call connects).


