#ifndef __WORKER_THREAD_H__
#define __WORKER_THREAD_H__

#include <memory>
#include "audio_pipe.h"
#include "lockfree_mpsc_queue.hpp"

enum class JobType {
    Connect,
    Disconnect,
    Terminate
};

struct WorkerJob {
    JobType type;
    std::shared_ptr<AwsPipe> pPipe;
};

extern deepgram::BoundedMPSCQueue<WorkerJob, 16384> g_job_queue;

// Condition variable for immediate worker thread wakeup
extern std::condition_variable g_job_cv;
extern std::mutex g_job_cv_mutex;

void push_job(WorkerJob* job);
void worker_thread_run(std::atomic<bool>& running);

// Performance monitoring
uint64_t get_worker_stats_jobs_processed();
uint64_t get_worker_stats_active_sessions();

#endif // __WORKER_THREAD_H__
