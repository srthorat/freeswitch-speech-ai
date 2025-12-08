#include "aws_client_manager.h"
#include <aws/core/Aws.h>
#include <aws/core/client/ClientConfiguration.h>
#include <aws/transcribestreaming/TranscribeStreamingServiceClient.h>
#include <atomic>

AwsClientManager g_client_manager;

// Thread-local storage for zero-contention client access
thread_local std::shared_ptr<Aws::TranscribeStreamingService::TranscribeStreamingServiceClient> t_client;
static std::atomic<uint64_t> g_client_count{0};

std::shared_ptr<Aws::TranscribeStreamingService::TranscribeStreamingServiceClient> AwsClientManager::getClient() {
    // Fast path: return existing thread-local client
    if (t_client) {
        return t_client;
    }
    
    // Slow path: create new client for this thread
    Aws::Client::ClientConfiguration config;
    config.maxConnections = 25; // Optimize for high throughput
    config.requestTimeoutMs = 30000;
    config.connectTimeoutMs = 5000;
    
    t_client = std::make_shared<Aws::TranscribeStreamingService::TranscribeStreamingServiceClient>(config);
    
    // Track client count for monitoring
    g_client_count.fetch_add(1, std::memory_order_relaxed);
    
    return t_client;
}

uint64_t AwsClientManager::getClientCount() {
    return g_client_count.load(std::memory_order_relaxed);
}
