/*
 * error_types.h - Structured Error Handling for Google Transcribe Module
 *
 * Provides structured error codes, error info structures, and error handler
 * callbacks for better debugging, monitoring, and error recovery.
 *
 * Design Goals:
 * - Machine-readable error codes for programmatic handling
 * - Human-readable messages for logging/debugging
 * - Detailed context (gRPC status, HTTP codes, etc.)
 * - Non-blocking error delivery via callbacks
 * - Metrics-friendly (error counts by category)
 */

#ifndef __ERROR_TYPES_H__
#define __ERROR_TYPES_H__

#include <string>
#include <functional>
#include <cstdint>
#include <atomic>
#include <chrono>

namespace google_transcribe {

/**
 * Error Category - High-level classification
 */
enum class ErrorCategory {
    NONE = 0,
    CONFIGURATION,      // Invalid config, missing credentials
    CONNECTION,         // Network, DNS, TLS failures
    AUTHENTICATION,     // Auth failures, expired tokens
    GRPC,               // gRPC-specific errors
    API,                // Google API errors (quota, invalid request)
    AUDIO,              // Audio format, encoding issues
    INTERNAL,           // Internal module errors
    TIMEOUT,            // Timeouts (connection, stream, response)
    RESOURCE,           // Memory, queue full, thread pool exhausted
    SHUTDOWN            // Clean shutdown (not an error)
};

/**
 * Error Code - Specific error identifiers
 */
enum class ErrorCode {
    // Success
    OK = 0,
    
    // Configuration errors (100-199)
    CONFIG_MISSING_PROJECT_ID = 100,
    CONFIG_MISSING_LOCATION = 101,
    CONFIG_INVALID_SAMPLE_RATE = 102,
    CONFIG_INVALID_CHANNEL_COUNT = 103,
    CONFIG_INVALID_LANGUAGE = 104,
    CONFIG_MISSING_CREDENTIALS = 105,
    
    // Connection errors (200-299)
    CONNECT_FAILED = 200,
    CONNECT_TIMEOUT = 201,
    CONNECT_DNS_FAILED = 202,
    CONNECT_TLS_FAILED = 203,
    CONNECT_REFUSED = 204,
    CONNECT_RESET = 205,
    
    // Authentication errors (300-399)
    AUTH_INVALID_CREDENTIALS = 300,
    AUTH_EXPIRED_TOKEN = 301,
    AUTH_PERMISSION_DENIED = 302,
    AUTH_PROJECT_NOT_FOUND = 303,
    
    // gRPC errors (400-499)
    GRPC_CANCELLED = 400,
    GRPC_UNKNOWN = 401,
    GRPC_DEADLINE_EXCEEDED = 402,
    GRPC_RESOURCE_EXHAUSTED = 403,
    GRPC_ABORTED = 404,
    GRPC_UNAVAILABLE = 405,
    GRPC_INTERNAL = 406,
    GRPC_UNIMPLEMENTED = 407,
    GRPC_DATA_LOSS = 408,
    
    // API errors (500-599)
    API_QUOTA_EXCEEDED = 500,
    API_RATE_LIMITED = 501,
    API_INVALID_REQUEST = 502,
    API_UNSUPPORTED_MODEL = 503,
    API_UNSUPPORTED_LANGUAGE = 504,
    API_AUDIO_TOO_LONG = 505,
    API_NO_SPEECH_DETECTED = 506,
    API_INVALID_RESOURCE = 507,      // "Invalid resource field value" - bad project/location
    API_RECOGNIZER_NOT_FOUND = 508,  // Recognizer path invalid
    
    // Audio errors (600-699)
    AUDIO_INVALID_FORMAT = 600,
    AUDIO_SAMPLE_RATE_MISMATCH = 601,
    AUDIO_CHANNEL_MISMATCH = 602,
    AUDIO_ENCODING_ERROR = 603,
    AUDIO_RESAMPLER_FAILED = 604,
    
    // Internal errors (700-799)
    INTERNAL_NULL_SESSION = 700,
    INTERNAL_INVALID_STATE = 701,
    INTERNAL_MEMORY_ALLOC = 702,
    INTERNAL_MUTEX_ERROR = 703,
    INTERNAL_UNEXPECTED = 799,
    
