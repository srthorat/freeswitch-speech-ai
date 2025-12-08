#include "async_http.hpp"
#include <curl/curl.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <queue>
#include <switch.h>

struct HttpJob {
    std::string url;
    std::vector<std::string> headers;
    std::string body;
};

class AsyncHttp::AsyncHttpImpl {
public:
    AsyncHttpImpl() : m_running(false), m_curl_multi(nullptr) {}

    ~AsyncHttpImpl() {
        stop();
    }

    void start() {
        m_running = true;
        
        // Initialize curl multi handle with optimizations
        m_curl_multi = curl_multi_init();
        curl_multi_setopt(m_curl_multi, CURLMOPT_MAXCONNECTS, 10L); // Connection pooling
        curl_multi_setopt(m_curl_multi, CURLMOPT_MAX_HOST_CONNECTIONS, 5L);
        curl_multi_setopt(m_curl_multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX); // HTTP/2
        
        m_thread = std::thread(&AsyncHttpImpl::run, this);
    }

    void stop() {
        if (m_running.exchange(false)) {
            if (m_thread.joinable()) {
                // Signal the thread to wake up and exit
                curl_multi_wakeup(m_curl_multi);
                m_thread.join();
            }
        }
    }

    void post(const std::string& url, const std::vector<std::string>& headers, const std::string& body) {
        if (!m_running) return;

        HttpJob job = {url, headers, body};
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            m_job_queue.push(job);
        }
        
        curl_multi_wakeup(m_curl_multi);
    }


private:
    void run() {
        m_curl_multi = curl_multi_init();
        if (!m_curl_multi) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to initialize curl_multi\n");
            return;
        }

        while (m_running) {
            std::unique_lock<std::mutex> lock(m_queue_mutex);
            while (!m_job_queue.empty()) {
                HttpJob job = m_job_queue.front();
                m_job_queue.pop();
                lock.unlock(); // Unlock while adding handle
                add_handle(job);
                lock.lock();
            }
            lock.unlock();

            int still_running = 0;
            curl_multi_perform(m_curl_multi, &still_running);

            // Wait for activity or timeout
            curl_multi_poll(m_curl_multi, NULL, 0, 100, NULL);

            // Check for completed transfers
            CURLMsg *msg;
            int msgs_left;
            while ((msg = curl_multi_info_read(m_curl_multi, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                    CURL *easy_handle = msg->easy_handle;
                    // You can check msg->data.result for the outcome
                    curl_multi_remove_handle(m_curl_multi, easy_handle);
                    curl_easy_cleanup(easy_handle);
                }
            }
        }
        curl_multi_cleanup(m_curl_multi);
    }

    void add_handle(const HttpJob& job) {
        CURL *easy_handle = curl_easy_init();
        if (easy_handle) {
            curl_easy_setopt(easy_handle, CURLOPT_URL, job.url.c_str());
            curl_easy_setopt(easy_handle, CURLOPT_POSTFIELDS, job.body.c_str());
            curl_easy_setopt(easy_handle, CURLOPT_POSTFIELDSIZE, job.body.length());
            
            struct curl_slist *chunk = NULL;
            for (const auto& header : job.headers) {
                chunk = curl_slist_append(chunk, header.c_str());
            }
            if (chunk) {
                curl_easy_setopt(easy_handle, CURLOPT_HTTPHEADER, chunk);
                // curl_slist_free_all(chunk) will be done after the request
            }
            
            curl_multi_add_handle(m_curl_multi, easy_handle);
        }
    }

    std::atomic<bool> m_running;
    std::thread m_thread;
    CURLM *m_curl_multi;
    std::queue<HttpJob> m_job_queue;
    std::mutex m_queue_mutex;
};

// --- Public AsyncHttp class methods ---
AsyncHttp::AsyncHttp() : m_pimpl(new AsyncHttpImpl()) {}
AsyncHttp::~AsyncHttp() {}
void AsyncHttp::start() { m_pimpl->start(); }
void AsyncHttp::stop() { m_pimpl->stop(); }
void AsyncHttp::post(const std::string& url, const std::vector<std::string>& headers, const std::string& body) {
    m_pimpl->post(url, headers, body);
}
