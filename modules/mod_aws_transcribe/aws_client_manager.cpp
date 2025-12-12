#include <cstdlib>
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
    
    const char* env_max_conn = std::getenv("AWS_MAX_CONNECTIONS");
    int max_conn = env_max_conn ? std::atoi(env_max_conn) : 25;
    config.maxConnections = max_conn > 0 ? max_conn : 25; // Minimum 1, fallback to default 25 if 0/invalid
    
    const char* env_req_timeout = std::getenv("AWS_REQUEST_TIMEOUT_MS");
    int req_timeout = env_req_timeout ? std::atoi(env_req_timeout) : 30000;
    config.requestTimeoutMs = req_timeout > 100 ? req_timeout : 30000; // Safe minimum 100ms
    
    const char* env_conn_timeout = std::getenv("AWS_CONNECT_TIMEOUT_MS");
    int conn_timeout = env_conn_timeout ? std::atoi(env_conn_timeout) : 5000;
    config.connectTimeoutMs = conn_timeout > 100 ? conn_timeout : 5000; // Safe minimum 100ms
    
    t_client = std::make_shared<Aws::TranscribeStreamingService::TranscribeStreamingServiceClient>(config);
    
    // Track client count for monitoring
    g_client_count.fetch_add(1, std::memory_order_relaxed);
    
    return t_client;
}

uint64_t AwsClientManager::getClientCount() {
    return g_client_count.load(std::memory_order_relaxed);
}
