include $(top_srcdir)/build/modmake.rulesam
MODNAME=mod_aws_transcribe

mod_LTLIBRARIES = mod_aws_transcribe.la
mod_aws_transcribe_la_SOURCES  = mod_aws_transcribe.cpp aws_transcribe_glue.cpp audio_pipe.cpp worker_thread.cpp aws_client_manager.cpp async_http.cpp async_pusher.cpp
mod_aws_transcribe_la_CFLAGS   = $(AM_CFLAGS)
mod_aws_transcribe_la_CXXFLAGS = $(AM_CXXFLAGS) -std=c++17 -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-core/include -I${switch_srcdir}/libs/aws-sdk-cpp/aws-cpp-sdk-transcribestreaming/include -I${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/include

mod_aws_transcribe_la_LIBADD   = $(switch_builddir)/libfreeswitch.la
mod_aws_transcribe_la_LDFLAGS  = -avoid-version -module -no-undefined -L${switch_srcdir}/libs/aws-sdk-cpp/build/.deps/install/lib -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-core -L${switch_srcdir}/libs/aws-sdk-cpp/build/aws-cpp-sdk-transcribestreaming -laws-cpp-sdk-transcribestreaming -laws-cpp-sdk-core -laws-c-event-stream -laws-checksums -laws-c-common -lpthread -lcurl -lcrypto -lssl -lz \
            mod_aws_transcribe.o aws_transcribe_glue.o audio_pipe.o worker_thread.o aws_client_manager.o async_http.o async_pusher.o

# Unit test for the lock-free ring buffer
check_PROGRAMS = test_ring_buffer
test_ring_buffer_SOURCES = test_ring_buffer.cpp
test_ring_buffer_CXXFLAGS = $(AM_CXXFLAGS) -std=c++17
test_ring_buffer_LDFLAGS = -pthread

# Unit test for the memory pool
check_PROGRAMS += test_memory_pool
test_memory_pool_SOURCES = test_memory_pool.cpp
test_memory_pool_CXXFLAGS = $(AM_CXXFLAGS) -std=c++17
test_memory_pool_LDFLAGS = -pthread