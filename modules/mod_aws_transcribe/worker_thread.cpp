#include "worker_thread.h"
#include <switch.h>
#include <thread>
#include <atomic>
#include <algorithm> // Required for std::find
#include "audio_pipe.h"
#include <chrono>

// Global job queue (lock-free MPSC)
deepgram::BoundedMPSCQueue<WorkerJob, 16384> g_job_queue;

// Performance counters
static std::atomic<uint64_t> g_jobs_processed{0};
static std::atomic<uint64_t> g_active_sessions{0};

void push_job(WorkerJob* job) {
    g_job_queue.push(job);
    // No synchronization needed - lock-free queue handles everything
}

// High-performance worker thread with adaptive backoff
void worker_thread_run(std::atomic<bool>& running) {
    std::vector<std::shared_ptr<AwsPipe>> sessions;
    uint32_t empty_loops = 0;
    const uint32_t max_backoff = 1000; // Max 1ms backoff
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "High-performance worker thread started\n");

    while (running.load(std::memory_order_relaxed)) {
        bool work_done = false;

        // Part 1: Process job queue (lock-free)
        WorkerJob* job;
        while ((job = g_job_queue.pop()) != nullptr) {
            work_done = true;
            g_jobs_processed.fetch_add(1, std::memory_order_relaxed);
            
            switch (job->type) {
                case JobType::Connect:
                    if (job->pPipe) {
                        sessions.push_back(job->pPipe);
                        job->pPipe->connect();
                        g_active_sessions.fetch_add(1, std::memory_order_relaxed);
                    }
                    break;
                case JobType::Disconnect:
                    if (job->pPipe) {
                        job->pPipe->close();
                        // O(1) swap-and-pop removal
                        auto it = std::find(sessions.begin(), sessions.end(), job->pPipe);
                        if (it != sessions.end()) {
                            std::swap(*it, sessions.back());
                            sessions.pop_back();
                            g_active_sessions.fetch_sub(1, std::memory_order_relaxed);
                        }
                    }
                    break;
                case JobType::Terminate:
                    running.store(false, std::memory_order_relaxed);
                    break;
            }
            delete job;
        }

        // Part 2: Process audio for active sessions
        if (!sessions.empty()) {
            work_done = true;
            for (auto& p : sessions) {
                p->process_audio();
            }
        }

        // Adaptive backoff: sleep longer when no work, shorter when busy
        if (!work_done) {
            empty_loops++;
            uint32_t backoff_us = std::min(empty_loops * 10, max_backoff);
            std::this_thread::sleep_for(std::chrono::microseconds(backoff_us));
        } else {
            empty_loops = 0; // Reset backoff when work is available
        }
    }
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
        "Worker thread shutting down. Processed %lu jobs, managed %lu sessions\n",
        g_jobs_processed.load(), g_active_sessions.load());
}

// Performance monitoring functions
uint64_t get_worker_stats_jobs_processed() {
    return g_jobs_processed.load(std::memory_order_relaxed);
}

uint64_t get_worker_stats_active_sessions() {
    return g_active_sessions.load(std::memory_order_relaxed);
}
