#ifndef __AWS_CLIENT_MANAGER_H__
#define __AWS_CLIENT_MANAGER_H__

#include <memory>
#include <cstdint>

// Forward declare to avoid including heavy AWS headers here
namespace Aws {
namespace TranscribeStreamingService {
    class TranscribeStreamingServiceClient;
}
}

/**
 * High-Performance AWS Client Manager with Zero Contention
 * 
 * Uses thread_local storage to eliminate mutex contention that was destroying
 * the lock-free architecture. Each thread gets its own client instance.
 */
class AwsClientManager {
public:
    // Zero-contention client access (lock-free)
    std::shared_ptr<Aws::TranscribeStreamingService::TranscribeStreamingServiceClient> getClient();
    
    // Monitoring support
    uint64_t getClientCount();
};

extern AwsClientManager g_client_manager;

#endif // __AWS_CLIENT_MANAGER_H__
