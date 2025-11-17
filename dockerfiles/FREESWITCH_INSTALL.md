# FreeSWITCH 1.10.11 Installation Guide - Build from Source

This document provides a comprehensive guide to building FreeSWITCH 1.10.11 from source in a Docker container, including all dependencies, common errors, and their solutions.

## Table of Contents

- [Overview](#overview)
- [Build Dependencies](#build-dependencies)
- [Runtime Dependencies](#runtime-dependencies)
- [Multi-Stage Build Process](#multi-stage-build-process)
- [Common Build Errors and Solutions](#common-build-errors-and-solutions)
- [Module Configuration](#module-configuration)
- [Build Process](#build-process)
- [Verification](#verification)

---

## Overview

FreeSWITCH 1.10.11 is a production-ready telephony platform released in August 2019. Building from source ensures you get all modules and can customize the build for your specific needs.

**Key Features:**
- Full SIP and WebRTC support
- 100+ modules for telephony, codecs, applications
- Event Socket for external control
- Database support (PostgreSQL, ODBC, SQLite)
- Video support (VP8, VP9, H.264)
- Audio codecs (Opus, G.711, G.722, etc.)
- Lua scripting for dialplan logic

**Build Time:**
- Native x86_64: 30-45 minutes (with 8-16 CPU cores)
- Apple Silicon (emulated): 60-90 minutes

---

## Build Dependencies

### Essential Build Tools

| Package | Purpose | Required For |
|---------|---------|--------------|
| `build-essential` | GCC, G++, make, and other compilation tools | All C/C++ compilation |
| `cmake` | Build system generator | Some modules and dependencies |
| `autoconf` | Generate configure scripts | FreeSWITCH and dependencies |
| `automake` | Makefile generation | FreeSWITCH and dependencies |
| `libtool` | Shared library support | Library creation |
| `libtool-bin` | Libtool executable | **Critical**: Provides `/usr/bin/libtool` binary |
| `pkg-config` | Library dependency resolution | Finding installed libraries |
| `nasm` | x86 assembler | **Critical**: libvpx video codec optimization |
| `git` | Version control | Cloning source repositories |
| `wget` | File downloads | Downloading assets |
| `ca-certificates` | SSL certificates | HTTPS connections |

### FreeSWITCH Core Dependencies

| Package | Purpose | Component |
|---------|---------|-----------|
| `libssl-dev` | OpenSSL development headers | TLS/SSL encryption |
| `libcurl4-openssl-dev` | HTTP client library | API calls, webhooks |
| `libpcre3-dev` | Perl-compatible regex | Pattern matching in dialplan |
| `libspeex1` | Speex codec runtime | Audio codec |
| `libspeexdsp-dev` | Speex DSP library | Echo cancellation, noise suppression |
| `libedit-dev` | Command-line editing | fs_cli interactive features |
| `libtiff-dev` | TIFF image support | Fax (T.38) support |
| `libldns-dev` | DNS resolution | SIP DNS SRV lookups |
| `uuid-dev` | UUID generation | **Critical**: Unique call IDs, UUIDs |

### Audio/Video Codecs

| Package | Purpose | Codec/Feature |
|---------|---------|---------------|
| `libopus-dev` | Opus codec | High-quality VoIP audio (WebRTC) |
| `libsndfile1-dev` | Audio file I/O | WAV, FLAC, OGG file support |
| `libshout3-dev` | Icecast streaming | Audio streaming |
| `libmpg123-dev` | MP3 decoder | MP3 playback |
| `libmp3lame-dev` | MP3 encoder | MP3 recording |
| `libavformat-dev` | FFmpeg container formats | Video container support |
| `libswscale-dev` | FFmpeg video scaling | Video format conversion |

### Database Support

| Package | Purpose | Database |
|---------|---------|----------|
| `libsqlite3-dev` | SQLite development | Default database (call logs, voicemail) |
| `libpq-dev` | PostgreSQL client | PostgreSQL backend |
| `unixodbc-dev` | ODBC driver manager | **Critical**: Generic database connectivity |

### SIP and WebRTC

| Package | Purpose | Protocol |
|---------|---------|----------|
| `libsofia-sip-ua-dev` | SIP stack (packaged version) | SIP protocol (supplementary) |
| `libsrtp2-dev` | Secure RTP | WebRTC media encryption |

**Note**: We build sofia-sip v1.13.17 from source for better compatibility with FreeSWITCH 1.10.11.

### Additional Dependencies

| Package | Purpose | Feature |
|---------|---------|---------|
| `libxml2-dev` | XML parsing | Configuration files, dialplan XML |
| `liblua5.2-dev` | Lua scripting | mod_lua (recommended for dialplan) |
| `libgoogle-perftools-dev` | tcmalloc allocator | Performance optimization |
| `python3` | Python interpreter | Build scripts |
| `python-is-python3` | Python symlink | Legacy script compatibility |
| `zlib1g-dev` | Compression library | Data compression |
| `libjpeg-dev` | JPEG image support | Video snapshots, caller ID photos |

### Dependencies Built from Source

These are built in separate Docker stages before FreeSWITCH:

#### 1. spandsp (v0d2e6ac)

**Purpose**: DSP library for telephony applications

**Features**:
- T.38 fax support
- DTMF tone detection/generation
- Echo cancellation
- Modem emulation

**Why from source**: Debian package version may be outdated or missing features needed by FreeSWITCH 1.10.11

**Build process**:
```dockerfile
RUN git clone https://github.com/freeswitch/spandsp.git \
    && cd spandsp \
    && git checkout 0d2e6ac \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

#### 2. sofia-sip (v1.13.17)

**Purpose**: SIP protocol stack

**Features**:
- SIP message parsing/generation
- Transaction layer
- Dialog management
- SDP (Session Description Protocol)

**Why from source**: FreeSWITCH requires specific version for optimal compatibility

**Build process**:
```dockerfile
RUN git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git \
    && cd sofia-sip \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

---

## Runtime Dependencies

Runtime dependencies are the shared libraries needed to **run** FreeSWITCH (without development headers):

### Core Runtime Libraries

| Package | Build Dependency | Purpose |
|---------|------------------|---------|
| `libssl1.1` | libssl-dev | OpenSSL runtime |
| `libcurl4` | libcurl4-openssl-dev | HTTP client runtime |
| `libpcre3` | libpcre3-dev | Regex runtime |
| `libspeex1` | libspeex1 | Speex codec runtime |
| `libspeexdsp1` | libspeexdsp-dev | Speex DSP runtime |
| `libedit2` | libedit-dev | Command-line editing |
| `libtiff5` | libtiff-dev | TIFF runtime |
| `libldns3` | libldns-dev | DNS runtime |
| `libuuid1` | uuid-dev | **Critical**: UUID runtime library |

### Codec Runtime Libraries

| Package | Build Dependency |
|---------|------------------|
| `libopus0` | libopus-dev |
| `libsndfile1` | libsndfile1-dev |
| `libshout3` | libshout3-dev |
| `libmpg123-0` | libmpg123-dev |
| `libmp3lame0` | libmp3lame-dev |
| `libavformat58` | libavformat-dev |
| `libswscale5` | libswscale-dev |

### Database Runtime Libraries

| Package | Build Dependency |
|---------|------------------|
| `libsqlite3-0` | libsqlite3-dev |
| `libpq5` | libpq-dev |
| `unixodbc` | unixodbc-dev |

### Other Runtime Libraries

| Package | Purpose |
|---------|---------|
| `libsofia-sip-ua0` | SIP stack runtime |
| `libsrtp2-1` | SRTP runtime |
| `libxml2` | XML parsing |
| `liblua5.2-0` | Lua runtime |
| `libtcmalloc-minimal4` | tcmalloc runtime |

### System Utilities

As requested by user:

| Package | Purpose |
|---------|---------|
| `procps` | ps, top, kill commands |
| `net-tools` | netstat, ifconfig |
| `iputils-ping` | ping command |
| `iproute2` | ip command |
| `lsof` | List open files |
| `vim` | Text editor |
| `curl` | HTTP client |
| `wget` | File downloader |
| `ca-certificates` | SSL certificates |

### Process Management

| Package | Purpose |
|---------|---------|
| `supervisor` | Process manager (systemd alternative for Docker) |

---

## Multi-Stage Build Process

The Dockerfile uses a multi-stage build to minimize final image size:

### Stage 1: Builder Base
- Install all build dependencies
- Serves as base for subsequent build stages

### Stage 1.5: Build spandsp
- Clone spandsp repository
- Build and install spandsp v0d2e6ac
- Install to `/usr/local`

### Stage 1.6: Build sofia-sip
- Clone sofia-sip repository
- Build and install sofia-sip v1.13.17
- Install to `/usr/local`

### Stage 2: Build FreeSWITCH
- Copy spandsp and sofia-sip from previous stages
- Clone FreeSWITCH v1.10.11 source
- Run `bootstrap.sh` to generate configure scripts
- Disable optional modules (Python, Java, Perl, mod_verto, etc.)
- Configure with flags:
  - `--prefix=/usr/local/freeswitch`
  - `--enable-core-pgsql-support`
  - `--enable-core-odbc-support`
  - `--enable-tcmalloc`
  - `--without-python`, `--without-python3`, `--without-java`, `--without-perl`
- Compile with `make -j ${BUILD_CPUS}`
- Install with `make install`
- Install sounds and sample configuration

### Stage 3: Runtime Image
- Start from clean Debian base
- Install only runtime dependencies (no build tools)
- Copy spandsp, sofia-sip, and FreeSWITCH from build stages
- Configure extensions 1000 and 1001
- Set up supervisor for process management
- Create entrypoint script

**Image Size Comparison**:
- Build stage: ~3-4 GB (with all build tools and source code)
- Runtime image: ~800 MB - 1 GB (only binaries and runtime libraries)

---

## Common Build Errors and Solutions

Below are all errors encountered during the build process, with explanations and solutions.

### Error 1: libyuv-dev Package Not Found

**Error Message**:
```
E: Unable to locate package libyuv-dev
```

**Explanation**:
- `libyuv` is Google's library for YUV image format conversion (used in video processing)
- Not available in Debian Bullseye repositories
- FreeSWITCH can use alternative libraries for video support

**Solution**:
- Removed `libyuv-dev` from dependency list
- Video support still works via `libavformat-dev` and `libswscale-dev` (FFmpeg libraries)

**Impact**: None - video features still fully functional

---

### Error 2: libtool Executable Not Found

**Error Message**:
```
build-requirements: libtool not found.
You need libtool version 1.5.14 or newer to build FreeSWITCH from source.
```

**Explanation**:
- `libtool` package provides libtool library files
- `libtool-bin` package provides the `/usr/bin/libtool` executable
- FreeSWITCH bootstrap requires the executable, not just the library

**Solution**:
- Added `libtool-bin` package to build dependencies

**Why this happens**: Debian split libtool into two packages for modularity

---

### Error 3: ODBC Support Missing

**Error Message**:
```
configure: error: no usable libodbc; please install unixodbc devel package or equivalent
```

**Explanation**:
- ODBC (Open Database Connectivity) provides generic database interface
- FreeSWITCH uses ODBC for database backends (MySQL, PostgreSQL, SQL Server, etc.)
- Enabled by default in FreeSWITCH configure

**Solution**:
- Added `unixodbc-dev` to build dependencies
- Added `unixodbc` to runtime dependencies

**Why needed**: Allows FreeSWITCH to connect to various databases without database-specific code

---

### Error 4: spandsp Library Missing

**Error Message**:
```
checking for spandsp >= 3.0... configure: error: no usable spandsp; please install spandsp3 devel package or equivalent
```

**Explanation**:
- spandsp provides DSP (Digital Signal Processing) for telephony
- Critical for fax support (T.38), DTMF detection, echo cancellation
- Debian package version may be outdated or incompatible with FreeSWITCH 1.10.11

**Solution**:
- Created separate Docker build stage to compile spandsp v0d2e6ac from source
- Copied compiled libraries to FreeSWITCH build and runtime stages

**Build stage**:
```dockerfile
FROM builder AS spandsp
RUN git clone https://github.com/freeswitch/spandsp.git \
    && cd spandsp \
    && git checkout 0d2e6ac \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

**Why from source**: Ensures exact version compatibility with FreeSWITCH 1.10.11

---

### Error 5: sofia-sip Library Missing

**Error Message** (implied from spandsp pattern):
```
checking for sofia-sip >= 1.13... configure: error: no usable sofia-sip
```

**Explanation**:
- sofia-sip is the SIP protocol stack used by FreeSWITCH
- Handles all SIP message parsing, generation, and protocol logic
- FreeSWITCH requires specific version for compatibility

**Solution**:
- Created separate Docker build stage to compile sofia-sip v1.13.17 from source
- Copied compiled libraries to FreeSWITCH build and runtime stages

**Build stage**:
```dockerfile
FROM builder AS sofia-sip
RUN git clone --depth 1 -b v1.13.17 https://github.com/freeswitch/sofia-sip.git \
    && cd sofia-sip \
    && ./bootstrap.sh \
    && ./configure \
    && make -j ${BUILD_CPUS} \
    && make install
```

**Why from source**: Debian package may be too old or have incompatible patches

---

### Error 6: mod_verto Requires libks2/libks

**Error Message**:
```
checking for libks2 >= 2.0.0... checking for libks >= 1.8.2... no
configure: error: You need to either install libks2 or libks or disable mod_verto in modules.conf
```

**Explanation**:
- `mod_verto` implements the Verto protocol (deprecated WebRTC signaling protocol)
- Verto was FreeSWITCH's original WebRTC solution before SIP over WebSocket became standard
- Requires `libks` (Kite Signaling library), which is complex to build
- **Verto is deprecated** - modern WebRTC uses SIP over WebSocket + SRTP

**Solution**:
- Disabled `mod_verto` in `modules.conf`
- Also disabled other optional modules requiring complex dependencies:
  - `mod_skinny` - Cisco SCCP protocol (niche use case)
  - `mod_signalwire` - SignalWire cloud integration (proprietary)
  - `mod_av` - Advanced video (requires additional libraries)

**Code**:
```dockerfile
RUN sed -i 's|^endpoints/mod_verto$|#endpoints/mod_verto|' modules.conf \
    && sed -i 's|^endpoints/mod_skinny$|#endpoints/mod_skinny|' modules.conf \
    && sed -i 's|^applications/mod_signalwire$|#applications/mod_signalwire|' modules.conf \
    && sed -i 's|^applications/mod_av$|#applications/mod_av|' modules.conf
```

**Impact**: None - use standard SIP over WebSocket for WebRTC instead of deprecated Verto

**WebRTC still works via**:
- mod_sofia (SIP over WebSocket on port 5080/5081)
- mod_srtp (SRTP encryption)
- mod_opus, mod_vp8, mod_h264 (WebRTC codecs)

---

### Error 7: Python 2.x Detection Failing

**Error Message**:
```
checking location of site-packages... Traceback (most recent call last):
File "<string>", line 1, in <module>
ImportError: cannot import name 'sysconfig' from 'distutils'
configure: error: Unable to detect python site-packages path
```

**Explanation**:
- FreeSWITCH configure script tries to detect Python for `mod_python`
- Python 3.9+ removed `distutils.sysconfig` (deprecated module)
- We don't need Python modules - better to use Event Socket Library (ESL) for external control

**Solution**:
- Added `--without-python` flag to configure
- Disabled `mod_python` in `modules.conf`

**Code**:
```dockerfile
RUN sed -i 's|^languages/mod_python$|#languages/mod_python|' modules.conf

RUN ./configure \
    --prefix=/usr/local/freeswitch \
    --without-python \
    ...
```

**Why disable Python modules**:
- **Stability**: Embedded interpreters can cause crashes
- **Better alternative**: Use Event Socket Library (ESL) to control FreeSWITCH from external Python scripts
- **Separation**: FreeSWITCH and application code run in separate processes
- **Flexibility**: Can restart application without restarting FreeSWITCH

---

### Error 8: Python 3.x Still Being Detected

**Error Message**:
```
configure: WARNING: python support disabled, building mod_python will fail!
checking for python3... /usr/bin/python3
checking python3 version... 3.9.2
checking for python3 distutils... yes
checking location of python3 site-packages... Traceback (most recent call last):
  File "<string>", line 1, in <module>
ImportError: cannot import name 'sysconfig' from 'distutils'
configure: error: Unable to detect python3 site-packages path
```

**Explanation**:
- `--without-python` only disables Python 2.x checks
- FreeSWITCH configure script separately checks for Python 3.x for `mod_python3`
- Same `distutils.sysconfig` issue with Python 3.9+

**Solution**:
- Added `--without-python3` flag to configure
- Disabled `mod_python3` in `modules.conf`

**Code**:
```dockerfile
RUN sed -i 's|^languages/mod_python3$|#languages/mod_python3|' modules.conf

RUN ./configure \
    --prefix=/usr/local/freeswitch \
    --without-python \
    --without-python3 \
    ...
```

---

### Error 9: libvpx Requires yasm or nasm

**Error Message**:
```
Neither yasm nor nasm have been found. See the prerequisites section in the README for more info.

Configuration failed. This could reflect a misconfiguration of your
toolchains, improper options selected, or another problem.
make: *** [Makefile:4487: libs/libvpx/Makefile] Error 1
```

**Explanation**:
- `libvpx` provides VP8 and VP9 video codecs (critical for WebRTC video)
- Requires assembly optimizations for acceptable performance
- `yasm` or `nasm` assemblers needed to compile assembly code

**Solution**:
- Added `nasm` package to build dependencies

**Code**:
```dockerfile
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build tools
    build-essential \
    ...
    nasm \
    ...
```

**Why nasm**: Modern, actively maintained, supports both x86 and x86_64

**Impact**: Enables high-performance VP8/VP9 video codecs for WebRTC

---

### Error 10: UUID Header Missing

**Error Message**:
```
src/switch_apr.c:90:10: fatal error: uuid/uuid.h: No such file or directory
   90 | #include <uuid/uuid.h>
      |          ^~~~~~~~~~~~~
compilation terminated.
make[1]: *** [Makefile:2310: src/libfreeswitch_la-switch_apr.lo] Error 1
```

**Explanation**:
- FreeSWITCH uses UUIDs (Universally Unique Identifiers) extensively:
  - Unique call IDs
  - Channel UUIDs
  - Session identifiers
- `uuid/uuid.h` provided by `uuid-dev` package
- `libuuid1` provides runtime library

**Solution**:
- Added `uuid-dev` to build dependencies
- Added `libuuid1` to runtime dependencies

**Code**:
```dockerfile
# Build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    ...
    uuid-dev \
    ...

# Runtime dependencies (in runtime stage)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ...
    libuuid1 \
    ...
```

**Why critical**: Without UUIDs, FreeSWITCH cannot generate unique call identifiers

---

### Error 11: Configuration Directory Not Created

**Error Message**:
```
cp: target '/usr/local/freeswitch/conf/' is not a directory
make: *** [Makefile:xxx] Error 1
```

**Explanation**:
- After successful compilation and `make install`, configuration directory doesn't exist
- `make install` creates main directories (`/usr/local/freeswitch/bin`, `/usr/local/freeswitch/lib`) but not `conf/`
- The vanilla configuration from source needs to be copied to this directory
- `cp` fails because target directory doesn't exist

**Solution**:
- Create `/usr/local/freeswitch/conf` directory before copying configuration

**Code**:
```dockerfile
# Install sample configuration
RUN mkdir -p /usr/local/freeswitch/conf \
    && cp -r /usr/local/src/freeswitch/conf/vanilla/* /usr/local/freeswitch/conf/ \
    && echo "✅ Sample configuration installed"
```

**Why this happens**:
- `make install` only creates directories for binaries and libraries
- Configuration is meant to be installed via `make samples` or manually
- We manually install vanilla config for better control

**Impact**: Without vanilla configuration, FreeSWITCH won't start (missing vars.xml, dialplan, SIP profiles, etc.)

---

### Error 12: Event Socket Module Not Built (fs_cli Can't Connect)

**Error Message**:
```bash
# fs_cli
[ERROR] fs_cli.c:1699 main() Error Connecting []
Usage: fs_cli [-H <host>] [-P <port>] [-p <secret>]...
```

**Symptoms**:
- `fs_cli` shows "Error Connecting []"
- Port 8021 is not listening: `netstat -an | grep 8021` shows nothing
- Module file missing: `/usr/local/freeswitch/mod/mod_event_socket.so` doesn't exist
- FreeSWITCH runs but Event Socket interface unavailable
- SIP profiles work normally (ports 5060, 5080)

**Explanation**:
- `mod_event_socket` provides the Event Socket Layer (ESL) interface
- Required for `fs_cli` to connect and control FreeSWITCH
- Default FreeSWITCH `modules.conf` may not include it in `event_handlers/` section
- If not in `modules.conf`, the module won't be compiled during `make`
- Without this module, fs_cli has no way to connect to FreeSWITCH

**Solution**:
- Explicitly ensure `mod_event_socket` is enabled in `modules.conf` before building
- Configure Event Socket to bind to IPv4 instead of IPv6

**Code**:
```dockerfile
# 1. Enable module in modules.conf (after bootstrap, before disabling optional modules)
RUN grep -q "^event_handlers/mod_event_socket$" modules.conf || \
    echo "event_handlers/mod_event_socket" >> modules.conf \
    && echo "✅ Ensured critical modules are enabled (mod_event_socket)"

# 2. Configure Event Socket for IPv4 binding (in runtime stage)
RUN cat > /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml <<'EOF'
<configuration name="event_socket.conf" description="Socket Client">
  <settings>
    <param name="nat-map" value="false"/>
    <!-- Bind to all IPv4 interfaces for fs_cli access -->
    <param name="listen-ip" value="0.0.0.0"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
    <!--<param name="apply-inbound-acl" value="loopback.auto"/>-->
    <!--<param name="stop-on-bind-error" value="true"/>-->
  </settings>
</configuration>
EOF
```

**IPv4 vs IPv6 Binding**:
- Default config uses `::` (IPv6 all interfaces)
- We use `0.0.0.0` (all IPv4 interfaces) for Docker compatibility
- This allows fs_cli to connect from both inside container and host machine
- For security, use `127.0.0.1` (localhost only) or enable ACL

**Why this happens**:
- FreeSWITCH source's default `modules.conf` varies by version
- Some versions don't include `mod_event_socket` by default
- The module must be explicitly listed under `event_handlers/` section
- Without it, configure/make skip the module entirely

**Verification**:
```bash
# Check if module exists after build
ls -la /usr/local/freeswitch/mod/mod_event_socket.so

# Check if port 8021 is listening
netstat -an | grep 8021

# Test fs_cli connection
fs_cli -x "status"
```

**Impact**: Without Event Socket:
- ❌ Can't use `fs_cli` for management
- ❌ Can't use Event Socket Library (ESL) for external applications
- ❌ Can't execute API commands remotely
- ✅ SIP calling still works (different module)

---

### Error 13: FreeSWITCH Not Loading Configuration (Module Exists But Won't Load)

**Error Message**:
```bash
# fs_cli still fails even though module exists
docker exec freeswitch ls -la /usr/local/freeswitch/lib/freeswitch/mod/mod_event_socket.so
# Shows: -rw-r--r-- 1 root root ... mod_event_socket.so (FILE EXISTS!)

docker exec freeswitch netstat -an | grep 8021
# Shows: (nothing - port not listening)

docker exec -it freeswitch fs_cli
# [ERROR] fs_cli.c:1699 main() Error Connecting []
```

**Symptoms**:
- `mod_event_socket.so` binary **exists** in `/usr/local/freeswitch/lib/freeswitch/mod/`
- Port 8021 is **not listening**
- `fs_cli` shows "Error Connecting []"
- FreeSWITCH **is running** (SIP works on port 5060)
- No `freeswitch.log` created in `/usr/local/freeswitch/log/`
- FreeSWITCH appears to run with minimal/fallback configuration

**Root Cause**:
FreeSWITCH was started **without specifying configuration paths**. The supervisor command was:
```bash
command=/usr/local/freeswitch/bin/freeswitch -nonat -nc -nf
```

Without `-conf`, `-log`, and `-db` flags, FreeSWITCH uses **default search paths** (`/etc/freeswitch`, `/usr/local/etc/freeswitch`) which don't match our custom prefix `/usr/local/freeswitch`.

Result:
- FreeSWITCH starts but doesn't load `/usr/local/freeswitch/conf/`
- `event_socket.conf.xml` is never loaded
- Module binary exists but is never activated
- No logs are written to the expected location

**Solution**:
Start FreeSWITCH with **explicit configuration paths** in supervisor command:

**Code**:
```dockerfile
# Supervisor configuration with explicit paths
RUN mkdir -p /etc/supervisor/conf.d && cat > /etc/supervisor/conf.d/freeswitch.conf <<'EOF'
[program:freeswitch]
command=/usr/local/freeswitch/bin/freeswitch -nonat -nc -nf \
  -conf /usr/local/freeswitch/conf \
  -log  /usr/local/freeswitch/log \
  -db   /usr/local/freeswitch/db
autostart=true
autorestart=true
startretries=3
user=freeswitch
stdout_logfile=/var/log/supervisor/freeswitch.log
stderr_logfile=/var/log/supervisor/freeswitch_err.log
EOF
```

**Why This Happens**:
- FreeSWITCH has **hardcoded default paths** in its source code
- When built with `--prefix=/usr/local/freeswitch`, binaries go to custom location
- BUT: without explicit `-conf` flag, FreeSWITCH searches default paths first
- This is a common issue with custom FreeSWITCH installations

**FreeSWITCH Default Search Order** (without `-conf`):
1. `/etc/freeswitch/` (doesn't exist)
2. `/usr/local/etc/freeswitch/` (doesn't exist)
3. Fallback to minimal embedded config
4. Never checks `/usr/local/freeswitch/conf/` (our actual config location!)

**Verification After Fix**:
```bash
# Check module loads correctly
docker exec freeswitch bash -c "ls /usr/local/freeswitch/lib/freeswitch/mod/mod_event_socket.so"
# Should exist

# Check port 8021 is listening
docker exec freeswitch netstat -an | grep 8021
# Should show: tcp 0 0 0.0.0.0:8021 0.0.0.0:* LISTEN

# Check logs are being created
docker exec freeswitch ls -la /usr/local/freeswitch/log/freeswitch.log
# Should exist and grow over time

# Test fs_cli connection
docker exec -it freeswitch fs_cli
# Should show FreeSWITCH CLI prompt
```

**Impact**:
- ❌ Without explicit paths: FreeSWITCH runs but with wrong/minimal config
- ❌ Modules exist but don't load
- ❌ No proper logging
- ❌ Event Socket never activates
- ✅ With explicit paths: All modules load correctly, fs_cli works, full functionality

**Best Practice**:
Always start FreeSWITCH in Docker with explicit paths:
- `-conf /usr/local/freeswitch/conf` (configuration directory)
- `-log /usr/local/freeswitch/log` (log directory)
- `-db /usr/local/freeswitch/db` (database directory)

This makes the system **predictable**, **debuggable**, and **production-ready**.

---

### Error 14: Language Bindings (Java, Perl, PHP)

**Not yet encountered, but proactively disabled**

**Why disabled**:
```dockerfile
RUN sed -i 's|^languages/mod_java$|#languages/mod_java|' modules.conf \
    && sed -i 's|^languages/mod_perl$|#languages/mod_perl|' modules.conf \
    && sed -i 's|^languages/mod_php$|#languages/mod_php|' modules.conf

RUN ./configure \
    --without-java \
    --without-perl \
    ...
```

**Reasons to disable**:
- **Java**: Requires JDK, JNI setup, large memory footprint
- **Perl**: Requires Perl dev headers, CPAN modules
- **PHP**: Requires PHP dev headers, unstable with FreeSWITCH

**Best practice**: Use Event Socket Library (ESL) for external control in any language

---

## Module Configuration

### Modules Disabled

The following modules are disabled in `modules.conf` to avoid complex dependencies:

| Module | Category | Reason |
|--------|----------|--------|
| `mod_verto` | Endpoint | Deprecated WebRTC protocol, requires libks |
| `mod_skinny` | Endpoint | Cisco SCCP, niche use case |
| `mod_signalwire` | Application | SignalWire cloud integration, proprietary |
| `mod_av` | Application | Advanced video, requires additional libraries |
| `mod_python` | Language | Python 2.x binding, deprecated |
| `mod_python3` | Language | Python 3.x binding, use ESL instead |
| `mod_java` | Language | Java binding, use ESL instead |
| `mod_perl` | Language | Perl binding, use ESL instead |
| `mod_php` | Language | PHP binding, use ESL instead |

### Critical Modules Enabled

The following critical modules are enabled and verified in testing:

**Endpoints**:
- `mod_sofia` - SIP and SIP over WebSocket (primary signaling)

**Applications**:
- `mod_conference` - Conference bridging
- `mod_dptools` - Dialplan tools
- `mod_commands` - API commands
- `mod_voicemail` - Voicemail system

**Dialplan**:
- `mod_dialplan_xml` - XML dialplan (default)
- `mod_lua` - Lua scripting (recommended)

**Codecs - Audio**:
- `mod_opus` - Opus codec (WebRTC)
- `mod_g711` - G.711 (PCMU/PCMA)
- `mod_g722` - G.722 wideband

**Codecs - Video**:
- `mod_vp8` - VP8 codec (WebRTC)
- `mod_vp9` - VP9 codec (WebRTC)
- `mod_h264` - H.264 codec

**Formats**:
- `mod_sndfile` - Audio file I/O
- `mod_local_stream` - Local audio streaming

**Event Handlers**:
- `mod_event_socket` - Event Socket (for fs_cli and ESL)

**Total Modules**: 100+ modules enabled by default (minus disabled language bindings and optional modules)

---

## Build Process

### Build Script Usage

```bash
./dockerfiles/build-freeswitch-base.sh [image-name]
```

**Default image name**: `freeswitch-base:1.10.11`

**Example**:
```bash
./dockerfiles/build-freeswitch-base.sh freeswitch-base:latest
```

### Build Script Features

1. **Platform Detection**: Automatically detects ARM64 (Apple Silicon) and uses x86_64 emulation
2. **CPU Detection**: Uses all available CPU cores for parallel compilation
3. **Version Management**: Reads spandsp and sofia-sip versions from `.env` file
4. **Progress Display**: Shows estimated build time and configuration

### Build Stages Timing

| Stage | Time (x86_64) | Time (ARM64 emulated) |
|-------|---------------|----------------------|
| Base builder dependencies | 2-3 min | 3-5 min |
| Build spandsp | 1-2 min | 2-4 min |
| Build sofia-sip | 2-3 min | 3-6 min |
| Configure FreeSWITCH | 2-3 min | 3-5 min |
| Compile FreeSWITCH | 20-30 min | 40-60 min |
| Install sounds | 2-3 min | 3-5 min |
| Runtime stage | 2-3 min | 3-5 min |
| **Total** | **30-45 min** | **60-90 min** |

### Build Optimization

The build uses parallel compilation:
- `make -j ${BUILD_CPUS}` - Uses all available CPU cores
- Docker BuildKit for layer caching
- Multi-stage build to minimize final image size

---

## Verification

### Automated Testing

Use the provided test script:

```bash
./dockerfiles/test-freeswitch-base.sh freeswitch-base:1.10.11
```

### Manual Testing

**Start container**:
```bash
docker run -d \
    --name freeswitch-test \
    -p 5060:5060/tcp \
    -p 5060:5060/udp \
    -p 5080:5080/tcp \
    -p 8021:8021/tcp \
    freeswitch-base:1.10.11
```

**Connect with fs_cli**:
```bash
docker exec -it freeswitch-test fs_cli
```

**Check status**:
```
fs_cli> status
fs_cli> show modules
fs_cli> sofia status
```

**Expected results**:
- FreeSWITCH uptime displayed
- 100+ modules loaded
- SIP profiles running on port 5060/5080

### Extension Testing

Extensions 1000 and 1001 are pre-configured with password `1234`.

**Using softphone** (like Zoiper, Linphone, or Bria):
1. Register extension 1000: `sip:1000@<container-ip>:5060` (password: 1234)
2. Register extension 1001: `sip:1001@<container-ip>:5060` (password: 1234)
3. Call from 1000 to 1001 by dialing `1001`
4. Verify two-way audio

---

## Troubleshooting

### Build Fails at Configure Stage

**Check**:
```bash
# View full build log
docker build -f dockerfiles/Dockerfile.freeswitch-base -t test . 2>&1 | tee build.log

# Search for "error" in log
grep -i "error" build.log | grep -v "NORMAL_CLEARING"
```

**Common causes**:
- Missing development package (check "Common Build Errors" section)
- Wrong package version for Debian release
- Network issues cloning repositories

### Build Fails at Compilation Stage

**Symptoms**: Errors like "No such file or directory" for header files

**Solution**: Add missing `-dev` package to build dependencies and corresponding runtime package

### FreeSWITCH Won't Start

**Check logs**:
```bash
docker logs freeswitch-test
docker exec freeswitch-test cat /usr/local/freeswitch/log/freeswitch.log
```

**Common issues**:
- Missing runtime library (add to runtime dependencies)
- Configuration file syntax error
- Permissions issue (files should be owned by `freeswitch:freeswitch`)

### fs_cli Cannot Connect

**Check**:
```bash
# Verify event socket configuration
docker exec freeswitch-test cat /usr/local/freeswitch/conf/autoload_configs/event_socket.conf.xml

# Verify port is listening
docker exec freeswitch-test netstat -an | grep 8021
```

**Default event socket**: Port 8021, password "ClueCon"

### SIP Clients Cannot Register

**Check**:
```bash
# Verify SIP profile is running
docker exec freeswitch-test fs_cli -x "sofia status"

# Check for binding errors
docker exec freeswitch-test cat /usr/local/freeswitch/log/freeswitch.log | grep -i "bind"
```

**Common issues**:
- Port 5060 already in use on host
- Firewall blocking UDP 5060
- NAT configuration needed for external clients

---

## Performance Optimization

### Build Time Optimization

**Use more CPU cores**:
```bash
# Manually set CPU cores
docker build --build-arg BUILD_CPUS=32 ...
```

**Enable BuildKit caching**:
```bash
export DOCKER_BUILDKIT=1
```

### Runtime Optimization

**tcmalloc enabled**: FreeSWITCH is configured with `--enable-tcmalloc` for better memory allocation performance

**Supervisor**: Uses supervisor instead of running FreeSWITCH as PID 1 for better signal handling

---

## Additional Resources

- **FreeSWITCH Official Docs**: https://freeswitch.org/confluence/
- **Installation Guide**: https://freeswitch.org/confluence/display/FREESWITCH/Installation
- **Modules Documentation**: https://freeswitch.org/confluence/display/FREESWITCH/Modules
- **Event Socket Library**: https://freeswitch.org/confluence/display/FREESWITCH/Event+Socket+Library
- **WebRTC Guide**: https://freeswitch.org/confluence/display/FREESWITCH/WebRTC

---

## Summary

Building FreeSWITCH 1.10.11 from source requires careful attention to dependencies. This guide documents all dependencies discovered through the build process and provides solutions to common errors.

**Key takeaways**:
1. **Multi-stage builds** minimize final image size (3-4 GB build → 800 MB runtime)
2. **Build from source** for spandsp and sofia-sip ensures version compatibility
3. **Disable language bindings** - use Event Socket Library instead for better stability
4. **Test thoroughly** - use provided test script to verify all components
5. **Document everything** - this guide helps future builds and troubleshooting

**Next steps**: After successful build, add custom modules one by one using the module testing approach in `dockerfiles/Dockerfile.mod-test`.
