#include "async_http.h"

#include <curl/curl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define AWS_HTTP_POLL_TIMEOUT_MS 100

typedef struct async_http_job {
    char* url;
    char* body;
    struct curl_slist* headers;
    char* response;
    size_t response_size;
    struct async_http_job* next;
} async_http_job_t;

struct async_http {
    pthread_t thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    async_http_job_t* head;
    async_http_job_t* tail;
    CURLM* multi;
    int running;
};

static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    async_http_job_t* job = (async_http_job_t*)userp;
    size_t total = size * nmemb;

    if (!job) {
        return 0;
    }

    char* new_buffer = realloc(job->response, job->response_size + total + 1);
    if (!new_buffer) {
        return 0;
    }

    memcpy(new_buffer + job->response_size, contents, total);
    job->response = new_buffer;
    job->response_size += total;
    job->response[job->response_size] = '\0';
    return total;
}

static void free_job(async_http_job_t* job) {
    if (!job) return;
    if (job->headers) {
        curl_slist_free_all(job->headers);
    }
    free(job->url);
    free(job->body);
    free(job->response);
    free(job);
}

static void* async_http_thread(void* data);
static void add_handle(struct async_http* client, async_http_job_t* job);

async_http_t* async_http_create(void) {
    async_http_t* client = (async_http_t*)calloc(1, sizeof(async_http_t));
    if (!client) {
        return NULL;
    }

    if (pthread_mutex_init(&client->mutex, NULL) != 0) {
        free(client);
        return NULL;
    }
    if (pthread_cond_init(&client->cond, NULL) != 0) {
        pthread_mutex_destroy(&client->mutex);
        free(client);
        return NULL;
    }

    client->multi = curl_multi_init();
    if (!client->multi) {
        pthread_cond_destroy(&client->cond);
        pthread_mutex_destroy(&client->mutex);
        free(client);
        return NULL;
    }

    curl_multi_setopt(client->multi, CURLMOPT_MAXCONNECTS, 10L);
    curl_multi_setopt(client->multi, CURLMOPT_MAX_HOST_CONNECTIONS, 5L);
#ifdef CURLMOPT_PIPELINING
    curl_multi_setopt(client->multi, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
#endif

    client->running = 1;
    if (pthread_create(&client->thread, NULL, async_http_thread, client) != 0) {
        curl_multi_cleanup(client->multi);
        pthread_cond_destroy(&client->cond);
        pthread_mutex_destroy(&client->mutex);
        free(client);
        return NULL;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "AsyncHttp started\n");
    return client;
}

void async_http_destroy(async_http_t* client) {
    if (!client) {
        return;
    }

    pthread_mutex_lock(&client->mutex);
    client->running = 0;
    pthread_cond_signal(&client->cond);
    pthread_mutex_unlock(&client->mutex);

    curl_multi_wakeup(client->multi);
    pthread_join(client->thread, NULL);

    async_http_job_t* job = client->head;
    while (job) {
        async_http_job_t* next = job->next;
        free_job(job);
        job = next;
    }

    if (client->multi) {
        curl_multi_cleanup(client->multi);
    }

    pthread_cond_destroy(&client->cond);
    pthread_mutex_destroy(&client->mutex);
    free(client);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, "AsyncHttp stopped\n");
}

switch_status_t async_http_post(async_http_t* client, const char* url, const char* body,
    const char* const* headers, size_t header_count) {
    if (!client || !url || !body) {
        return SWITCH_STATUS_FALSE;
    }

    async_http_job_t* job = (async_http_job_t*)calloc(1, sizeof(async_http_job_t));
    if (!job) {
        return SWITCH_STATUS_FALSE;
    }

    job->url = strdup(url);
    job->body = strdup(body);
    job->next = NULL;

    if (!job->url || !job->body) {
        free_job(job);
        return SWITCH_STATUS_FALSE;
    }

    for (size_t i = 0; i < header_count; ++i) {
        job->headers = curl_slist_append(job->headers, headers[i]);
    }

    pthread_mutex_lock(&client->mutex);
    if (client->tail) {
        client->tail->next = job;
        client->tail = job;
    } else {
        client->head = client->tail = job;
    }
    pthread_cond_signal(&client->cond);
    pthread_mutex_unlock(&client->mutex);

    curl_multi_wakeup(client->multi);
    return SWITCH_STATUS_SUCCESS;
}