    // Timeout errors (800-899)
    TIMEOUT_CONNECTION = 800,
    TIMEOUT_STREAM = 801,
    TIMEOUT_RESPONSE = 802,
    TIMEOUT_WRITE = 803,
    
    // Resource errors (900-999)
    RESOURCE_QUEUE_FULL = 900,
    RESOURCE_MEMORY_EXHAUSTED = 901,
    RESOURCE_THREAD_POOL_EXHAUSTED = 902,
    RESOURCE_HANDLE_LIMIT = 903,
    
    // Pusher-specific errors (1000-1099)
    PUSHER_NOT_CONFIGURED = 1000,
    PUSHER_HTTP_ERROR = 1001,
    PUSHER_TIMEOUT = 1002,
    PUSHER_QUEUE_FULL = 1003,
    PUSHER_AUTH_FAILED = 1004,
    
    // Shutdown (not an error)
    SHUTDOWN_REQUESTED = 9999
};

/**
 * ErrorInfo - Detailed error information
 */
struct ErrorInfo {
    ErrorCode code = ErrorCode::OK;
    ErrorCategory category = ErrorCategory::NONE;
    std::string message;
    std::string details;
    
    // Context
    uint64_t session_id = 0;
    std::string fs_uuid;
    
    // gRPC context (if applicable)
    int grpc_status_code = 0;
    std::string grpc_error_message;
    std::string grpc_error_details;
    
    // HTTP context (if applicable)
    int http_status_code = 0;
    std::string http_response_body;
    
    // Timing
    std::chrono::system_clock::time_point timestamp;
    uint64_t latency_ms = 0;
    
    // Retry info
    int retry_count = 0;
    bool is_retryable = false;
    
    ErrorInfo() : timestamp(std::chrono::system_clock::now()) {}
    
    ErrorInfo(ErrorCode c, const std::string& msg)
        : code(c)
        , category(categorizeError(c))
        , message(msg)
        , timestamp(std::chrono::system_clock::now())
        , is_retryable(isRetryableError(c))
    {}
    
    ErrorInfo(ErrorCode c, const std::string& msg, const std::string& detail)
        : code(c)
        , category(categorizeError(c))
        , message(msg)
        , details(detail)
        , timestamp(std::chrono::system_clock::now())
        , is_retryable(isRetryableError(c))
    {}
    
    bool isError() const { return code != ErrorCode::OK; }
    bool isSuccess() const { return code == ErrorCode::OK; }
    
    // Get error code as integer
    int codeAsInt() const { return static_cast<int>(code); }
    
    // Get category name
    const char* categoryName() const {
        switch (category) {
            case ErrorCategory::NONE: return "none";
            case ErrorCategory::CONFIGURATION: return "configuration";
            case ErrorCategory::CONNECTION: return "connection";
            case ErrorCategory::AUTHENTICATION: return "authentication";
            case ErrorCategory::GRPC: return "grpc";
            case ErrorCategory::API: return "api";
            case ErrorCategory::AUDIO: return "audio";
            case ErrorCategory::INTERNAL: return "internal";
            case ErrorCategory::TIMEOUT: return "timeout";
            case ErrorCategory::RESOURCE: return "resource";
            case ErrorCategory::SHUTDOWN: return "shutdown";
            default: return "unknown";
        }
    }
    
    // Convert to JSON string for logging/events
    std::string toJson() const {
        char buf[2048];
        snprintf(buf, sizeof(buf),
            "{\"error_code\":%d,\"category\":\"%s\",\"message\":\"%s\","
            "\"session_id\":%lu,\"retryable\":%s,\"retry_count\":%d}",
            static_cast<int>(code),
            categoryName(),
            message.c_str(),
            session_id,
            is_retryable ? "true" : "false",
            retry_count);
        return std::string(buf);
    }
    
private:
    static ErrorCategory categorizeError(ErrorCode code) {
        int c = static_cast<int>(code);
        if (c == 0) return ErrorCategory::NONE;
        if (c >= 100 && c < 200) return ErrorCategory::CONFIGURATION;
        if (c >= 200 && c < 300) return ErrorCategory::CONNECTION;
        if (c >= 300 && c < 400) return ErrorCategory::AUTHENTICATION;
        if (c >= 400 && c < 500) return ErrorCategory::GRPC;
        if (c >= 500 && c < 600) return ErrorCategory::API;
        if (c >= 600 && c < 700) return ErrorCategory::AUDIO;
        if (c >= 700 && c < 800) return ErrorCategory::INTERNAL;
        if (c >= 800 && c < 900) return ErrorCategory::TIMEOUT;
        if (c >= 900 && c < 1000) return ErrorCategory::RESOURCE;
        if (c >= 9000) return ErrorCategory::SHUTDOWN;
        return ErrorCategory::INTERNAL;
    }
    
