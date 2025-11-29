# Makefile for mod_google_transcribev2
# Google Speech-to-Text v2 module for FreeSWITCH
#
# High-Performance Async Architecture (v2)
# - Supports 2000+ concurrent calls
# - Async gRPC with CompletionQueue
# - Lock-free audio queues
# - Non-blocking Pusher delivery

FS_PREFIX ?= /usr/local/freeswitch
CXX = g++
CC = gcc

# Compiler flags
CFLAGS = -fPIC -I${FS_PREFIX}/include/freeswitch -Wall -Werror
CXXFLAGS = -fPIC -std=c++17 -I${FS_PREFIX}/include/freeswitch -Wall -Werror -Wno-unused-variable

# Find gRPC and protobuf using pkg-config
GRPC_CFLAGS = $(shell pkg-config --cflags grpc++ protobuf 2>/dev/null || echo "-I/usr/local/include")
GRPC_LIBS = $(shell pkg-config --libs grpc++ protobuf 2>/dev/null || echo "-L/usr/local/lib -lgrpc++ -lgrpc -lprotobuf -pthread")

CXXFLAGS += $(GRPC_CFLAGS)

# Linker flags
LDFLAGS = -shared -Wl,-rpath,/usr/local/lib -lcurl -lssl -lcrypto -lspeex -lspeexdsp $(GRPC_LIBS)

# Source files - C
SOURCES_C = mod_google_transcribev2.c

# Source files - C++ (new async architecture)
SOURCES_CPP = \
	google_transcribe_glue.cpp \
	grpc_manager.cpp \
	async_pusher_client.cpp \
	audio_queue.cpp \
	google_streamer_session.cpp

# Proto sources (generated)
PROTO_SOURCES = speech.grpc.pb.cc speech.pb.cc
PROTO_OBJECTS = $(PROTO_SOURCES:.cc=.o)

# All objects
OBJECTS = $(SOURCES_C:.c=.o) $(SOURCES_CPP:.cpp=.o) $(PROTO_OBJECTS)

# Target module
TARGET = mod_google_transcribev2.so

.PHONY: all clean install proto

all: proto $(TARGET)

proto: speech.pb.h speech.grpc.pb.h

speech.pb.h speech.pb.cc: speech.proto
	@echo "Generating protobuf files..."
	protoc --cpp_out=. speech.proto

speech.grpc.pb.h speech.grpc.pb.cc: speech.proto
	@echo "Generating gRPC files..."
	protoc --grpc_out=. --plugin=protoc-gen-grpc=$(shell which grpc_cpp_plugin) speech.proto

$(TARGET): $(OBJECTS)
	@echo "Linking $(TARGET)..."
	$(CXX) -o $@ $^ $(LDFLAGS)
	@echo "Build successful!"

%.o: %.c
	@echo "Compiling $<..."
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.cc
	@echo "Compiling $<..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

install: $(TARGET)
	@echo "Installing $(TARGET) to ${FS_PREFIX}/lib/freeswitch/mod/"
	install -d ${FS_PREFIX}/lib/freeswitch/mod
	install -m 755 $(TARGET) ${FS_PREFIX}/lib/freeswitch/mod/
	@echo "Installation complete!"

clean:
	@echo "Cleaning..."
	rm -f *.o $(TARGET) speech.pb.h speech.pb.cc speech.grpc.pb.h speech.grpc.pb.cc
	@echo "Clean complete!"

# Help target
help:
	@echo "mod_google_transcribev2 Makefile (High-Performance Async v2)"
	@echo ""
	@echo "Architecture:"
	@echo "  - GrpcManager: Shared CompletionQueue + worker thread pool"
	@echo "  - AsyncPusherClient: Non-blocking HTTP delivery"
	@echo "  - AudioQueue: Lock-free SPSC queue"
	@echo "  - GoogleStreamerSession: Async state machine per session"
	@echo ""
	@echo "Scalability:"
	@echo "  - Old version: 1 thread per session (2000 sessions = 2000+ threads)"
	@echo "  - New version: Fixed thread pool (~8-16 threads for 2000+ sessions)"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build the module (default)"
	@echo "  install  - Install the module to FreeSWITCH"
	@echo "  clean    - Remove build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  FS_PREFIX - FreeSWITCH installation directory (default: /usr/local/freeswitch)"
	@echo ""
	@echo "Example:"
	@echo "  make"
	@echo "  sudo make install"
	@echo "  make FS_PREFIX=/opt/freeswitch"

# Dependencies
google_transcribe_glue.o: google_transcribe_glue.cpp google_streamer_session.h grpc_manager.h async_pusher_client.h audio_queue.h mod_google_transcribev2.h
grpc_manager.o: grpc_manager.cpp grpc_manager.h google_streamer_session.h
async_pusher_client.o: async_pusher_client.cpp async_pusher_client.h
audio_queue.o: audio_queue.cpp audio_queue.h
google_streamer_session.o: google_streamer_session.cpp google_streamer_session.h grpc_manager.h async_pusher_client.h audio_queue.h speech.pb.h speech.grpc.pb.h
mod_google_transcribev2.o: mod_google_transcribev2.c mod_google_transcribev2.h