static void* async_http_thread(void* data) {
    async_http_t* client = (async_http_t*)data;
    int running_handles = 0;

    while (1) {
        pthread_mutex_lock(&client->mutex);
        while (client->running && client->head == NULL && running_handles == 0) {
            pthread_cond_wait(&client->cond, &client->mutex);
        }

        async_http_job_t* job_list = client->head;
        client->head = NULL;
        client->tail = NULL;
        int running_flag = client->running;
        pthread_mutex_unlock(&client->mutex);

        while (job_list) {
            async_http_job_t* next = job_list->next;
            job_list->next = NULL;
            add_handle(client, job_list);
            job_list = next;
        }

        if (client->multi) {
            CURLMcode rc;
            do {
                rc = curl_multi_perform(client->multi, &running_handles);
            } while (rc == CURLM_CALL_MULTI_PERFORM);

            if (running_handles > 0) {
                curl_multi_poll(client->multi, NULL, 0, AWS_HTTP_POLL_TIMEOUT_MS, NULL);
            }
        }

        CURLMsg* msg;
        int msgs_left;
        while ((msg = curl_multi_info_read(client->multi, &msgs_left))) {
            if (msg->msg == CURLMSG_DONE) {
                CURL* easy_handle = msg->easy_handle;
                async_http_job_t* job = NULL;
                curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, (char**)&job);

                long response_code = 0;
                curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &response_code);

                if (msg->data.result != CURLE_OK) {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                        "Pusher HTTP error: %s (code=%d)\n",
                        curl_easy_strerror(msg->data.result), msg->data.result);
                } else if (response_code < 200 || response_code >= 300) {
                    const char* resp_body = (job && job->response) ? job->response : "(no body)";
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                        "Pusher HTTP %ld: %s\n", response_code, resp_body);
                } else {
                    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
                        "Pusher HTTP %ld: Success\n", response_code);
                }

                curl_multi_remove_handle(client->multi, easy_handle);
                curl_easy_cleanup(easy_handle);
                free_job(job);

                if (running_handles > 0) {
                    running_handles--;
                }
            }
        }

        if (!running_flag && running_handles == 0) {
            pthread_mutex_lock(&client->mutex);
            int has_pending = (client->head != NULL);
            pthread_mutex_unlock(&client->mutex);
            if (!has_pending) {
                break;
            }
        }
    }

    return NULL;
}

static void add_handle(struct async_http* client, async_http_job_t* job) {
    if (!job) {
        return;
    }

    CURL* easy_handle = curl_easy_init();
    if (!easy_handle) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "Failed to init CURL handle\n");
        free_job(job);
        return;
    }

    curl_easy_setopt(easy_handle, CURLOPT_URL, job->url);
    curl_easy_setopt(easy_handle, CURLOPT_POST, 1L);
    curl_easy_setopt(easy_handle, CURLOPT_POSTFIELDS, job->body);
    curl_easy_setopt(easy_handle, CURLOPT_POSTFIELDSIZE, (long)strlen(job->body));
    curl_easy_setopt(easy_handle, CURLOPT_HTTPHEADER, job->headers);
    curl_easy_setopt(easy_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(easy_handle, CURLOPT_WRITEDATA, job);
    curl_easy_setopt(easy_handle, CURLOPT_PRIVATE, job);
    curl_easy_setopt(easy_handle, CURLOPT_NOSIGNAL, 1L);

    CURLMcode rc = curl_multi_add_handle(client->multi, easy_handle);
    if (rc != CURLM_OK) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
            "Failed to add CURL handle: %s\n", curl_multi_strerror(rc));
        curl_easy_cleanup(easy_handle);
        free_job(job);
    }
}
