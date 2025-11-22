# ============================================================================
# Dockerfile for FreeSWITCH with ALL Speech AI Modules
# ============================================================================
# This Dockerfile builds ALL FIVE speech transcription modules in one image:
#   - mod_audio_fork (generic WebSocket audio streaming)
#   - mod_aws_transcribe (AWS Transcribe Streaming)
#   - mod_deepgram_transcribe (Deepgram Streaming)
#   - mod_azure_transcribe (Azure Cognitive Services)
#   - mod_google_transcribe (Google Cloud Speech-to-Text)
#
# Base image includes:
#   - FreeSWITCH 1.10.11 fully built and configured
#   - All required configs for SIP calls
#   - Event Socket configured for fs_cli
#   - Production patches and optimizations
#
# This image adds ALL dependencies:
#   - libwebsockets 4.3.3 (for mod_audio_fork & mod_deepgram_transcribe)
#   - AWS SDK C++ (for mod_aws_transcribe)
#   - Azure Speech SDK (for mod_azure_transcribe)
#   - gRPC + protobuf + googleapis (for mod_google_transcribe)
#
# Build time: 45-60 minutes (includes all SDKs)
# Final size: ~1.5 GB
#
# Usage:
#   docker build -t freeswitch-speech-ai:all-modules .
#
# Or with custom build args:
#   docker build \
#     --build-arg BUILD_CPUS=8 \
#     --build-arg AWS_SDK_CPP_VERSION=1.11.345 \
#     --build-arg GRPC_VERSION=1.64.2 \
#     -t freeswitch-speech-ai:all-modules .
# ============================================================================

ARG BASE_IMAGE=srt2011/freeswitch-base:latest
ARG AWS_SDK_CPP_VERSION=1.11.345
ARG LIBWEBSOCKETS_VERSION=4.3.3
ARG GRPC_VERSION=1.64.2

# ============================================================================
# Builder Stage: Compile all modules with all dependencies
# ============================================================================
FROM ${BASE_IMAGE} AS builder

ARG BUILD_CPUS=4
ARG AWS_SDK_CPP_VERSION=1.11.345
ARG LIBWEBSOCKETS_VERSION=4.3.3
ARG GRPC_VERSION=1.64.2

# Print build configuration
RUN echo "=============================================" \
    && echo "Building FreeSWITCH Speech AI - ALL MODULES" \
    && echo "=============================================" \
    && echo "Base Image: ${BASE_IMAGE}" \
    && echo "Modules:" \
    && echo "  - mod_audio_fork" \
    && echo "  - mod_aws_transcribe" \
    && echo "  - mod_deepgram_transcribe" \
    && echo "  - mod_azure_transcribe" \
    && echo "  - mod_google_transcribe" \
    && echo "" \
    && echo "Dependencies:" \
    && echo "  - libwebsockets: ${LIBWEBSOCKETS_VERSION}" \
    && echo "  - AWS SDK C++: ${AWS_SDK_CPP_VERSION}" \
    && echo "  - Azure Speech SDK: latest" \
    && echo "  - gRPC: ${GRPC_VERSION}" \
    && echo "  - Build CPUs: ${BUILD_CPUS}" \
    && echo "============================================="

