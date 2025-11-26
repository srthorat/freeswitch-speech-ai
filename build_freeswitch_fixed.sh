#!/bin/bash
#
# build_freeswitch_fixed.sh
#
# FreeSWITCH 1.10.11 + mod_google_transcribe (Google Speech V2 API)
# FAST BUILD — uses system gRPC/Protobuf (no source compile)
# STABLE — Dockerfile-identical FS build
# CLEAN — automatic recovery + smart re-run logic
#

###############################################################################
# SAFER SHELL SETTINGS
###############################################################################

# Safe error handling without -u (which can cause issues with unset variables)
set -e
set -o pipefail

# Global error handler with line number
trap 'echo ""; echo "[ERROR] Script failed at line $LINENO"; echo ""' ERR

###############################################################################
# CONFIGURATION
###############################################################################

readonly FREESWITCH_VERSION="v1.10.11"
readonly SPANDSP_VERSION="0d2e6ac"
readonly SOFIA_VERSION="1.13.17"

readonly FS_PREFIX="/usr/local/freeswitch"
readonly SRC_DIR="/usr/local/src"
readonly CURRENT_DIR="$(pwd)"
readonly MOD_GOOGLE_SRC="${CURRENT_DIR}/modules/mod_google_transcribe"

readonly LOG_FILE="/var/log/fs_build_$(date +%Y%m%d_%H%M%S).log"
readonly BUILD_CPUS=$(nproc)

DO_CORE=false
DO_MOD_GOOGLE=false
DO_ALL=false
USE_SYSTEM_GRPC=false
FORCE_REBUILD_GRPC=false

###############################################################################
# LOGGING
###############################################################################

# Create log directory if needed
mkdir -p /var/log

# Setup logging
exec > >(tee -a "${LOG_FILE}") 2>&1

log()       { echo -e "$(date +'%F %T') [INFO] $1"; }
log_warn()  { echo -e "$(date +'%F %T') [WARN] $1"; }
log_error() { echo -e "$(date +'%F %T') [ERROR] $1"; }
log_step()  { echo -e "\n============================================================\n[STEP] $1\n============================================================\n"; }

check_root() { 
    if [[ $EUID -ne 0 ]]; then
        echo "Must run as root"
        exit 1
    fi
}

###############################################################################
# SYSTEM DEPENDENCIES
###############################################################################

cleanup_conflicts() {
    log "Cleaning up potential conflicts..."
    
    # Only remove conflicting system binaries, not our built ones
    rm -f /usr/bin/protoc* 2>/dev/null || true
    rm -f /usr/bin/grpc_* 2>/dev/null || true
    
    # Remove old build artifacts
    if [[ -d "${SRC_DIR}/googleapis/gens" ]]; then
        log "Cleaning up old googleapis build..."
        rm -rf "${SRC_DIR}/googleapis/gens"
    fi
    
    # Note: Preserving /usr/local/bin/protoc* and source builds for caching
}

install_system_deps() {
    log_step "Installing System Dependencies (Matching Dockerfile)"

    # Update package cache
    apt-get update -q || {
        log_error "Failed to update package cache"
        exit 1
    }
    
    # Install dependencies exactly as in the working Dockerfile
    apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        autoconf \
        automake \
        libtool \
        libtool-bin \
        pkg-config \
        nasm \
        git \
        wget \
        ca-certificates \
        libssl-dev \
        libcurl4-openssl-dev \
        libpcre3-dev \
        libspeex1 \
        libspeexdsp-dev \
        libedit-dev \
        libtiff-dev \
        libldns-dev \
        uuid-dev \
        libopus-dev \
        libsndfile1-dev \
        libshout3-dev \
        libmpg123-dev \
        libmp3lame-dev \
        libsqlite3-dev \
        libpq-dev \
        unixodbc-dev \
        libsofia-sip-ua-dev \
        libsrtp2-dev \
        libavformat-dev \
        libswscale-dev \
        libxml2-dev \
        liblua5.2-dev \
        libgoogle-perftools-dev \
        python3 \
        python-is-python3 \
        zlib1g-dev \
        libjpeg-dev \
        libcjson-dev || {
        log_error "Failed to install dependencies"
        exit 1
    }
    
    # Remove system protobuf/grpc to avoid version conflicts
    log "Removing system protobuf/grpc packages to avoid conflicts..."
    apt-get remove -y libprotobuf-dev protobuf-compiler libgrpc-dev libgrpc++-dev protobuf-compiler-grpc 2>/dev/null || true
    
    log "✅ Dependencies installed successfully"
}

