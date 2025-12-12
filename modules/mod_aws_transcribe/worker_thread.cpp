#include "worker_thread.h"
#include <switch.h>
#include <thread>
#include <atomic>
#include <algorithm> // Required for std::find
#include "audio_pipe.h"
#include <chrono>
#include <condition_variable>
#include <vector>
#include <string>
#include <iostream>
#include <sstream>

// Thread Affinity Helper
static void set_thread_affinity(int thread_idx) {
    const char* affinity_str = std::getenv("MOD_AWS_WORKER_AFFINITY");
    if (!affinity_str) return;

    // Parse comma-separated list of cores: "0,2,4,6"
    std::vector<int> cores;
    std::string s(affinity_str);
    std::string delimiter = ",";
    size_t pos = 0;
    std::string token;
    while ((pos = s.find(delimiter)) != std::string::npos) {
        token = s.substr(0, pos);
        cores.push_back(std::stoi(token));
        s.erase(0, pos + delimiter.length());
    }
    cores.push_back(std::stoi(s));

    if (cores.empty()) return;

    // Assign round-robin
    int core_id = cores[thread_idx % cores.size()];

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset) != 0) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "Failed to set affinity for worker thread %d to core %d\n", thread_idx, core_id);
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "Worker thread %d pinned to core %d\n", thread_idx, core_id);
    }
}

// Global job queue (lock-free MPSC)
aws::BoundedMPSCQueue<WorkerJob, 16384> g_job_queue;

// Condition variable for immediate worker thread wakeup
// CRITICAL FIX: Prevents thread starvation - same issue as Deepgram module
std::condition_variable g_job_cv;
std::mutex g_job_cv_mutex;

// Performance counters
static std::atomic<uint64_t> g_jobs_processed{0};
static std::atomic<uint64_t> g_active_sessions{0};

void push_job(WorkerJob* job) {
    g_job_queue.push(job);
    // CRITICAL FIX: Wake worker thread immediately
    // Without this, thread sleeps while jobs wait in queue (thread starvation)
    g_job_cv.notify_one();
}

// High-performance worker thread with condition variable wakeup
// High-performance worker thread with condition variable wakeup
void worker_thread_run(std::atomic<bool>& running, int thread_idx) {
    std::vector<std::shared_ptr<AwsPipe>> sessions;
    static switch_time_t last_cleanup = 0;
    
    // Set thread affinity based on index and Env Var
    set_thread_affinity(thread_idx);
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "High-performance worker thread %d started\n", thread_idx);

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
                        // CRITICAL: DON'T remove immediately - AWS SDK async operations need time
                        // Mark for removal but keep in sessions vector for now
                        // The shared_ptr will keep object alive until AWS completes
                        // TODO: Add proper async completion tracking
                        // For now, keep in vector - it won't process audio after close()
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

        // Part 3: Periodic cleanup of closed sessions (every 1 second)
        // Remove sessions that have been closed for 5+ seconds to allow AWS SDK async operations to complete
        switch_time_t now = switch_time_now();
        if (now - last_cleanup > 1000000) { // 1 second
            size_t before = sessions.size();
            sessions.erase(
                std::remove_if(sessions.begin(), sessions.end(),
                    [](const auto& pipe) { return pipe->should_destroy(); }),
                sessions.end()
            );
            size_t removed = before - sessions.size();
            if (removed > 0) {
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                    "Cleaned up %zu closed AWS transcription session(s)\n", removed);
                g_active_sessions.fetch_sub(removed, std::memory_order_relaxed);
            }
            last_cleanup = now;
        }

        // CRITICAL FIX: Use condition variable instead of sleep
        // This allows immediate wakeup when jobs arrive (via notify_one)
        // Prevents thread starvation that causes 100ms-1000ms+ delays
        if (!work_done) {
            std::unique_lock<std::mutex> lock(g_job_cv_mutex);
            g_job_cv.wait_for(lock, std::chrono::milliseconds(1));
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