    static bool isRetryableError(ErrorCode code) {
        switch (code) {
            // Retryable errors
            case ErrorCode::CONNECT_TIMEOUT:
            case ErrorCode::CONNECT_RESET:
            case ErrorCode::GRPC_UNAVAILABLE:
            case ErrorCode::GRPC_RESOURCE_EXHAUSTED:
            case ErrorCode::GRPC_ABORTED:
            case ErrorCode::API_RATE_LIMITED:
            case ErrorCode::TIMEOUT_CONNECTION:
            case ErrorCode::TIMEOUT_STREAM:
            case ErrorCode::PUSHER_TIMEOUT:
            case ErrorCode::PUSHER_HTTP_ERROR:
                return true;
            default:
                return false;
        }
    }
};

/**
 * Error handler callback type
 * Called when errors occur (on worker thread, should be non-blocking)
 */
using ErrorHandler = std::function<void(const ErrorInfo& error)>;

/**
 * ErrorMetrics - Thread-safe error statistics
 */
class ErrorMetrics {
public:
    void recordError(ErrorCategory category) {
        switch (category) {
            case ErrorCategory::CONFIGURATION: m_config_errors++; break;
            case ErrorCategory::CONNECTION: m_connection_errors++; break;
            case ErrorCategory::AUTHENTICATION: m_auth_errors++; break;
            case ErrorCategory::GRPC: m_grpc_errors++; break;
            case ErrorCategory::API: m_api_errors++; break;
            case ErrorCategory::AUDIO: m_audio_errors++; break;
            case ErrorCategory::INTERNAL: m_internal_errors++; break;
            case ErrorCategory::TIMEOUT: m_timeout_errors++; break;
            case ErrorCategory::RESOURCE: m_resource_errors++; break;
            default: break;
        }
        m_total_errors++;
    }
    
    void recordSuccess() { m_total_success++; }
    void recordRetry() { m_total_retries++; }
    
    // Getters
    uint64_t totalErrors() const { return m_total_errors.load(); }
    uint64_t totalSuccess() const { return m_total_success.load(); }
    uint64_t totalRetries() const { return m_total_retries.load(); }
    
    uint64_t configErrors() const { return m_config_errors.load(); }
    uint64_t connectionErrors() const { return m_connection_errors.load(); }
    uint64_t authErrors() const { return m_auth_errors.load(); }
    uint64_t grpcErrors() const { return m_grpc_errors.load(); }
    uint64_t apiErrors() const { return m_api_errors.load(); }
    uint64_t audioErrors() const { return m_audio_errors.load(); }
    uint64_t internalErrors() const { return m_internal_errors.load(); }
    uint64_t timeoutErrors() const { return m_timeout_errors.load(); }
    uint64_t resourceErrors() const { return m_resource_errors.load(); }
    
    // Error rate (errors / total operations)
    double errorRate() const {
        uint64_t total = m_total_errors.load() + m_total_success.load();
        if (total == 0) return 0.0;
        return static_cast<double>(m_total_errors.load()) / total;
    }
    
    // Reset metrics
    void reset() {
        m_total_errors = 0;
        m_total_success = 0;
        m_total_retries = 0;
        m_config_errors = 0;
        m_connection_errors = 0;
        m_auth_errors = 0;
        m_grpc_errors = 0;
        m_api_errors = 0;
        m_audio_errors = 0;
        m_internal_errors = 0;
        m_timeout_errors = 0;
        m_resource_errors = 0;
    }
    