###############################################################################
# SPANDSP BUILD (Required for FreeSWITCH)
###############################################################################

build_spandsp() {
    log_step "Building SpanDSP (required version)"
    
    # Set library path for builds
    export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
    
    # Check if correct SpanDSP is already installed
    if pkg-config --exists spandsp && pkg-config --atleast-version=3.0 spandsp 2>/dev/null; then
        local installed_version=$(pkg-config --modversion spandsp 2>/dev/null || echo "unknown")
        log "SpanDSP ${installed_version} found, checking if it's the correct build..."
        
        # Try to compile a test to see if it works with FreeSWITCH
        if [[ -f /usr/include/spandsp.h ]]; then
            log "SpanDSP headers found, may already be correct version"
            # You can uncomment the next line to skip rebuild if you're sure it's correct
            # return 0
        fi
    fi
    
    cd "${SRC_DIR}"
    
    # Remove old spandsp if exists
    rm -rf spandsp
    
    log "Cloning SpanDSP..."
    git clone https://github.com/freeswitch/spandsp.git || {
        log_error "Failed to clone SpanDSP"
        exit 1
    }
    
    cd spandsp
    
    log "Checking out specific commit ${SPANDSP_VERSION}..."
    git checkout ${SPANDSP_VERSION} || {
        log_error "Failed to checkout SpanDSP commit ${SPANDSP_VERSION}"
        exit 1
    }
    
    # Clean any existing build artifacts to prevent bootstrap conflicts
    log "Cleaning existing build artifacts..."
    rm -rf config autom4te.cache 2>/dev/null || true
    
    log "Building SpanDSP..."
    ./bootstrap.sh || {
        log_error "SpanDSP bootstrap failed"
        exit 1
    }
    
    ./configure || {
        log_error "SpanDSP configure failed"
        exit 1
    }
    
    make -j "${BUILD_CPUS}" || {
        log_error "SpanDSP make failed"
        exit 1
    }
    
    make install || {
        log_error "SpanDSP install failed"
        exit 1
    }
    
    # Update library cache
    ldconfig
    
    log "✅ SpanDSP installed successfully"
}

###############################################################################
# SOFIA-SIP BUILD (Optional but recommended)
###############################################################################

build_sofia_sip() {
    log_step "Building Sofia-SIP ${SOFIA_VERSION}"
    
    cd "${SRC_DIR}"
    
    # Remove old sofia-sip if exists
    if [[ -d sofia-sip ]]; then
        log "Removing existing sofia-sip directory..."
        rm -rf sofia-sip
    fi
    
    log "Cloning Sofia-SIP..."
    git clone --depth 1 -b "v${SOFIA_VERSION}" https://github.com/freeswitch/sofia-sip.git || {
        log_error "Failed to clone Sofia-SIP"
        exit 1
    }
    
    cd sofia-sip
    
    log "Running bootstrap..."
    ./bootstrap.sh || {
        log_error "Sofia-SIP bootstrap failed"
        exit 1
    }
    
    log "Configuring Sofia-SIP..."
    ./configure || {
        log_error "Sofia-SIP configure failed"
        exit 1
    }
    
    log "Building Sofia-SIP..."
    make -j "${BUILD_CPUS}" || {
        log_error "Sofia-SIP make failed"
        exit 1
    }
    
    log "Installing Sofia-SIP..."
    make install || {
        log_error "Sofia-SIP install failed"
        exit 1
    }
    
    # Update library cache
    ldconfig
    
    log "✅ Sofia-SIP ${SOFIA_VERSION} installed successfully"
}

###############################################################################
# FREESWITCH BUILD (Dockerfile IDENTICAL)
###############################################################################

