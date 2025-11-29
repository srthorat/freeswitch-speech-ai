/*
 * grpc_manager.cpp - Implementation of GrpcManager singleton
 *
 * Provides efficient handling of gRPC I/O for thousands of concurrent
 * transcription sessions using a shared CompletionQueue and worker pool.
 */

#include "grpc_manager.h"
#include "google_streamer_session.h"
#include <switch.h>
#include <algorithm>

namespace google_transcribe {

// Singleton instance
GrpcManager& GrpcManager::getInstance() {
    static GrpcManager instance;
    return instance;
}

GrpcManager::GrpcManager() {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "GrpcManager: Instance created\n");
}

GrpcManager::~GrpcManager() {
    if (m_running.load()) {
        shutdown();
    }
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "GrpcManager: Instance destroyed, processed %lu total events\n",
        m_total_events_processed.load());
}

void GrpcManager::start(unsigned int num_workers) {
    if (m_running.load()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "GrpcManager: Already running\n");
        return;
    }

    // Auto-detect number of workers if not specified
    if (num_workers == 0) {
        num_workers = std::thread::hardware_concurrency();
        if (num_workers == 0) {
            num_workers = 4;  // Fallback
        }
        // Use at least 4 workers, at most 16 for efficiency
        num_workers = std::max(4u, std::min(num_workers, 16u));
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "GrpcManager: Starting with %u worker threads\n", num_workers);

    m_running.store(true);
    m_shutdown_requested.store(false);

    // Create worker threads
    for (unsigned int i = 0; i < num_workers; i++) {
        m_workers.emplace_back(&GrpcManager::workerLoop, this, i);
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "GrpcManager: Started successfully\n");
}

void GrpcManager::shutdown() {
    if (!m_running.load()) {
        return;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "GrpcManager: Initiating shutdown, %zu active sessions\n",
        getSessionCount());

    m_shutdown_requested.store(true);

    // Shutdown the CompletionQueue - this will cause all Next() calls to return
    m_cq.Shutdown();

    // Wait for all workers to finish
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    m_running.store(false);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "GrpcManager: Shutdown complete, processed %lu total events, %lu total sessions\n",
        m_total_events_processed.load(), m_total_sessions_created.load());
}

void GrpcManager::registerSession(uint64_t session_id, std::shared_ptr<GoogleStreamerSession> session) {
    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    m_sessions[session_id] = session;
    m_total_sessions_created++;

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "GrpcManager: Registered session %lu, total active: %zu\n",
        session_id, m_sessions.size());
}

void GrpcManager::unregisterSession(uint64_t session_id) {
    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    m_sessions.erase(session_id);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "GrpcManager: Unregistered session %lu, total active: %zu\n",
        session_id, m_sessions.size());
}

size_t GrpcManager::getSessionCount() const {
    std::lock_guard<std::mutex> lock(m_sessions_mutex);
    // Clean up expired weak_ptrs and count valid ones
    size_t count = 0;
    for (const auto& pair : m_sessions) {
        if (!pair.second.expired()) {
            count++;
        }
    }
    return count;
}

void GrpcManager::workerLoop(int worker_id) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "GrpcManager: Worker %d started\n", worker_id);

    void* tag;
    bool ok;

    while (true) {
        // Block until an event is available or shutdown
        bool got_event = m_cq.Next(&tag, &ok);

        if (!got_event) {
            // CompletionQueue has been shut down
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                "GrpcManager: Worker %d received shutdown signal\n", worker_id);
            break;
        }

        m_total_events_processed++;

        if (tag == nullptr) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "GrpcManager: Worker %d received null tag\n", worker_id);
            continue;
        }

        // Cast tag to our operation type and process
        AsyncOperationTag* op_tag = static_cast<AsyncOperationTag*>(tag);

        try {
            op_tag->proceed(ok);
        } catch (const std::exception& e) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "GrpcManager: Worker %d caught exception: %s\n", worker_id, e.what());
        }

        // The tag is owned by the session and will be cleaned up appropriately
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "GrpcManager: Worker %d stopped\n", worker_id);
}

// Tag implementations - these forward to the session's state machine

void ConnectTag::proceed(bool ok) {
    auto session = getSession();
    if (session) {
        session->onConnectComplete(ok);
    }
    // Tag will be deleted by session
}

void WriteTag::proceed(bool ok) {
    auto session = getSession();
    if (session) {
        session->onWriteComplete(ok);
    }
}

void ReadTag::proceed(bool ok) {
    auto session = getSession();
    if (session) {
        session->onReadComplete(ok);
    }
}

void WritesDoneTag::proceed(bool ok) {
    auto session = getSession();
    if (session) {
        session->onWritesDoneComplete(ok);
    }
}

void FinishTag::proceed(bool ok) {
    auto session = getSession();
    if (session) {
        session->onFinishComplete(ok);
    }
}

} // namespace google_transcribe
