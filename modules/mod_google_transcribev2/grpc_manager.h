/*
 * grpc_manager.h - Singleton gRPC Manager for High-Concurrency Transcription
 *
 * This class manages a shared CompletionQueue and worker thread pool to handle
 * gRPC I/O for thousands of concurrent transcription sessions efficiently.
 *
 * Architecture:
 * - Single CompletionQueue shared by all sessions
 * - Fixed pool of worker threads (typically 4-8, based on CPU cores)
 * - Each session is a state machine driven by CompletionQueue events
 * - No per-session threads - scales to 2000+ concurrent calls
 *
 * Thread Safety:
 * - CompletionQueue is thread-safe for Next() calls
 * - Session registration uses mutex protection
 * - Worker threads process events for any session
 */

#ifndef __GRPC_MANAGER_H__
#define __GRPC_MANAGER_H__

#include <grpc++/grpc++.h>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <functional>

#include "error_types.h"

namespace google_transcribe {

// Forward declaration
class GoogleStreamerSession;

/**
 * GrpcManager - Singleton managing all gRPC I/O
 *
 * Usage:
 *   GrpcManager::getInstance().start();  // On module load
 *   GrpcManager::getInstance().shutdown(); // On module unload
 *
 * Sessions register themselves and receive callbacks via the CompletionQueue.
 */
class GrpcManager {
public:
    /**
     * Get the singleton instance
     */
    static GrpcManager& getInstance();

    /**
     * Start the worker thread pool
     * Call this once when the FreeSWITCH module loads
     *
     * @param num_workers Number of worker threads (0 = auto-detect based on CPU cores)
     */
    void start(unsigned int num_workers = 0);

    /**
     * Shutdown the manager and wait for all workers to complete
     * Call this when the FreeSWITCH module unloads
     */
    void shutdown();

    /**
     * Check if the manager is running
     */
    bool isRunning() const { return m_running.load(); }

    /**
     * Get the CompletionQueue for async operations
     * Sessions use this to register their async calls
     */
    grpc::CompletionQueue* getCompletionQueue() { return &m_cq; }

    /**
     * Register a session for tracking
     * Used for debugging and graceful shutdown
     */
    void registerSession(uint64_t session_id, std::shared_ptr<GoogleStreamerSession> session);

    /**
     * Unregister a session
     */
    void unregisterSession(uint64_t session_id);

    /**
     * Get current session count (for monitoring)
     */
    size_t getSessionCount() const;

    /**
     * Get worker thread count
     */
    size_t getWorkerCount() const { return m_workers.size(); }
    
    /**
     * Get global error metrics (aggregated across all sessions)
     */
    const ErrorMetrics& getGlobalMetrics() const { return m_global_metrics; }
    
    /**
     * Record an error in global metrics
     */
    void recordError(ErrorCategory category) {
        m_global_metrics.recordError(category);
    }
    
    /**
     * Record a success in global metrics
     */
    void recordSuccess() {
        m_global_metrics.recordSuccess();
    }

    // Delete copy and move constructors
    GrpcManager(const GrpcManager&) = delete;
    GrpcManager& operator=(const GrpcManager&) = delete;
    GrpcManager(GrpcManager&&) = delete;
    GrpcManager& operator=(GrpcManager&&) = delete;

private:
    GrpcManager();
    ~GrpcManager();

    /**
     * Worker thread function
     * Processes CompletionQueue events and dispatches to session state machines
     */
    void workerLoop(int worker_id);

    grpc::CompletionQueue m_cq;
    std::vector<std::thread> m_workers;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_shutdown_requested{false};

    // Session tracking for debugging and graceful shutdown
    mutable std::mutex m_sessions_mutex;
    std::unordered_map<uint64_t, std::weak_ptr<GoogleStreamerSession>> m_sessions;

    // Stats for monitoring
    std::atomic<uint64_t> m_total_events_processed{0};
    std::atomic<uint64_t> m_total_sessions_created{0};
    
    // Global error metrics
    ErrorMetrics m_global_metrics;
};

/**
 * Base class for async operation tags
 * Each async gRPC operation needs a tag to identify it in the CompletionQueue
 */
class AsyncOperationTag {
public:
    enum class Type {
        CONNECT,
        WRITE,
        READ,
        WRITES_DONE,
        FINISH
    };

    AsyncOperationTag(Type type, std::shared_ptr<GoogleStreamerSession> session)
        : m_type(type), m_session(session) {}

    virtual ~AsyncOperationTag() = default;

    Type getType() const { return m_type; }
    std::shared_ptr<GoogleStreamerSession> getSession() const { return m_session.lock(); }

    /**
     * Called by the worker thread when this operation completes
     * @param ok true if the operation completed successfully
     */
    virtual void proceed(bool ok) = 0;

protected:
    Type m_type;
    std::weak_ptr<GoogleStreamerSession> m_session;
};

/**
 * Tag for connection establishment
 */
class ConnectTag : public AsyncOperationTag {
public:
    ConnectTag(std::shared_ptr<GoogleStreamerSession> session)
        : AsyncOperationTag(Type::CONNECT, session) {}
    void proceed(bool ok) override;
};

/**
 * Tag for write operations
 */
class WriteTag : public AsyncOperationTag {
public:
    WriteTag(std::shared_ptr<GoogleStreamerSession> session)
        : AsyncOperationTag(Type::WRITE, session) {}
    void proceed(bool ok) override;
};

/**
 * Tag for read operations
 */
class ReadTag : public AsyncOperationTag {
public:
    ReadTag(std::shared_ptr<GoogleStreamerSession> session)
        : AsyncOperationTag(Type::READ, session) {}
    void proceed(bool ok) override;
};

/**
 * Tag for WritesDone operation
 */
class WritesDoneTag : public AsyncOperationTag {
public:
    WritesDoneTag(std::shared_ptr<GoogleStreamerSession> session)
        : AsyncOperationTag(Type::WRITES_DONE, session) {}
    void proceed(bool ok) override;
};

/**
 * Tag for stream finish
 */
class FinishTag : public AsyncOperationTag {
public:
    FinishTag(std::shared_ptr<GoogleStreamerSession> session)
        : AsyncOperationTag(Type::FINISH, session) {}
    void proceed(bool ok) override;
};

} // namespace google_transcribe

#endif // __GRPC_MANAGER_H__