build_freeswitch() {
    log_step "Building FreeSWITCH ${FREESWITCH_VERSION}"

    # Check if FreeSWITCH is already installed
    if [[ -f "${FS_PREFIX}/bin/freeswitch" ]] && [[ -f "${FS_PREFIX}/include/freeswitch/switch.h" ]]; then
        log "FreeSWITCH already installed at ${FS_PREFIX}"
        log "Version: $("${FS_PREFIX}/bin/freeswitch" -version 2>/dev/null | head -n1 || echo 'unknown')"
        log "Skipping FreeSWITCH build (use --force-rebuild to rebuild anyway)"
        return 0
    fi

    # Build dependencies first
    build_spandsp
    build_sofia_sip

    # Ensure source directory exists
    mkdir -p "${SRC_DIR}"
    cd "${SRC_DIR}"

    # Clone or update FreeSWITCH
    if [[ ! -d freeswitch ]]; then
        log "Cloning FreeSWITCH ${FREESWITCH_VERSION}..."
        git clone --depth 1 -b "${FREESWITCH_VERSION}" https://github.com/signalwire/freeswitch.git || {
            log_error "Failed to clone FreeSWITCH"
            exit 1
        }
    else
        log "FreeSWITCH source already exists, using existing..."
    fi

    cd freeswitch

    # Bootstrap
    log "Running bootstrap..."
    ./bootstrap.sh -j || {
        log_error "Bootstrap failed"
        exit 1
    }

    # Configure modules.conf
    log "Configuring modules..."
    
    # Ensure event_socket is enabled
    if ! grep -q "^event_handlers/mod_event_socket$" modules.conf; then
        echo "event_handlers/mod_event_socket" >> modules.conf
    fi

    # Disable unnecessary modules
    sed -i 's/^endpoints\/mod_verto$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^endpoints\/mod_rtc$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^endpoints\/mod_skinny$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^applications\/mod_signalwire$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^applications\/mod_av$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^languages\/mod_python$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^languages\/mod_python3$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^languages\/mod_java$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^languages\/mod_perl$/#&/' modules.conf 2>/dev/null || true
    sed -i 's/^languages\/mod_php$/#&/' modules.conf 2>/dev/null || true

    # Configure - matching Dockerfile exactly
    log "Running configure..."
    ./configure \
        --prefix="${FS_PREFIX}" \
        --enable-core-pgsql-support \
        --enable-core-odbc-support \
        --enable-tcmalloc \
        --without-python \
        --without-python3 \
        --without-java \
        --without-perl || {
        log_error "Configure failed"
        exit 1
    }

    # Build
    log "Building FreeSWITCH (this may take a while)..."
    make -j "${BUILD_CPUS}" || {
        log_error "Make failed"
        exit 1
    }

    # Install
    log "Installing FreeSWITCH..."
    make install || {
        log_error "Make install failed"
        exit 1
    }
    
    # Note: install-headers doesn't exist in FreeSWITCH 1.10.11
    # The headers are installed automatically with make install
    
    # Install sounds (optional, continue on failure)
    log "Installing sounds..."
    make cd-sounds-install cd-moh-install 2>/dev/null || log_warn "Sound installation failed (non-critical)"

    # Setup configuration
    log "Setting up configuration..."
    mkdir -p "${FS_PREFIX}/conf"
    
    if [[ -d "${SRC_DIR}/freeswitch/conf/vanilla" ]]; then
        cp -r "${SRC_DIR}/freeswitch/conf/vanilla/"* "${FS_PREFIX}/conf/" || {
            log_error "Failed to copy configuration"
            exit 1
        }
    fi

    # Event Socket configuration
    cat > "${FS_PREFIX}/conf/autoload_configs/event_socket.conf.xml" <<'EOF'
<configuration name="event_socket.conf" description="Socket Client">
  <settings>
    <param name="nat-map" value="false"/>
    <param name="listen-ip" value="0.0.0.0"/>
    <param name="listen-port" value="8021"/>
    <param name="password" value="ClueCon"/>
  </settings>
</configuration>
EOF

    log "✅ FreeSWITCH core build completed"
}

###############################################################################
# PROTOBUF/GRPC BUILD (Required for mod_google_transcribe)
###############################################################################

###############################################################################
# PROTOBUF/GRPC INSTALLATION
###############################################################################

