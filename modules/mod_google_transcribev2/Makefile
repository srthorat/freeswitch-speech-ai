# Makefile for mod_google_transcribev2
# Google Speech-to-Text v2 module for FreeSWITCH

FS_PREFIX ?= /usr/local/freeswitch
CXX = g++
CC = gcc

# Compiler flags
CFLAGS = -fPIC -I${FS_PREFIX}/include/freeswitch -Wall -Werror
CXXFLAGS = -fPIC -std=c++17 -I${FS_PREFIX}/include/freeswitch -Wall -Werror

# Find Google Cloud Speech library using pkg-config
GOOGLE_CFLAGS = $(shell pkg-config --cflags google_cloud_cpp_speech 2>/dev/null || echo "-I/usr/local/include")
GOOGLE_LIBS = $(shell pkg-config --libs google_cloud_cpp_speech 2>/dev/null || echo "-lgoogle_cloud_cpp_speech -lgrpc++ -lgrpc -lprotobuf")

CXXFLAGS += $(GOOGLE_CFLAGS)

# Linker flags
LDFLAGS = -shared -lcurl -lssl -lcrypto -lspeex -lspeexdsp $(GOOGLE_LIBS)

# Source files
SOURCES_C = mod_google_transcribev2.c
SOURCES_CPP = google_transcribe_glue.cpp
OBJECTS = $(SOURCES_C:.c=.o) $(SOURCES_CPP:.cpp=.o)

# Target module
TARGET = mod_google_transcribev2.so

.PHONY: all clean install

all: $(TARGET)

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

install: $(TARGET)
	@echo "Installing $(TARGET) to ${FS_PREFIX}/lib/freeswitch/mod/"
	install -d ${FS_PREFIX}/lib/freeswitch/mod
	install -m 755 $(TARGET) ${FS_PREFIX}/lib/freeswitch/mod/
	@echo "Installation complete!"

clean:
	@echo "Cleaning..."
	rm -f $(OBJECTS) $(TARGET)
	@echo "Clean complete!"

# Help target
help:
	@echo "mod_google_transcribev2 Makefile"
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