    // Get stats as JSON
    std::string toJson() const {
        char buf[1024];
        snprintf(buf, sizeof(buf),
            "{\"total_errors\":%lu,\"total_success\":%lu,\"total_retries\":%lu,"
            "\"error_rate\":%.4f,\"by_category\":{\"config\":%lu,\"connection\":%lu,"
            "\"auth\":%lu,\"grpc\":%lu,\"api\":%lu,\"audio\":%lu,\"internal\":%lu,"
            "\"timeout\":%lu,\"resource\":%lu}}",
            m_total_errors.load(), m_total_success.load(), m_total_retries.load(),
            errorRate(),
            m_config_errors.load(), m_connection_errors.load(),
            m_auth_errors.load(), m_grpc_errors.load(), m_api_errors.load(),
            m_audio_errors.load(), m_internal_errors.load(),
            m_timeout_errors.load(), m_resource_errors.load());
        return std::string(buf);
    }
    
private:
    std::atomic<uint64_t> m_total_errors{0};
    std::atomic<uint64_t> m_total_success{0};
    std::atomic<uint64_t> m_total_retries{0};
    
    std::atomic<uint64_t> m_config_errors{0};
    std::atomic<uint64_t> m_connection_errors{0};
    std::atomic<uint64_t> m_auth_errors{0};
    std::atomic<uint64_t> m_grpc_errors{0};
    std::atomic<uint64_t> m_api_errors{0};
    std::atomic<uint64_t> m_audio_errors{0};
    std::atomic<uint64_t> m_internal_errors{0};
    std::atomic<uint64_t> m_timeout_errors{0};
    std::atomic<uint64_t> m_resource_errors{0};
};

/**
 * Helper to convert gRPC status codes to our error codes
 */
inline ErrorCode grpcStatusToErrorCode(int grpc_code) {
    switch (grpc_code) {
        case 0:  return ErrorCode::OK;                    // OK
        case 1:  return ErrorCode::GRPC_CANCELLED;        // CANCELLED
        case 2:  return ErrorCode::GRPC_UNKNOWN;          // UNKNOWN
        case 3:  return ErrorCode::API_INVALID_REQUEST;   // INVALID_ARGUMENT
        case 4:  return ErrorCode::GRPC_DEADLINE_EXCEEDED; // DEADLINE_EXCEEDED
        case 5:  return ErrorCode::API_UNSUPPORTED_MODEL; // NOT_FOUND
        case 6:  return ErrorCode::API_INVALID_REQUEST;   // ALREADY_EXISTS
        case 7:  return ErrorCode::AUTH_PERMISSION_DENIED; // PERMISSION_DENIED
        case 8:  return ErrorCode::GRPC_RESOURCE_EXHAUSTED; // RESOURCE_EXHAUSTED
        case 9:  return ErrorCode::API_INVALID_REQUEST;   // FAILED_PRECONDITION
        case 10: return ErrorCode::GRPC_ABORTED;          // ABORTED
        case 11: return ErrorCode::API_INVALID_REQUEST;   // OUT_OF_RANGE
        case 12: return ErrorCode::GRPC_UNIMPLEMENTED;    // UNIMPLEMENTED
        case 13: return ErrorCode::GRPC_INTERNAL;         // INTERNAL
        case 14: return ErrorCode::GRPC_UNAVAILABLE;      // UNAVAILABLE
        case 15: return ErrorCode::GRPC_DATA_LOSS;        // DATA_LOSS
        case 16: return ErrorCode::AUTH_INVALID_CREDENTIALS; // UNAUTHENTICATED
        default: return ErrorCode::GRPC_UNKNOWN;
    }
}

/**
 * Helper to convert HTTP status codes to our error codes
 */
inline ErrorCode httpStatusToErrorCode(int http_code) {
    if (http_code >= 200 && http_code < 300) return ErrorCode::OK;
    if (http_code == 400) return ErrorCode::API_INVALID_REQUEST;
    if (http_code == 401) return ErrorCode::AUTH_INVALID_CREDENTIALS;
    if (http_code == 403) return ErrorCode::AUTH_PERMISSION_DENIED;
    if (http_code == 404) return ErrorCode::API_UNSUPPORTED_MODEL;
    if (http_code == 429) return ErrorCode::API_RATE_LIMITED;
    if (http_code >= 500) return ErrorCode::GRPC_UNAVAILABLE;
    return ErrorCode::PUSHER_HTTP_ERROR;
}

} // namespace google_transcribe

#endif // __ERROR_TYPES_H__
