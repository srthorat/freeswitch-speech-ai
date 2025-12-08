#ifndef __AUDIO_PIPE_H__
#define __AUDIO_PIPE_H__

#include <switch.h>
#include <string>
#include <memory>
#include <functional>

#include <aws/core/Aws.h>
#include <aws/transcribestreaming/TranscribeStreamingServiceClient.h>

#include "transcript_data.h"

// Forward declaration
class AwsInternalPipe;

// Define the response handler callback type
typedef std::function<void(switch_core_session_t*, const transcript_data_t*, const char*)> ResponseHandler_t;

class AwsPipe : public std::enable_shared_from_this<AwsPipe> {
public:
    // Default constructor for pre-allocation in the object pool
    AwsPipe();
    
    // Initialize a recycled pipe object for a new session
    void init(switch_core_session_t* session, uint32_t sampleRate, uint32_t channels, const char* lang, bool interim, const char* bugname, const ResponseHandler_t& callback);

    ~AwsPipe();

    void connect();
    void process_audio();
    void close();
    
    // Producer methods for the glue layer
    bool reserveAudioSpace(size_t size, void** ptr1, size_t* len1, void** ptr2, size_t* len2);
    void commitAudioData(size_t size);

    uint32_t getId() const;
    uint32_t getSampleRate() const;
    uint32_t getChannels() const;

    void* get_node() { return m_node; }
    void set_node(void* node) { m_node = node; }
private:
    std::unique_ptr<AwsInternalPipe> m_pimpl;
    void* m_node;
};

#endif // __AUDIO_PIPE_H__