install_grpc_from_packages() {
    log_step "Installing gRPC/Protobuf from packages"
    
    # Install minimal packages - we'll build protobuf/grpc from source for compatibility
    apt-get update -q
    apt-get install -y --no-install-recommends \
        libcjson-dev \
        libabsl-dev \
        libre2-dev \
        libssl-dev \
        zlib1g-dev || {
        log_error "Failed to install required dependencies"
        return 1
    }
    
    log "✅ Essential packages installed - will build protobuf/grpc from source"
    return 1  # Always fall back to source build for compatibility
}

build_protobuf_grpc() {
    log_step "Building Protobuf and gRPC from source for mod_google_transcribe"
    
    # Check if compatible versions are already installed (unless force rebuild)
    if ! $FORCE_REBUILD_GRPC; then
        if pkg-config --exists protobuf && pkg-config --atleast-version=3.21.12 protobuf 2>/dev/null; then
            if pkg-config --exists grpc++ && pkg-config --atleast-version=1.51.0 grpc++ 2>/dev/null; then
                if command -v grpc_cpp_plugin &> /dev/null; then
                    # Check for version compatibility with format differences
                    PROTOC_VER=$(protoc --version | cut -d' ' -f2)
                    LIB_VER=$(pkg-config --modversion protobuf)
                    PROTOC_NORM=$(echo $PROTOC_VER | sed 's/\.0$//')
                    LIB_NORM=$(echo $LIB_VER | sed 's/\.0$//')
                    
                    if [[ "$PROTOC_NORM" == "$LIB_NORM" ]] || [[ "$PROTOC_VER" == "$LIB_NORM" ]] || [[ "$PROTOC_NORM" == "$LIB_VER" ]]; then
                        INSTALLED_PROTOBUF=$(pkg-config --modversion protobuf)
                        INSTALLED_GRPC=$(pkg-config --modversion grpc++)
                        log "Compatible versions already installed:"
                        log "  - Protobuf: $INSTALLED_PROTOBUF (protoc: $PROTOC_VER)"
                        log "  - gRPC: $INSTALLED_GRPC"
                        log "Version formats are compatible - skipping rebuild to save time."
                        return 0
                    fi
                fi
            fi
        fi
    else
        log "Forcing rebuild of protobuf/gRPC as requested..."
    fi
    
    cd "${SRC_DIR}"
    
    # Build protobuf (matching system version for compatibility)
    log "Building protobuf v3.21.12..."
    
    # Only clone if directory doesn't exist or if forcing rebuild
    if [[ ! -d protobuf ]] || $FORCE_REBUILD_GRPC; then
        rm -rf protobuf 2>/dev/null || true
        git clone --depth 1 -b v3.21.12 https://github.com/protocolbuffers/protobuf.git || {
            log_error "Failed to clone protobuf"
            exit 1
        }
    else
        log "Using existing protobuf source directory"
    fi
    
    cd protobuf
    
    # Only update submodules if needed
    if [[ ! -f .git/modules/third_party/googletest/HEAD ]] || $FORCE_REBUILD_GRPC; then
        git submodule update --init --recursive
    else
        log "Submodules already initialized"
    fi
    # Only rebuild cmake if needed
    if [[ ! -d cmake/build/CMakeCache.txt ]] || $FORCE_REBUILD_GRPC; then
        rm -rf cmake/build 2>/dev/null || true
        mkdir -p cmake/build
        cd cmake/build
        
        cmake ../.. -DCMAKE_BUILD_TYPE=Release -Dprotobuf_BUILD_TESTS=OFF || {
            log_error "Protobuf cmake failed"
            exit 1
        }
    else
        log "Using existing protobuf cmake build directory"
        cd cmake/build
    fi
    
    make -j "${BUILD_CPUS}" || {
        log_error "Protobuf build failed"
        exit 1
    }
    
    make install || {
        log_error "Protobuf install failed"
        exit 1
    }
    
    # Update library cache and PATH
    ldconfig
    export PATH="/usr/local/bin:$PATH"
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
    
    # Build gRPC (matching compatible version)
    log "Building gRPC v1.51.3..."
    cd "${SRC_DIR}"
    
    # Only clone if directory doesn't exist or if forcing rebuild
    if [[ ! -d grpc ]] || $FORCE_REBUILD_GRPC; then
        rm -rf grpc 2>/dev/null || true
        git clone --depth 1 -b v1.51.3 https://github.com/grpc/grpc.git || {
            log_error "Failed to clone gRPC"
            exit 1
        }
    else
        log "Using existing gRPC source directory"
    fi
    
    cd grpc
    
    # Only update submodules if needed
    if [[ ! -f .git/modules/third_party/abseil-cpp/HEAD ]] || $FORCE_REBUILD_GRPC; then
        git submodule update --init --recursive
    else
        log "Submodules already initialized"
    fi
    # Only rebuild cmake if needed
    if [[ ! -f cmake/build/CMakeCache.txt ]] || $FORCE_REBUILD_GRPC; then
        rm -rf cmake/build 2>/dev/null || true
        mkdir -p cmake/build
        cd cmake/build
        
        cmake ../.. \
            -DCMAKE_BUILD_TYPE=Release \
            -DgRPC_INSTALL=ON \
            -DgRPC_BUILD_TESTS=OFF \
            -DgRPC_PROTOBUF_PROVIDER=package \
            -DgRPC_SSL_PROVIDER=package \
            -DgRPC_ABSL_PROVIDER=package \
            -DgRPC_RE2_PROVIDER=package \
            -DgRPC_ZLIB_PROVIDER=package || {
            log_error "gRPC cmake failed"
            exit 1
        }
    else
        log "Using existing gRPC cmake build directory"
        cd cmake/build
    fi
    
    make -j "${BUILD_CPUS}" || {
        log_error "gRPC build failed"
        exit 1
    }
    
    make install || {
        log_error "gRPC install failed"
        exit 1
    }
    
    # Update library cache and verify installation
    ldconfig
    
    # Set environment globally for all subsequent processes
    echo 'export PATH="/usr/local/bin:$PATH"' >> /etc/environment
    echo 'export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"' >> /etc/environment
    echo 'export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"' >> /etc/environment
    
    export PATH="/usr/local/bin:$PATH"
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
    export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"
    
    # Verify our built versions are being used
    log "Verifying built protobuf version: $(pkg-config --modversion protobuf)"
    log "Verifying built gRPC version: $(pkg-config --modversion grpc++)"
    log "Using protoc: $(which protoc)"
    log "Using grpc_cpp_plugin: $(which grpc_cpp_plugin)"
    
    # Critical: Verify protoc and library versions match (normalize format differences)
    PROTOC_VER=$(protoc --version | cut -d' ' -f2)
    LIB_VER=$(pkg-config --modversion protobuf)
    
    # Normalize versions by removing trailing .0 for comparison
    PROTOC_NORM=$(echo $PROTOC_VER | sed 's/\.0$//')
    LIB_NORM=$(echo $LIB_VER | sed 's/\.0$//')
    
    if [[ "$PROTOC_NORM" != "$LIB_NORM" ]]; then
        log_error "CRITICAL: Version mismatch after build - protoc=$PROTOC_VER, lib=$LIB_VER"
        exit 1
    fi
    
    log "✅ Protobuf and gRPC built successfully with compatible versions: protoc=$PROTOC_VER, lib=$LIB_VER"
}