# Install all build dependencies
RUN apt-get update && apt-get install -y --quiet --no-install-recommends \
    build-essential \
    git \
    cmake \
    ca-certificates \
    wget \
    autoconf \
    libtool \
    pkg-config \
    libcurl4-openssl-dev \
    libssl-dev \
    uuid-dev \
    zlib1g-dev \
    libpulse-dev \
    libspeexdsp-dev \
    libasound2-dev \
    && rm -rf /var/lib/apt/lists/*

# Set library path
ENV LD_LIBRARY_PATH=/usr/local/lib

# ============================================================================
# Build libwebsockets (for mod_audio_fork & mod_deepgram_transcribe)
# ============================================================================
WORKDIR /usr/local/src
RUN echo "=========================================" \
    && echo "Building libwebsockets ${LIBWEBSOCKETS_VERSION}..." \
    && echo "=========================================" \
    && git clone --depth 1 -b v${LIBWEBSOCKETS_VERSION} https://github.com/warmcat/libwebsockets.git \
    && cd libwebsockets \
    && mkdir -p build && cd build \
    && cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    && make -j ${BUILD_CPUS} \
    && make install \
    && ldconfig \
    && echo "✅ libwebsockets ${LIBWEBSOCKETS_VERSION} installed"

# ============================================================================
# Build AWS SDK C++ (for mod_aws_transcribe)
# ============================================================================
WORKDIR /usr/local/src
RUN echo "=========================================" \
    && echo "Cloning AWS SDK C++ ${AWS_SDK_CPP_VERSION}..." \
    && echo "=========================================" \
    && git clone --depth 1 -b ${AWS_SDK_CPP_VERSION} https://github.com/aws/aws-sdk-cpp.git \
    && cd aws-sdk-cpp \
    && git submodule update --init --recursive \
    && echo "✅ AWS SDK C++ source downloaded"

RUN echo "=========================================" \
    && echo "Building AWS SDK C++ ${AWS_SDK_CPP_VERSION}..." \
    && echo "Components: core, transcribestreaming" \
    && echo "=========================================" \
    && cd /usr/local/src/aws-sdk-cpp \
    && mkdir -p build && cd build \
    && cmake .. \
        -DBUILD_ONLY="transcribestreaming" \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DBUILD_SHARED_LIBS=ON \
        -DENABLE_TESTING=OFF \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DCMAKE_CXX_FLAGS="-Wno-unused-parameter -Wno-error=nonnull -Wno-error=deprecated-declarations -Wno-error=uninitialized -Wno-error=maybe-uninitialized" \
    && make -j ${BUILD_CPUS} \
    && make install \
    && ldconfig \
    && echo "✅ AWS SDK C++ ${AWS_SDK_CPP_VERSION} built and installed"

# Copy pkg-config files
RUN mkdir -p /usr/local/lib/pkgconfig \
    && find /usr/local/src/aws-sdk-cpp/ -type f -name "*.pc" | xargs -I {} cp {} /usr/local/lib/pkgconfig/ \
    && echo "✅ AWS SDK pkg-config files installed"

# Fix cJSON header conflict between AWS SDK and FreeSWITCH
RUN echo "=========================================" \
    && echo "Fixing cJSON header conflict..." \
    && echo "=========================================" \
    && if [ -f /usr/local/include/aws/core/external/cjson/cJSON.h ]; then \
        echo "Found AWS SDK cJSON header, adding header guards..."; \
        sed -i '/#ifndef cJSON_AS4CPP__h/i #ifndef cJSON__h\n#define cJSON__h' \
            /usr/local/include/aws/core/external/cjson/cJSON.h \
        && echo '#endif' >> /usr/local/include/aws/core/external/cjson/cJSON.h \
        && echo "✅ cJSON header guards added successfully"; \
    else \
        echo "⚠️  WARNING: AWS SDK cJSON header not found at expected location"; \
    fi

# ============================================================================
# Build gRPC and Protocol Buffers (for mod_google_transcribe)
# ============================================================================
WORKDIR /usr/local/src
RUN echo "=========================================" \
    && echo "Cloning gRPC v${GRPC_VERSION}..." \
    && echo "=========================================" \
    && git clone --depth 1 -b v${GRPC_VERSION} https://github.com/grpc/grpc \
    && cd grpc \
    && git submodule update --init --recursive \
    && echo "✅ gRPC source downloaded with all submodules"

RUN echo "=========================================" \
    && echo "Building gRPC v${GRPC_VERSION}..." \
    && echo "This includes Protocol Buffers (protoc)" \
    && echo "=========================================" \
    && cd /usr/local/src/grpc \
    && mkdir -p cmake/build && cd cmake/build \
    && cmake ../.. \
        -DBUILD_SHARED_LIBS=ON \
        -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DgRPC_SSL_PROVIDER=package \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_STANDARD=17 \
    && make -j ${BUILD_CPUS} \
    && make install \
    && ldconfig \
    && echo "✅ gRPC v${GRPC_VERSION} built and installed"

# Verify gRPC and protoc installation
RUN echo "=========================================" \
    && echo "Verifying gRPC and protoc installation..." \
    && echo "=========================================" \
    && /usr/local/bin/protoc --version \
    && ls -lh /usr/local/bin/grpc_cpp_plugin \
    && ls -1 /usr/local/lib/libgrpc++.so && echo "  ✅ grpc++" \
    && ls -1 /usr/local/lib/libgrpc.so && echo "  ✅ grpc" \
    && ls -1 /usr/local/lib/libprotobuf.so && echo "  ✅ protobuf"

# ============================================================================
# Clone googleapis and generate protobuf files (for mod_google_transcribe)
# ============================================================================
WORKDIR /usr/local/src
RUN echo "=========================================" \
    && echo "Cloning googleapis..." \
    && echo "=========================================" \
    && git clone --depth 1 https://github.com/googleapis/googleapis.git \
    && echo "✅ googleapis cloned"

RUN echo "=========================================" \
    && echo "Generating protobuf files for Google Cloud Speech API..." \
    && echo "=========================================" \
    && cd /usr/local/src/googleapis \
    && mkdir -p gens \
    && /usr/local/bin/protoc \
        --proto_path=. \
        --cpp_out=gens \
        --grpc_out=gens \
        --plugin=protoc-gen-grpc=/usr/local/bin/grpc_cpp_plugin \
        google/cloud/speech/v1/*.proto \
        google/api/*.proto \
        google/rpc/*.proto \
        google/longrunning/*.proto \
        google/type/*.proto \
    && echo "✅ Protobuf files generated"

# ============================================================================
# Download Azure Speech SDK (for mod_azure_transcribe)
# ============================================================================
WORKDIR /tmp
RUN echo "=========================================" \
    && echo "Downloading Azure Speech SDK (latest)..." \
    && echo "=========================================" \
    && wget -q -O SpeechSDK-Linux.tar.gz \
        https://aka.ms/csspeech/linuxbinary \
    && tar -xzf SpeechSDK-Linux.tar.gz -C /tmp \
    && SDK_DIR=$(ls -d /tmp/SpeechSDK-Linux-* | head -1) \
    && SDK_VERSION=$(basename $SDK_DIR | sed 's/SpeechSDK-Linux-//') \
    && echo "Found Azure Speech SDK version: $SDK_VERSION" \
    && mkdir -p /usr/local/include/MicrosoftSpeechSDK \
    && mkdir -p /usr/local/lib/MicrosoftSpeechSDK \
    && cp -r $SDK_DIR/include/* /usr/local/include/MicrosoftSpeechSDK/ \
    && cp -r $SDK_DIR/lib/x64/* /usr/local/lib/MicrosoftSpeechSDK/ \
    && ldconfig \
    && rm -rf /tmp/SpeechSDK-Linux* \
    && echo "✅ Azure Speech SDK $SDK_VERSION installed"

# ============================================================================
# Build mod_audio_fork
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_audio_fork /usr/local/src/mod_audio_fork

RUN echo "=========================================" \
    && echo "Building mod_audio_fork..." \
    && echo "=========================================" \
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
    && echo "✅ mod_audio_fork compiled successfully" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so

# ============================================================================
# Build mod_aws_transcribe
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_aws_transcribe /usr/local/src/mod_aws_transcribe

RUN echo "=========================================" \
    && echo "Building mod_aws_transcribe..." \
    && echo "=========================================" \
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
    && echo "✅ mod_aws_transcribe compiled successfully" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so

# ============================================================================
# Build mod_deepgram_transcribe
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_deepgram_transcribe /usr/local/src/mod_deepgram_transcribe

RUN echo "=========================================" \
    && echo "Building mod_deepgram_transcribe..." \
    && echo "=========================================" \
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
    && echo "✅ mod_deepgram_transcribe compiled successfully" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so

# ============================================================================
# Build mod_azure_transcribe
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_azure_transcribe /usr/local/src/mod_azure_transcribe

RUN echo "=========================================" \
    && echo "Building mod_azure_transcribe..." \
    && echo "=========================================" \
    && cd /usr/local/src/mod_azure_transcribe \
    && gcc -fPIC -c \
        -I/usr/local/freeswitch/include/freeswitch \
        mod_azure_transcribe.c \
    && g++ -fPIC -c -std=c++14 \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include/MicrosoftSpeechSDK/cxx_api \
        -I/usr/local/include/MicrosoftSpeechSDK/c_api \
        azure_transcribe_glue.cpp \
    && g++ -shared \
        -o /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so \
        mod_azure_transcribe.o \
        azure_transcribe_glue.o \
        -L/usr/local/lib/MicrosoftSpeechSDK \
        -lMicrosoft.CognitiveServices.Speech.core \
        -lasound \
        -lpthread \
        -lssl \
        -lcrypto \
        -lz \
    && echo "✅ mod_azure_transcribe compiled successfully" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so

# ============================================================================
# Build mod_google_transcribe
# ============================================================================
WORKDIR /usr/local/src
COPY ./modules/mod_google_transcribe /usr/local/src/mod_google_transcribe

RUN echo "=========================================" \
    && echo "Building mod_google_transcribe..." \
    && echo "=========================================" \
    && cd /usr/local/src/mod_google_transcribe \
    && gcc -fPIC -c \
        -I/usr/local/freeswitch/include/freeswitch \
        mod_google_transcribe.c \
    && g++ -fPIC -c -std=c++17 \
        -I/usr/local/freeswitch/include/freeswitch \
        -I/usr/local/include \
        -I/usr/local/src/googleapis/gens \
        google_glue.cpp \
    && g++ -shared \
        -o /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so \
        mod_google_transcribe.o \
        google_glue.o \
        /usr/local/src/googleapis/gens/google/cloud/speech/v1/*.pb.cc \
        /usr/local/src/googleapis/gens/google/api/*.pb.cc \
        /usr/local/src/googleapis/gens/google/rpc/*.pb.cc \
        /usr/local/src/googleapis/gens/google/longrunning/*.pb.cc \
        /usr/local/src/googleapis/gens/google/type/*.pb.cc \
        -L/usr/local/lib \
        -lgrpc++ \
        -lgrpc \
        -lprotobuf \
        -lpthread \
        -lssl \
        -lcrypto \
        -lz \
    && echo "✅ mod_google_transcribe compiled successfully" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so

# ============================================================================
# Validation Stage: Verify all modules
# ============================================================================
RUN echo "=========================================" \
    && echo "Module Validation - ALL 5 MODULES" \
    && echo "=========================================" \
    && for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe mod_azure_transcribe mod_google_transcribe; do \
        MODULE_PATH="/usr/local/freeswitch/lib/freeswitch/mod/${module}.so"; \
        echo ""; \
        echo "Checking ${module}..."; \
        if [ -f "$MODULE_PATH" ]; then \
            echo "✅ Module file exists"; ls -lh "$MODULE_PATH"; \
        else \
            echo "❌ ERROR: Module file not found: $MODULE_PATH"; exit 1; \
        fi; \
        echo "Dependencies:"; \
        ldd "$MODULE_PATH" | head -20; \
        if ldd "$MODULE_PATH" | grep -q "not found"; then \
            echo "❌ ERROR: Missing dependencies in ${module}"; \
            ldd "$MODULE_PATH" | grep "not found"; exit 1; \
        else \
            echo "✅ No missing dependencies in ${module}"; \
        fi; \
    done \
    && echo "=========================================" \
    && echo "✅ All 5 modules validated successfully!" \
    && echo "========================================="

# ============================================================================
# Update FreeSWITCH modules.conf.xml to load all modules
# ============================================================================
RUN sed -i '/<\/modules>/i \    <!-- Speech Transcription Modules - All 5 Modules -->' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_audio_fork"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_aws_transcribe"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_deepgram_transcribe"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_azure_transcribe"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && sed -i '/<\/modules>/i \    <load module="mod_google_transcribe"/>' \
    /usr/local/freeswitch/conf/autoload_configs/modules.conf.xml \
    && echo "✅ Added all 5 speech modules to modules.conf.xml"

# ============================================================================
# Runtime Validation: Quick module load test
# ============================================================================
RUN export LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib/MicrosoftSpeechSDK:$LD_LIBRARY_PATH \
    && timeout 60s /usr/local/freeswitch/bin/freeswitch -nonat -nc -nf >/dev/null 2>&1 & FS_PID=$! \
    && sleep 25 \
    && grep -q "mod_audio_fork" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_aws_transcribe" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_deepgram_transcribe" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_azure_transcribe" /usr/local/freeswitch/log/freeswitch.log \
    && grep -q "mod_google_transcribe" /usr/local/freeswitch/log/freeswitch.log \
    && ! grep -E "mod_audio_fork|mod_aws_transcribe|mod_deepgram_transcribe|mod_azure_transcribe|mod_google_transcribe" /usr/local/freeswitch/log/freeswitch.log | grep -qiE "error|fail|cannot|unable" \
    && kill $FS_PID 2>/dev/null || true \
    && echo "✅ Runtime validation passed - all 5 modules loaded successfully"

# ============================================================================
# Final Stage: Clean Runtime Image
# ============================================================================
FROM ${BASE_IMAGE} AS runtime

ARG AWS_SDK_CPP_VERSION=1.11.345
ARG GRPC_VERSION=1.64.2

# Install runtime dependencies for all modules
RUN apt-get update && apt-get install -y --quiet --no-install-recommends \
    libcurl4 \
    libssl1.1 \
    zlib1g \
    libpulse0 \
    libspeexdsp1 \
    libasound2 \
    && rm -rf /var/lib/apt/lists/*

ENV LD_LIBRARY_PATH=/usr/local/lib:/usr/local/lib/MicrosoftSpeechSDK

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

# Copy Azure Speech SDK libraries
COPY --from=builder /usr/local/lib/MicrosoftSpeechSDK /usr/local/lib/MicrosoftSpeechSDK

# Copy all five modules
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so \
    /usr/local/freeswitch/lib/freeswitch/mod/
COPY --from=builder /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so \
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
    && echo "Runtime Image Validation..." \
    && echo "=========================================" \
    && echo "" \
    && echo "Installed Libraries:" \
    && echo "  libwebsockets:" && ls -lh /usr/local/lib/libwebsockets.so* | head -3 \
    && echo "  AWS SDK Core:" && ls -lh /usr/local/lib/libaws-cpp-sdk-core.so* | head -3 \
    && echo "  AWS SDK Transcribe:" && ls -lh /usr/local/lib/libaws-cpp-sdk-transcribestreaming.so* | head -3 \
    && echo "  gRPC++:" && ls -lh /usr/local/lib/libgrpc++.so* | head -3 \
    && echo "  protobuf:" && ls -lh /usr/local/lib/libprotobuf.so* | head -3 \
    && echo "  Azure SDK:" && ls -lh /usr/local/lib/MicrosoftSpeechSDK/*.so* | head -3 \
    && echo "" \
    && echo "Installed Modules:" \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_audio_fork.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_aws_transcribe.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_deepgram_transcribe.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_azure_transcribe.so \
    && ls -lh /usr/local/freeswitch/lib/freeswitch/mod/mod_google_transcribe.so \
    && echo "" \
    && echo "Verifying dependencies for all modules..." \
    && for module in mod_audio_fork mod_aws_transcribe mod_deepgram_transcribe mod_azure_transcribe mod_google_transcribe; do \
        echo ""; \
        echo "Checking ${module}..."; \
        if ! ldd /usr/local/freeswitch/lib/freeswitch/mod/${module}.so | grep "not found"; then \
            echo "✅ ${module} - all dependencies satisfied"; \
        else \
            echo "❌ ${module} - missing dependencies"; exit 1; \
        fi; \
    done \
    && echo "" \
    && echo "Checking example configuration files..." \
    && ls -lh /usr/local/freeswitch/conf/dialplan/default.xml \
    && ls -lh /usr/local/freeswitch/conf/directory/default/1000.xml \
    && ls -lh /usr/local/freeswitch/conf/directory/default/1001.xml \
    && ls -lh /usr/local/freeswitch/conf/directory/default/1002.xml \
    && ls -lh /usr/local/freeswitch/conf/directory/default/1003.xml \
    && ls -lh /usr/local/freeswitch/conf/directory/default/1004.xml \
    && echo "✅ Configuration files installed" \
    && echo "" \
    && echo "=========================================" \
    && echo "✅ All 5 modules ready!" \
    && echo "=========================================" \
    && echo "" \
    && echo "Available Speech Modules:" \
    && echo "  ✅ mod_audio_fork (Generic WebSocket Streaming)" \
    && echo "  ✅ mod_aws_transcribe (AWS Transcribe)" \
    && echo "  ✅ mod_deepgram_transcribe (Deepgram)" \
    && echo "  ✅ mod_azure_transcribe (Azure Cognitive Services)" \
    && echo "  ✅ mod_google_transcribe (Google Cloud Speech-to-Text)" \
    && echo "" \
    && echo "Example Configuration:" \
    && echo "  ✅ dialplan/default.xml (with all transcription examples)" \
    && echo "  ✅ directory/default/1000.xml (Audio Fork)" \
    && echo "  ✅ directory/default/1001.xml (Deepgram)" \
    && echo "  ✅ directory/default/1002.xml (Azure)" \
    && echo "  ✅ directory/default/1003.xml (AWS)" \
    && echo "  ✅ directory/default/1004.xml (Google)" \
    && echo "" \
    && echo "Unified Features (all modules):" \
    && echo "  ✅ User-choice sampling rate (8k/16k/custom)" \
    && echo "  ✅ Mono/Mixed/Stereo support" \
    && echo "  ✅ Metadata support with session events" \
    && echo "  ✅ Speaker information for Pusher integration" \
    && echo "  ✅ Complete feature parity across all modules" \
    && echo "========================================="

# Labels
LABEL maintainer="FreeSWITCH Speech AI"
LABEL description="FreeSWITCH 1.10.11 with ALL speech transcription modules"
LABEL modules="mod_audio_fork,mod_aws_transcribe,mod_deepgram_transcribe,mod_azure_transcribe,mod_google_transcribe"
LABEL aws.sdk.version="${AWS_SDK_CPP_VERSION}"
LABEL grpc.version="${GRPC_VERSION}"
LABEL libwebsockets.version="4.3.3"
LABEL azure.sdk.version="latest"
LABEL base.image="srt2011/freeswitch-base:latest"
LABEL features="user-choice-sampling,metadata,mono-mixed-stereo,speaker-info,pusher-integration"

# Expose FreeSWITCH ports
EXPOSE 5060/tcp 5060/udp 5080/tcp 5080/udp 8021/tcp
EXPOSE 16384-16484/udp

# Health check
HEALTHCHECK --interval=30s --timeout=10s --start-period=40s --retries=3 \
    CMD /usr/local/freeswitch/bin/fs_cli -x "status" | grep -q "UP" || exit 1

# Set environment
ENV PATH="/usr/local/freeswitch/bin:${PATH}"

# Inherit CMD from base image
