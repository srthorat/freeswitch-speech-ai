# Makefile for mod_google_transcribe with Google Speech v2 API support

# Variables
CXX = g++
CC = gcc
CXXFLAGS = -fPIC -std=c++17 -I/usr/local/freeswitch/include/freeswitch -I/usr/local/include
CFLAGS = -fPIC -I/usr/local/freeswitch/include/freeswitch
LDFLAGS = -shared -L/usr/local/lib -Wl,-rpath,/usr/local/lib -Wl,--no-as-needed -lprotobuf -lgrpc++ -lgrpc -lcurl -lcjson -lssl -lcrypto

# Source files
PROTO_SRC = speech.pb.cc speech.grpc.pb.cc
CPP_SRC = google_glue.cpp
C_SRC = mod_google_transcribe.c
OBJECTS = $(PROTO_SRC:.cc=.o) $(CPP_SRC:.cpp=.o) $(C_SRC:.c=.o)

# Target
TARGET = mod_google_transcribe.so

# Default target
all: $(TARGET)

# Generate protobuf files from .proto
%.pb.cc %.pb.h: %.proto
	protoc --cpp_out=. --grpc_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` $<

# Compile protobuf sources
speech.pb.o: speech.pb.cc speech.pb.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

speech.grpc.pb.o: speech.grpc.pb.cc speech.grpc.pb.h speech.pb.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile C++ sources
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile C sources
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Link everything together
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -Wl,--export-dynamic -o $@ $^

# Install target
install: $(TARGET)
	cp $(TARGET) /usr/local/freeswitch/lib/freeswitch/mod/

# Clean target
clean:
	rm -f *.o $(TARGET) speech.pb.cc speech.pb.h speech.grpc.pb.cc speech.grpc.pb.h

# Force regeneration of protobuf files
proto-clean:
	rm -f speech.pb.cc speech.pb.h speech.grpc.pb.cc speech.grpc.pb.h

# Rebuild everything
rebuild: clean all

.PHONY: all install clean proto-clean rebuild