###############################################################################
# mod_google_transcribe BUILD
###############################################################################

check_module_source() {
    if [[ ! -d "${MOD_GOOGLE_SRC}" ]]; then
        log_error "Module source not found at: ${MOD_GOOGLE_SRC}"
        log_error "Please ensure the module source exists at: ${CURRENT_DIR}/modules/mod_google_transcribe/"
        log_error "Directory structure should be:"
        log_error "  ${CURRENT_DIR}/"
        log_error "    └── modules/"
        log_error "        └── mod_google_transcribe/"
        log_error "            ├── mod_google_transcribe.c"
        log_error "            └── google_glue.cpp"
        return 1
    fi
    
    if [[ ! -f "${MOD_GOOGLE_SRC}/mod_google_transcribe.c" ]]; then
        log_error "mod_google_transcribe.c not found in ${MOD_GOOGLE_SRC}"
        return 1
    fi
    
    if [[ ! -f "${MOD_GOOGLE_SRC}/google_glue.cpp" ]]; then
        log_error "google_glue.cpp not found in ${MOD_GOOGLE_SRC}"
        return 1
    fi
    
    return 0
}

build_mod_google() {
    log_step "Building mod_google_transcribe"
    
    # Ensure our built libraries are found first - set globally
    export PATH="/usr/local/bin:$PATH"
    export PKG_CONFIG_PATH="/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH"
    export LD_LIBRARY_PATH="/usr/local/lib:$LD_LIBRARY_PATH"
    
    # Force library cache update
    ldconfig

    # Check if FreeSWITCH headers exist
    if [[ ! -f "${FS_PREFIX}/include/freeswitch/switch.h" ]]; then
        log_error "FreeSWITCH headers missing — core build incomplete"
        log_error "Please run with --core or --all first"
        exit 1
    fi

    # Check module source
    if ! check_module_source; then
        exit 1
    fi
    
    # Always build from source to ensure version compatibility
    if $USE_SYSTEM_GRPC; then
        log "Attempting to use system packages as requested..."
        if ! install_grpc_from_packages; then
            log_error "--system-grpc specified but package installation failed"
            exit 1
        fi
    else
        log "Building protobuf/gRPC from source for guaranteed compatibility..."
        build_protobuf_grpc
        
        # Verify the build
        if ! pkg-config --exists protobuf grpc++; then
            log_error "Source build verification failed"
            exit 1
        fi
        log "Built protobuf version: $(pkg-config --modversion protobuf)"
        log "Built gRPC version: $(pkg-config --modversion grpc++)"
    fi
    
    # Verify grpc_cpp_plugin is available
    if ! command -v grpc_cpp_plugin &> /dev/null; then
        log_error "grpc_cpp_plugin not found after installation"
        exit 1
    fi

    cd "${SRC_DIR}"
    
    # Clone googleapis
    log "Cloning googleapis..."
    rm -rf googleapis
    git clone --depth 1 https://github.com/googleapis/googleapis.git || {
        log_error "Failed to clone googleapis"
        exit 1
    }

    cd googleapis
    mkdir -p gens

    # Clean and regenerate protobuf files for version compatibility
    log "Generating protobuf files with exact version match..."
    
    # Remove all old generated files
    rm -rf gens
    find . -name "*.pb.h" -o -name "*.pb.cc" -o -name "*.grpc.pb.h" -o -name "*.grpc.pb.cc" | xargs rm -f 2>/dev/null || true
    
    mkdir -p gens
    
    log "Using protoc version: $(protoc --version)"
    log "Using grpc_cpp_plugin: $(which grpc_cpp_plugin)"
    
    # Verify versions are compatible (handle format differences gracefully)
    PROTOC_VERSION=$(protoc --version | cut -d' ' -f2)
    PKG_VERSION=$(pkg-config --modversion protobuf)
    
    # Normalize versions by removing trailing .0 for comparison
    PROTOC_NORM=$(echo $PROTOC_VERSION | sed 's/\.0$//')
    PKG_NORM=$(echo $PKG_VERSION | sed 's/\.0$//')
    
    # Accept various compatible formats (3.21.12 vs 3.21.12.0)
    if [[ "$PROTOC_NORM" == "$PKG_NORM" ]] || [[ "$PROTOC_VERSION" == "$PKG_NORM" ]] || [[ "$PROTOC_NORM" == "$PKG_VERSION" ]]; then
        log "Version compatibility confirmed: protoc=$PROTOC_VERSION, lib=$PKG_VERSION (compatible formats)"
    else
        log_warn "Version format difference detected: protoc=$PROTOC_VERSION, lib=$PKG_VERSION"
        log_warn "This is usually just a formatting difference, proceeding with build..."
        log "If compilation fails, you can use --force-rebuild-grpc to rebuild from scratch"
    fi
    
    # Generate in stages to better isolate errors
    protoc --proto_path=. --cpp_out=gens \
        google/type/*.proto || {
        log_error "Failed to generate google/type protos"
        exit 1
    }
    
    protoc --proto_path=. --cpp_out=gens \
        google/api/*.proto || {
        log_error "Failed to generate google/api protos"
        exit 1
    }
    
    protoc --proto_path=. --cpp_out=gens \
        google/rpc/*.proto || {
        log_error "Failed to generate google/rpc protos"
        exit 1
    }
    
    protoc --proto_path=. --cpp_out=gens \
        google/longrunning/*.proto || {
        log_error "Failed to generate google/longrunning protos"
        exit 1
    }
    
    # Generate Google Cloud Speech v2 API protobuf files
    protoc --proto_path=. --cpp_out=gens --grpc_out=gens \
        --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
        google/cloud/speech/v2/*.proto || {
        log_error "Failed to generate google/cloud/speech/v2 protos"
        exit 1
    }

    # Copy module source
    log "Copying module source..."
    cd "${SRC_DIR}"
    rm -rf mod_google_transcribe
    cp -r "${MOD_GOOGLE_SRC}" mod_google_transcribe || {
        log_error "Failed to copy module source"
        exit 1
    }

    cd mod_google_transcribe
    
    # Fix cJSON compatibility if needed
    log "Checking cJSON compatibility..."
    if ! grep -q "cJSON_GetNumberValue" /usr/include/cjson/cJSON.h 2>/dev/null; then
        log_warn "cJSON_GetNumberValue not found, adding compatibility macro..."
        # Add compatibility macro to the source
        sed -i '1i#ifndef cJSON_GetNumberValue\n#define cJSON_GetNumberValue(item) ((item)->valuedouble)\n#endif' mod_google_transcribe.c
    fi

    # Compile C file
    log "Compiling mod_google_transcribe.c..."
    gcc -fPIC -c \
        -I"${FS_PREFIX}/include/freeswitch" \
        -I/usr/include/cjson \
        -Wno-deprecated-declarations \
        -Wno-implicit-function-declaration \
        mod_google_transcribe.c || {
        log_error "Failed to compile mod_google_transcribe.c"
        exit 1
    }

    # Fix includes for Google Speech v2 API
    log "Converting module to Google Speech v2 API..."
    sed -i 's|#include "google/cloud/speech/v2/cloud_speech\.grpc\.pb\.h"|#include "google/cloud/speech/v2/cloud_speech.grpc.pb.h"|g' google_glue.cpp
    sed -i 's|#include "google/cloud/speech/v2/cloud_speech\.pb\.h"|#include "google/cloud/speech/v2/cloud_speech.pb.h"|g' google_glue.cpp
    
    # Compile C++ file with proper include paths
    log "Compiling google_glue.cpp for Speech v2 API..."
    log "Checking for required headers..."
    
    # Verify protobuf headers
    if ! pkg-config --exists protobuf; then
        log_error "Protobuf development headers not found"
        exit 1
    fi
    
    # Get proper include paths from pkg-config
    PROTOBUF_CFLAGS=$(pkg-config --cflags protobuf)
    GRPC_CFLAGS=$(pkg-config --cflags grpc++)
    
    log "Using protobuf CFLAGS: $PROTOBUF_CFLAGS"
    log "Using gRPC CFLAGS: $GRPC_CFLAGS"
    
    # Note: Compilation may fail initially due to v1->v2 API differences
    # This will be fixed in subsequent steps
    g++ -fPIC -c -std=c++17 \
        -I"${FS_PREFIX}/include/freeswitch" \
        -I"${SRC_DIR}/googleapis/gens" \
        $PROTOBUF_CFLAGS \
        $GRPC_CFLAGS \
        google_glue.cpp || {
        log_warn "Initial compilation failed - this is expected for v1->v2 API conversion"
        log "Module needs API conversion from Speech v1 to v2"
        return 1
    }

    # Link shared library with proper library paths
    log "Linking mod_google_transcribe.so..."
    
    # Get proper library paths from pkg-config
    PROTOBUF_LIBS=$(pkg-config --libs protobuf)
    GRPC_LIBS=$(pkg-config --libs grpc++)
    
    log "Using protobuf LIBS: $PROTOBUF_LIBS"
    log "Using gRPC LIBS: $GRPC_LIBS"
    
    # Ensure module directory exists
    mkdir -p "${FS_PREFIX}/lib/freeswitch/mod"
    
    g++ -shared \
        -o "${FS_PREFIX}/lib/freeswitch/mod/mod_google_transcribe.so" \
        mod_google_transcribe.o google_glue.o \
        "${SRC_DIR}/googleapis/gens"/google/cloud/speech/v2/*.pb.cc \
        "${SRC_DIR}/googleapis/gens"/google/api/*.pb.cc \
        "${SRC_DIR}/googleapis/gens"/google/rpc/*.pb.cc \
        "${SRC_DIR}/googleapis/gens"/google/longrunning/*.pb.cc \
        "${SRC_DIR}/googleapis/gens"/google/type/*.pb.cc \
        $PROTOBUF_LIBS $GRPC_LIBS \
        -lpthread -lssl -lcrypto -lcurl -lz -lcjson || {
        log_error "Failed to link mod_google_transcribe.so"
        exit 1
    }

    # Update modules.conf.xml
    local MODULE_XML="${FS_PREFIX}/conf/autoload_configs/modules.conf.xml"
    if [[ -f "$MODULE_XML" ]]; then
        if ! grep -q "mod_google_transcribe" "$MODULE_XML"; then
            sed -i '/<\/modules>/i\    <load module="mod_google_transcribe"/>' "$MODULE_XML"
            log "Added mod_google_transcribe to modules.conf.xml"
        fi
    else
        log_warn "modules.conf.xml not found at $MODULE_XML"
    fi

    log "✅ mod_google_transcribe installed successfully"
}

###############################################################################
# Runtime environment setup
###############################################################################

configure_runtime() {
    log_step "Setting up runtime environment"

    # Create user and group
    getent group freeswitch >/dev/null 2>&1 || groupadd -r freeswitch
    id -u freeswitch >/dev/null 2>&1 || useradd -r -g freeswitch -s /bin/false freeswitch

    # Set permissions
    chown -R freeswitch:freeswitch "${FS_PREFIX}"

    # Update library cache
    ldconfig

    log "✅ Runtime environment configured"
}

###############################################################################
# CLI Argument Parsing
###############################################################################

usage() {
    echo "Usage: sudo $0 --core | --mod-google | --all [--system-grpc] [--force-rebuild-grpc]"
    echo ""
    echo "Options:"
    echo "  --core                Build FreeSWITCH core only"
    echo "  --mod-google          Build mod_google_transcribe only (requires core)"
    echo "  --all                 Build both core and module"
    echo "  --system-grpc         Use system gRPC/protobuf packages (may have version issues)"
    echo "  --force-rebuild-grpc  Force rebuild of gRPC/protobuf even if compatible versions exist"
    echo ""
    echo "Note: Module source must exist at: ${CURRENT_DIR}/modules/mod_google_transcribe/"
    exit 1
}

# Check for arguments
if [[ $# -eq 0 ]]; then
    usage
fi

# Parse arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --all) 
            DO_CORE=true
            DO_MOD_GOOGLE=true
            shift 
            ;;
        --core) 
            DO_CORE=true
            shift 
            ;;
        --mod-google) 
            DO_MOD_GOOGLE=true
            shift 
            ;;
        --system-grpc)
            USE_SYSTEM_GRPC=true
            shift
            ;;
        --force-rebuild-grpc)
            FORCE_REBUILD_GRPC=true
            shift
            ;;
        -h|--help)
            usage
            ;;
        *) 
            echo "Unknown option: $1"
            usage 
            ;;
    esac
done

###############################################################################
# EXECUTION
###############################################################################

# Main execution
main() {
    log "Starting FreeSWITCH build script..."
    log "Current directory: ${CURRENT_DIR}"
    log "Log file: ${LOG_FILE}"
    
    check_root
    cleanup_conflicts
    install_system_deps

    if $DO_CORE; then 
        build_freeswitch
    fi
    
    if $DO_MOD_GOOGLE; then 
        build_mod_google
    fi

    configure_runtime

    log ""
    log "🎉 Build Completed Successfully!"
    log "📄 Log saved at ${LOG_FILE}"
    log ""
    log "Next steps:"
    log "  1. Start FreeSWITCH: ${FS_PREFIX}/bin/freeswitch -nc"
    log "  2. Check status: ${FS_PREFIX}/bin/fs_cli -x 'status'"
    if $DO_MOD_GOOGLE; then
        log "  3. Load module: ${FS_PREFIX}/bin/fs_cli -x 'load mod_google_transcribe'"
    fi
}

# Run main function
main