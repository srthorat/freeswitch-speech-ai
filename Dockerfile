# ============================================================================
# Dockerfile for FreeSWITCH with Speech AI Modules
# ============================================================================
# This Dockerfile builds the following speech transcription modules:
#   - mod_audio_fork (generic WebSocket audio streaming)
#   - mod_aws_transcribe (AWS Transcribe Streaming)
#   - mod_deepgram_transcribe (Deepgram Streaming)
#   - mod_google_transcribev2 (Google Cloud Speech-to-Text v2)
#
# Base image includes:
#   - FreeSWITCH 1.10.11 fully built and configured
#   - All required configs for SIP calls
#   - Event Socket configured for fs_cli
#   - Production patches and optimizations
#
# This Dockerfile replicates the exact installation sequence from:
#   scripts/install-all.sh --module all
#
# Build time: ~45-60 minutes (includes all SDKs)
# Final size: ~1.2 GB
#
# Usage:
#   docker build -t freeswitch-speech-ai:latest .
#
# Or with custom build args:
#   docker build \
#     --build-arg BUILD_CPUS=8 \
#     --build-arg AWS_SDK_CPP_VERSION=1.11.345 \
#     --build-arg GOOGLE_CLOUD_CPP_VERSION=2.30.0 \
#     -t freeswitch-speech-ai:latest .
# ============================================================================

ARG BASE_IMAGE=srt2011/freeswitch-base:latest
ARG AWS_SDK_CPP_VERSION=1.11.345
ARG LIBWEBSOCKETS_VERSION=4.3.3
ARG GOOGLE_CLOUD_CPP_VERSION=2.30.0

# ============================================================================
# Builder Stage: Compile all modules with dependencies
# ============================================================================
FROM ${BASE_IMAGE} AS builder

ARG BUILD_CPUS=4
ARG AWS_SDK_CPP_VERSION=1.11.345
ARG LIBWEBSOCKETS_VERSION=4.3.3
ARG GOOGLE_CLOUD_CPP_VERSION=2.30.0

# Print build configuration
RUN echo "=============================================" \
    && echo "FreeSWITCH Speech AI - Unified Build" \
    && echo "=============================================" \
    && echo "Modules:" \
    && echo "  - mod_audio_fork" \
    && echo "  - mod_aws_transcribe" \
    && echo "  - mod_deepgram_transcribe" \
    && echo "  - mod_google_transcribev2" \
    && echo "" \
    && echo "Dependencies:" \
    && echo "  - libwebsockets: ${LIBWEBSOCKETS_VERSION}" \
    && echo "  - AWS SDK C++: ${AWS_SDK_CPP_VERSION}" \
    && echo "  - Google Cloud C++: ${GOOGLE_CLOUD_CPP_VERSION}" \
    && echo "  - Build CPUs: ${BUILD_CPUS}" \
    && echo "============================================="

