#ifndef __TRANSCRIPT_DATA_H__
#define __TRANSCRIPT_DATA_H__

#include <stdbool.h>

typedef struct {
    // Flags
    bool is_final;
    bool speech_final;
    bool has_transcript;
    bool is_connection_event; // Special flag for connection_success events
    
    // Channel/speaker info
    int channel_index;
    
    // Transcript content
    char transcript[8192];
    double confidence;
    
    // Timing
    double start_time;
    double end_time;
    
    // Raw JSON (for passthrough to FreeSWITCH event)
    const char* raw_json;

} transcript_data_t;

#endif // __TRANSCRIPT_DATA_H__