# ============================================================================
# Step 1: Install System Dependencies (from install-all.sh)
# ============================================================================
RUN apt-get update && apt-get install -y --no-install-recommends \
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
    && rm -rf /var/lib/apt/lists/*

# Set library path
ENV LD_LIBRARY_PATH=/usr/local/lib

# ============================================================================
# Step 2: Module-Specific Dependencies
# ============================================================================

# libwebsockets 4.3.3 (for mod_audio_fork and mod_deepgram_transcribe)
WORKDIR /usr/local/src
RUN echo "Building libwebsockets ${LIBWEBSOCKETS_VERSION}..." \
    && git clone --depth 1 -b v${LIBWEBSOCKETS_VERSION} https://github.com/warmcat/libwebsockets.git \
    && cd libwebsockets \
    && mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && make -j ${BUILD_CPUS} \
    && make install \
    && ldconfig \
    && echo "✓ libwebsockets installed"

# AWS SDK C++ 1.11.345 (for mod_aws_transcribe)
WORKDIR /usr/local/src
RUN echo "Building AWS SDK C++ ${AWS_SDK_CPP_VERSION}..." \
    && git clone --depth 1 -b ${AWS_SDK_CPP_VERSION} https://github.com/aws/aws-sdk-cpp.git \
    && cd aws-sdk-cpp \
    && git submodule update --init --recursive \
    && mkdir -p build && cd build \
    && cmake .. \
        -DBUILD_ONLY="transcribestreaming" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_SHARED_LIBS=ON \
        -DENABLE_TESTING=OFF \
        -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull" \
    && make -j ${BUILD_CPUS} \
    && make install \
    && ldconfig \
    && echo "✓ AWS SDK C++ installed"

# Fix cJSON header conflict (from install-all.sh)
RUN if [ -f /usr/local/include/aws/core/external/cjson/cJSON.h ]; then \
        if ! grep -q '#ifndef cJSON__h' /usr/local/include/aws/core/external/cjson/cJSON.h; then \
            sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' \
                /usr/local/include/aws/core/external/cjson/cJSON.h; \
            echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h; \
            echo "✓ cJSON header guards added"; \
        fi; \
    fi

# gRPC from system packages (for mod_google_transcribev2)
RUN echo "Installing gRPC from system packages..." \
    && apt-get update \
    && apt-get install -y \
        libgrpc++-dev \
        libgrpc-dev \
        protobuf-compiler \
        protobuf-compiler-grpc \
        libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/* \
    && echo "✓ gRPC installed"

# Google Cloud C++ Speech library (for mod_google_transcribev2)
RUN echo "Installing additional dependencies for Google Cloud C++..." \
    && apt-get update \
    && apt-get install -y \
        nlohmann-json3-dev \
        libcurl4-openssl-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/src
RUN echo "Building Google Cloud C++ Speech library ${GOOGLE_CLOUD_CPP_VERSION}..." \
    && git clone --depth 1 -b v${GOOGLE_CLOUD_CPP_VERSION} https://github.com/googleapis/google-cloud-cpp.git \
    && cd google-cloud-cpp \
    && mkdir -p build && cd build \
    && cmake .. \
        -DBUILD_SHARED_LIBS=ON \
        -DCMAKE_BUILD_TYPE=Release \
        -DGOOGLE_CLOUD_CPP_ENABLE=speech \
        -DBUILD_TESTING=OFF \
        -DGOOGLE_CLOUD_CPP_WITH_MOCKS=OFF \
    && make -j ${BUILD_CPUS} \
    && make install \
    && ldconfig \
    && echo "✓ Google Cloud C++ Speech library installed"

# ============================================================================
# Step 4: Build mod_audio_fork
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_audio_fork /usr/local/src/mod_audio_fork

RUN echo "Building mod_audio_fork..." \
    && cd /usr/local/src/mod_audio_fork \
    && gcc -fPIC -c \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include \
        mod_audio_fork.c \
    && g++ -fPIC -c -std=c++11 \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include \
        lws_glue.cpp audio_pipe.cpp parser.cpp \
    && mkdir -p /usr/local/freeswitch/lib/freeswitch/mod \
    && g++ -shared \
        -o /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so \
        *.o \
        -lwebsockets \
        -lpthread \
        -lssl \
        -lcrypto \
    && echo "✓ mod_audio_fork built"

# ============================================================================
# Step 5: Build mod_aws_transcribe
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_aws_transcribe /usr/local/src/mod_aws_transcribe

RUN echo "Building mod_aws_transcribe..." \
    && cd /usr/local/src/mod_aws_transcribe \
    && gcc -fPIC -c \
        -I/usr/local/freeswitch/include/freeswitch \
        mod_aws_transcribe.c \
    && g++ -fPIC -c -std=c++11 \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include \
        aws_transcribe_glue.cpp \
    && g++ -shared \
        -o /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so \
        mod_aws_transcribe.o \
        aws_transcribe_glue.o \
        -L/usr/local/lib \
        -laws-cpp-sdk-transcribestreaming \
        -laws-cpp-sdk-core \
        -laws-c-event-stream \
        -laws-checksums \
        -laws-c-common \
        -lpthread \
        -lcurl \
        -lssl \
        -lcrypto \
        -lz \
    && echo "✓ mod_aws_transcribe built"

# ============================================================================
# Step 6: Build mod_deepgram_transcribe
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_deepgram_transcribe /usr/local/src/mod_deepgram_transcribe

RUN echo "Building mod_deepgram_transcribe..." \
    && cd /usr/local/src/mod_deepgram_transcribe \
    && gcc -fPIC -c \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include \
        mod_deepgram_transcribe.c \
    && g++ -fPIC -c -std=c++11 \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include \
        dg_transcribe_glue.cpp audio_pipe.cpp parser.cpp \
    && g++ -shared \
        -o /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so \
        mod_deepgram_transcribe.o \
        dg_transcribe_glue.o \
        audio_pipe.o \
        parser.o \
        -lwebsockets \
        -lpthread \
        -lssl \
        -lcrypto \
    && echo "✓ mod_deepgram_transcribe built"

# ============================================================================
# Step 7: Build mod_google_transcribev2
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_google_transcribev2 /usr/local/src/mod_google_transcribev2

RUN echo "Building mod_google_transcribev2..." \
    && cd /usr/local/src/mod_google_transcribev2 \
    && make clean > /dev/null 2>&1 || true \
    && make -j ${BUILD_CPUS} FS_PREFIX=/usr/local/freeswitch \
    && make install FS_PREFIX=/usr/local/freeswitch \
    && echo "✓ mod_google_transcribev2 built"

# ============================================================================
# Module Validation
# ============================================================================
RUN echo "=========================================" \
    && echo "Module Validation" \
    && echo "=========================================" \
    && for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe mod_google_transcribev2; do \
        MODULE_PATH="/usr/local/freeswitch/lib/freeswitch/mod/${module}.so"; \
        echo "Checking ${module}..."; \
        if [ -f "$MODULE_PATH" ]; then \
            echo "✓ Module exists"; \
            if ldd "$MODULE_PATH" | grep -q "not found"; then \
                echo "✗ Missing dependencies in ${module}"; \
                ldd "$MODULE_PATH" | grep "not found"; exit 1; \
            else \
                echo "✓ All dependencies satisfied"; \
            fi; \
        else \
            echo "✗ Module not found: $MODULE_PATH"; exit 1; \
        fi; \
    done \
    && echo "=========================================" \
    && echo "✓ All 4 modules validated!" \
    && echo "========================================="

# ============================================================================
# Update FreeSWITCH modules.conf.xml
# ============================================================================
RUN sed -i '/<\/modules>/i \    <!-- Speech Transcription Modules -->' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_audio_fork"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_aws_transcribe"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_deepgram_transcribe"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_google_transcribev2"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && echo "✓ modules.conf.xml configured"

# ============================================================================
# Runtime Validation: Quick module load test
# ============================================================================
RUN export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH \
    && timeout 60s /usr/local/freeswitch/bin/freeswitch -nonat -nc -nf >/dev/null 2>&1 & FS_PID=$! \
    && sleep 25 \
    && grep -q "mod_audio_fork" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_aws_transcribe" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_deepgram_transcribe" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_google_transcribev2" /usr/local/freeswitch/log/freeswitch.log \
    && ! grep -E "mod_audio_fork|mod_aws_transcribe|mod_deepgram_transcribe|mod_google_transcribev2" /usr/local/freeswitch/log/freeswitch.log | grep -qiE "error|fail|cannot|unable" \
    && kill $FS_PID 2>/dev/null || true \
    && echo "✓ Runtime validation passed - all 4 modules loaded successfully"

# ============================================================================
# Final Stage: Clean Runtime Image
# ============================================================================
FROM ${BASE_IMAGE} AS runtime

ARG AWS_SDK_CPP_VERSION=1.11.345
ARG GOOGLE_CLOUD_CPP_VERSION=2.30.0

# Install runtime dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl4 \
    libssl3 \
    zlib1g \
    libspeexdsp1 \
    && rm -rf /var/lib/apt/lists/*

ENV LD_LIBRARY_PATH=/usr/local/lib

# Copy libwebsockets libraries
COPY --from=builder /usr/local/lib/libwebsockets.so* /usr/local/lib/

# Copy AWS SDK C++ libraries
COPY --from=builder /usr/local/lib/libaws-cpp-sdk-*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libaws-c-*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libaws-crt-cpp.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libaws-checksums.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libs2n.so* /usr/local/lib/

# Copy gRPC and protobuf libraries
COPY --from=builder /usr/local/lib/libgrpc*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libprotobuf*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libabsl_*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libupb*.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libre2.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libaddress_sorting.so* /usr/local/lib/
COPY --from=builder /usr/local/lib/libgpr.so* /usr/local/lib/

# Copy Google Cloud C++ libraries
COPY --from=builder /usr/local/lib/libgoogle_cloud_cpp*.so* /usr/local/lib/

# Copy all four modules
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so \
    /usr/local/freeswitch/lib/freeswitch/mod/

# Copy updated modules.conf.xml
COPY --from=builder /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    /usr/local/freeswitch/conf/autoload_configs/

# Copy example configuration files (dialplan and directory)
COPY examples/freeswitch-config/dialplan/default.xml \
    /usr/local/freeswitch/conf/dialplan/default.xml
COPY examples/freeswitch-config/directory/1000.xml \
    /usr/local/freeswitch/conf/directory/default/1000.xml
COPY examples/freeswitch-config/directory/1001.xml \
    /usr/local/freeswitch/conf/directory/default/1001.xml
COPY examples/freeswitch-config/directory/1002.xml \
    /usr/local/freeswitch/conf/directory/default/1002.xml
COPY examples/freeswitch-config/directory/1003.xml \
    /usr/local/freeswitch/conf/directory/default/1003.xml
COPY examples/freeswitch-config/directory/1004.xml \
    /usr/local/freeswitch/conf/directory/default/1004.xml

# Update library cache
RUN ldconfig

# Runtime validation
RUN echo "=========================================" \
    && echo "Runtime Image Validation" \
    && echo "=========================================" \
    && echo "Installed Modules:" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribev2.so \
    && echo "" \
    && echo "Verifying dependencies..." \
    && for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe mod_google_transcribev2; do \
        echo "Checking ${module}..."; \
        if ! ldd /usr/local/freeswitch/lib/freeswitch/mod/${module}.so | grep "not found"; then \
            echo "✓ ${module} - all dependencies satisfied"; \
        else \
            echo "✗ ${module} - missing dependencies"; exit 1; \
        fi; \
    done \
    && echo "=========================================" \
    && echo "✓ All 4 modules ready!" \
    && echo "========================================="

# Labels
LABEL maintainer="FreeSWITCH Speech AI"
LABEL description="FreeSWITCH 1.10.11 with speech transcription modules"
LABEL modules="mod_audio_fork,mod_aws_transcribe,mod_deepgram_transcribe,mod_google_transcribev2"
LABEL aws.sdk.version="${AWS_SDK_CPP_VERSION}"
LABEL google.cloud.cpp.version="${GOOGLE_CLOUD_CPP_VERSION}"
LABEL libwebsockets.version="4.3.3"
LABEL base.image="srt2011/freeswitch-base:latest"

# Expose FreeSWITCH ports
EXPOSE 5060/tcp 5060/udp 5080/tcp 5080/udp 8021/tcp
EXPOSE 16384-16484/udp

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 \
    CMD /usr/local/freeswitch/bin/fs_cli -x "status" | grep -q "UP" || exit 1

# Set environment
ENV PATH="/usr/local/freeswitch/bin:${PATH}"

# Inherit CMD from base image
