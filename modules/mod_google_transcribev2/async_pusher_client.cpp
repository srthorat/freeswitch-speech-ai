/*
 * async_pusher_client.cpp - Implementation of AsyncPusherClient
 *
 * Non-blocking HTTP client for delivering transcription events to Pusher.
 * Transforms Google Speech transcriptions to Deepgram-compatible Pusher format.
 */

#include "async_pusher_client.h"
#include <switch.h>
#include <switch_json.h>  // cJSON for JSON parsing/building
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>
#include <cstdlib>  // std::getenv

namespace google_transcribe {

// CURL write callback
static size_t curlWriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::string* response = static_cast<std::string*>(userp);
    size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

// Singleton instance
AsyncPusherClient& AsyncPusherClient::getInstance() {
    static AsyncPusherClient instance;
    return instance;
}

AsyncPusherClient::AsyncPusherClient() {
    // Initialize CURL globally (should be done once)
    curl_global_init(CURL_GLOBAL_DEFAULT);
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "AsyncPusherClient: Instance created\n");
}

AsyncPusherClient::~AsyncPusherClient() {
    if (m_running.load()) {
        shutdown(1000);
    }
    curl_global_cleanup();
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO,
        "AsyncPusherClient: Instance destroyed, sent=%lu, failed=%lu, dropped=%lu\n",
        m_events_sent.load(), m_events_failed.load(), m_events_dropped.load());
}

void AsyncPusherClient::start(unsigned int num_workers, size_t max_queue_size) {
    if (m_running.load()) {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "AsyncPusherClient: Already running\n");
        return;
    }

    m_max_queue_size = max_queue_size;
    m_running.store(true);
    m_shutdown_requested.store(false);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "AsyncPusherClient: Starting with %u workers, max queue %zu\n",
        num_workers, max_queue_size);

    // Create CURL handles for each worker (connection pooling)
    m_curl_handles.resize(num_workers);
    for (unsigned int i = 0; i < num_workers; i++) {
        m_curl_handles[i] = curl_easy_init();
        if (!m_curl_handles[i]) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                "AsyncPusherClient: Failed to create CURL handle for worker %u\n", i);
        }
    }

    // Create worker threads
    for (unsigned int i = 0; i < num_workers; i++) {
        m_workers.emplace_back(&AsyncPusherClient::workerLoop, this, i);
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "AsyncPusherClient: Started successfully\n");
}

void AsyncPusherClient::shutdown(unsigned int timeout_ms) {
    if (!m_running.load()) {
        return;
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "AsyncPusherClient: Initiating shutdown, %zu events pending\n",
        getQueueDepth());

    m_shutdown_requested.store(true);

    // Notify all workers to wake up
    m_queue_cv.notify_all();

    // Wait for workers with timeout
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    m_workers.clear();

    // Cleanup CURL handles
    for (auto handle : m_curl_handles) {
        if (handle) {
            curl_easy_cleanup(handle);
        }
    }
    m_curl_handles.clear();

    m_running.store(false);

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE,
        "AsyncPusherClient: Shutdown complete, sent=%lu, failed=%lu, dropped=%lu\n",
        m_events_sent.load(), m_events_failed.load(), m_events_dropped.load());
}

bool AsyncPusherClient::enqueue(PusherEvent event) {
    // Quick check without lock
    if (!m_running.load() || m_shutdown_requested.load()) {
        return false;
    }

    // Validate credentials
    if (event.app_id.empty() || event.app_key.empty() || event.app_secret.empty()) {
        // Pusher not configured - silently skip
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);

        // Check queue size
        if (m_queue.size() >= m_max_queue_size) {
            m_events_dropped++;
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "AsyncPusherClient: Queue full, dropping event for call_id=%s\n",
                event.call_id.c_str());
            return false;
        }

        m_queue.push(std::move(event));
    }

    m_queue_cv.notify_one();
    return true;
}

size_t AsyncPusherClient::getQueueDepth() const {
    std::lock_guard<std::mutex> lock(m_queue_mutex);
    return m_queue.size();
}

void AsyncPusherClient::workerLoop(int worker_id) {
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "AsyncPusherClient: Worker %d started\n", worker_id);

    while (true) {
        PusherEvent event;

        {
            std::unique_lock<std::mutex> lock(m_queue_mutex);

            m_queue_cv.wait(lock, [this] {
                return !m_queue.empty() || m_shutdown_requested.load();
            });

            if (m_shutdown_requested.load() && m_queue.empty()) {
                break;
            }

            if (m_queue.empty()) {
                continue;
            }

            event = std::move(m_queue.front());
            m_queue.pop();
        }

        // Send the event (outside the lock)
        bool success = sendEvent(event);

        if (success) {
            m_events_sent++;
        } else {
            // Retry logic
            if (event.retry_count < PusherEvent::MAX_RETRIES) {
                event.retry_count++;
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                    "AsyncPusherClient: Retrying event for call_id=%s (attempt %d/%d)\n",
                    event.call_id.c_str(), event.retry_count, PusherEvent::MAX_RETRIES);

                // Re-enqueue with exponential backoff delay
                // For simplicity, we just re-add to queue (could add delay)
                std::lock_guard<std::mutex> lock(m_queue_mutex);
                if (m_queue.size() < m_max_queue_size) {
                    m_queue.push(std::move(event));
                } else {
                    m_events_failed++;
                }
            } else {
                m_events_failed++;
                switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR,
                    "AsyncPusherClient: Failed to send event for call_id=%s after %d retries\n",
                    event.call_id.c_str(), PusherEvent::MAX_RETRIES);
            }
        }
    }

    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG,
        "AsyncPusherClient: Worker %d stopped\n", worker_id);
}

bool AsyncPusherClient::sendEvent(const PusherEvent& event) {
    // Skip events with empty payloads (e.g., empty transcripts)
    if (event.json_payload.empty()) {
        return true;  // Not an error, just skip
    }

    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }

    std::string body = buildRequestBody(event);
    std::string url = buildSignedUrl(event, body);
    std::string response;

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);  // 2 second timeout
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1L);  // 1 second connect timeout
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);  // Thread-safe

    CURLcode res = curl_easy_perform(curl);

    bool success = false;
    if (res == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        success = (http_code == 200);

        if (!success) {
            switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
                "AsyncPusherClient: HTTP %ld for call_id=%s: %s\n",
                http_code, event.call_id.c_str(), response.c_str());
        }
    } else {
        switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING,
            "AsyncPusherClient: CURL error for call_id=%s: %s\n",
            event.call_id.c_str(), curl_easy_strerror(res));
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return success;
}

std::string AsyncPusherClient::buildRequestBody(const PusherEvent& event) {
    std::string escaped_data = escapeJson(event.json_payload);

    std::ostringstream body;
    body << "{\"name\":\"" << event.event_name << "\","
         << "\"channels\":[\"" << event.channel_name << "\"],"
         << "\"data\":\"" << escaped_data << "\"}";

    return body.str();
}

std::string AsyncPusherClient::buildSignedUrl(const PusherEvent& event, const std::string& body) {
    std::string body_md5 = md5Hex(body);
    std::time_t timestamp = std::time(nullptr);

    std::ostringstream query;
    query << "auth_key=" << event.app_key
          << "&auth_timestamp=" << timestamp
          << "&auth_version=1.0"
          << "&body_md5=" << body_md5;

    std::ostringstream to_sign;
    to_sign << "POST\n"
            << "/apps/" << event.app_id << "/events\n"
            << query.str();

    std::string signature = hmacSha256Hex(event.app_secret, to_sign.str());

    std::string cluster = event.cluster.empty() ? "us2" : event.cluster;

    std::ostringstream url;
    url << "https://api-" << cluster << ".pusher.com"
        << "/apps/" << event.app_id << "/events?"
        << query.str()
        << "&auth_signature=" << signature;

    return url.str();
}

std::string AsyncPusherClient::hmacSha256Hex(const std::string& key, const std::string& data) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;

    HMAC(EVP_sha256(),
         key.c_str(), static_cast<int>(key.length()),
         reinterpret_cast<const unsigned char*>(data.c_str()), data.length(),
         digest, &len);

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; i++) {
        hex << std::setw(2) << static_cast<int>(digest[i]);
    }

    return hex.str();
}

std::string AsyncPusherClient::md5Hex(const std::string& data) {
    unsigned char digest[MD5_DIGEST_LENGTH];

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_md5(), nullptr);
    EVP_DigestUpdate(ctx, data.c_str(), data.length());
    EVP_DigestFinal_ex(ctx, digest, nullptr);
    EVP_MD_CTX_free(ctx);

    std::ostringstream hex;
    hex << std::hex << std::setfill('0');
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        hex << std::setw(2) << static_cast<int>(digest[i]);
    }

    return hex.str();
}

std::string AsyncPusherClient::escapeJson(const std::string& input) {
    std::string output;
    output.reserve(input.size() * 2);  // Reserve space for escaped chars

    for (char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:   output += c; break;
        }
    }

    return output;
}

// Helper functions

PusherEvent createTranscriptionEvent(
    const std::string& json_result,
    const std::string& call_id,
    const CallMetadata& metadata,
    bool is_final,
    const std::string& app_id,
    const std::string& app_key,
    const std::string& app_secret,
    const std::string& cluster)
{
    PusherEvent event;
    
    // Parse Google JSON to extract transcript text and channel
    // Format: {"transcript":"...", "channel_id":"ch_1"|"ch_2", "items":[...], ...}
    // OR:     {"alternatives":[{"transcript":"..."}], "channel":N, ...}
    cJSON* root = cJSON_Parse(json_result.c_str());
    
    std::string transcript_text;
    int speaker_channel = 0;  // Default to channel 0 (caller)
    
    if (root) {
        // Try direct "transcript" field first (new format)
        cJSON* transcript_field = cJSON_GetObjectItem(root, "transcript");
        if (transcript_field && cJSON_IsString(transcript_field)) {
            const char* text = cJSON_GetStringValue(transcript_field);
            if (text) transcript_text = text;
        }
        
        // Fallback: Extract from alternatives[0].transcript (old format)
        if (transcript_text.empty()) {
            cJSON* alternatives = cJSON_GetObjectItem(root, "alternatives");
            if (alternatives && cJSON_IsArray(alternatives) && cJSON_GetArraySize(alternatives) > 0) {
                cJSON* first_alt = cJSON_GetArrayItem(alternatives, 0);
                cJSON* alt_transcript = cJSON_GetObjectItem(first_alt, "transcript");
                if (alt_transcript && cJSON_IsString(alt_transcript)) {
                    const char* text = cJSON_GetStringValue(alt_transcript);
                    if (text) transcript_text = text;
                }
            }
        }
        
        // Extract channel from "channel_id" field (e.g., "ch_1", "ch_2")
        // channel_id: "ch_1" = channel 0 (caller), "ch_2" = channel 1 (callee)
        cJSON* channel_id_field = cJSON_GetObjectItem(root, "channel_id");
        if (channel_id_field && cJSON_IsString(channel_id_field)) {
            const char* ch_id = cJSON_GetStringValue(channel_id_field);
            if (ch_id) {
                // Parse "ch_N" format - ch_1 = channel 0, ch_2 = channel 1
                if (strncmp(ch_id, "ch_", 3) == 0) {
                    int ch_num = atoi(ch_id + 3);
                    speaker_channel = (ch_num > 1) ? 1 : 0;  // ch_2 -> 1, ch_1 -> 0
                }
            }
        }
        
        // Fallback: numeric "channel" field (0=caller, 1=callee)
        if (!channel_id_field) {
            cJSON* channel_field = cJSON_GetObjectItem(root, "channel");
            if (channel_field && cJSON_IsNumber(channel_field)) {
                speaker_channel = (int)channel_field->valuedouble;
            }
        }
        
        cJSON_Delete(root);
    }
    
    // Skip empty transcripts
    if (transcript_text.empty()) {
        event.json_payload = "";  // Will be skipped by sendEvent
        return event;
    }
    
    // Map channel to speaker_id: channel 0 = caller, channel 1 = callee
    // Format matches Deepgram: "Name(Number)"
    std::string speaker_id;
    if (speaker_channel == 0) {
        // Caller (Channel 0 / left audio / ch_1)
        speaker_id = (!metadata.caller_name.empty() ? metadata.caller_name : "Unknown");
        speaker_id += "(";
        speaker_id += (!metadata.caller_number.empty() ? metadata.caller_number : "Unknown");
        speaker_id += ")";
    } else {
        // Callee (Channel 1 / right audio / ch_2)
        speaker_id = (!metadata.callee_name.empty() ? metadata.callee_name : "Unknown");
        speaker_id += "(";
        speaker_id += (!metadata.callee_number.empty() ? metadata.callee_number : "Unknown");
        speaker_id += ")";
    }
    
    // Get current timestamp in ISO 8601 format
    std::time_t now = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    
    // Build transformed JSON in Deepgram-compatible format:
    // {"type":"final|interim", "speaker_id":"Name(Number)", "text":"...", "timestamp":"..."}
    cJSON* pusher_data = cJSON_CreateObject();
    cJSON_AddStringToObject(pusher_data, "type", is_final ? "final" : "interim");
    cJSON_AddStringToObject(pusher_data, "speaker_id", speaker_id.c_str());
    cJSON_AddStringToObject(pusher_data, "text", transcript_text.c_str());
    cJSON_AddStringToObject(pusher_data, "timestamp", timestamp);
    
    char* json_str = cJSON_PrintUnformatted(pusher_data);
    if (json_str) {
        event.json_payload = json_str;
        free(json_str);
    }
    cJSON_Delete(pusher_data);
    
    event.call_id = call_id;
    event.sip_call_id = metadata.sip_call_id;
    event.caller_number = metadata.caller_number;
    event.caller_name = metadata.caller_name;
    event.callee_number = metadata.callee_number;
    event.callee_name = metadata.callee_name;
    
    // Channel name matches Deepgram format: "call-<callId>"
    const char* channel_prefix = std::getenv("PUSHER_CHANNEL_PREFIX");
    if (!channel_prefix) channel_prefix = "call-";
    event.channel_name = std::string(channel_prefix) + (metadata.sip_call_id.empty() ? call_id : metadata.sip_call_id);
    
    // Event name matches Deepgram format
    const char* event_final = std::getenv("PUSHER_EVENT_FINAL");
    const char* event_interim = std::getenv("PUSHER_EVENT_INTERIM");
    if (!event_final) event_final = "transcription-final";
    if (!event_interim) event_interim = "transcription-interim";
    event.event_name = is_final ? event_final : event_interim;
    
    event.is_final = is_final;
    event.app_id = app_id;
    event.app_key = app_key;
    event.app_secret = app_secret;
    event.cluster = cluster;
    return event;
}

PusherEvent createSessionStartEvent(
    const std::string& call_id,
    const CallMetadata& metadata,
    const std::string& app_id,
    const std::string& app_key,
    const std::string& app_secret,
    const std::string& cluster)
{
    PusherEvent event;
    
    // Build caller_id and callee_id strings matching Deepgram format: "Name(Number)"
    std::string caller_id = (!metadata.caller_name.empty() ? metadata.caller_name : "Unknown");
    caller_id += "(";
    caller_id += (!metadata.caller_number.empty() ? metadata.caller_number : "Unknown");
    caller_id += ")";
    
    std::string callee_id = (!metadata.callee_name.empty() ? metadata.callee_name : "Unknown");
    callee_id += "(";
    callee_id += (!metadata.callee_number.empty() ? metadata.callee_number : "Unknown");
    callee_id += ")";
    
    // Get current timestamp in ISO 8601 format
    std::time_t now = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    
    // Build session start JSON matching Deepgram format:
    // {"type":"session_start", "caller_id":"Name(Number)", "callee_id":"Name(Number)", "timestamp":"..."}
    cJSON* session_data = cJSON_CreateObject();
    cJSON_AddStringToObject(session_data, "type", "session_start");
    cJSON_AddStringToObject(session_data, "caller_id", caller_id.c_str());
    cJSON_AddStringToObject(session_data, "callee_id", callee_id.c_str());
    cJSON_AddStringToObject(session_data, "timestamp", timestamp);
    
    char* json_str = cJSON_PrintUnformatted(session_data);
    if (json_str) {
        event.json_payload = json_str;
        free(json_str);
    }
    cJSON_Delete(session_data);
    
    event.call_id = call_id;
    event.sip_call_id = metadata.sip_call_id;
    event.caller_number = metadata.caller_number;
    event.caller_name = metadata.caller_name;
    event.callee_number = metadata.callee_number;
    event.callee_name = metadata.callee_name;
    
    // Channel name matches Deepgram format: "call-<callId>"
    const char* channel_prefix = std::getenv("PUSHER_CHANNEL_PREFIX");
    if (!channel_prefix) channel_prefix = "call-";
    event.channel_name = std::string(channel_prefix) + (metadata.sip_call_id.empty() ? call_id : metadata.sip_call_id);
    
    // Event name matches Deepgram
    const char* event_session_start = std::getenv("PUSHER_EVENT_SESSION_START");
    if (!event_session_start) event_session_start = "session-start";
    event.event_name = event_session_start;
    
    event.is_final = false;
    event.app_id = app_id;
    event.app_key = app_key;
    event.app_secret = app_secret;
    event.cluster = cluster;
    return event;
}

PusherEvent createSessionStopEvent(
    const std::string& call_id,
    const CallMetadata& metadata,
    const std::string& app_id,
    const std::string& app_key,
    const std::string& app_secret,
    const std::string& cluster)
{
    PusherEvent event;
    
    // Get current timestamp in ISO 8601 format
    std::time_t now = std::time(nullptr);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&now));
    
    // Build session stop JSON matching Deepgram format
    cJSON* session_data = cJSON_CreateObject();
    cJSON_AddStringToObject(session_data, "type", "session_stop");
    cJSON_AddStringToObject(session_data, "timestamp", timestamp);
    
    char* json_str = cJSON_PrintUnformatted(session_data);
    if (json_str) {
        event.json_payload = json_str;
        free(json_str);
    }
    cJSON_Delete(session_data);
    
    event.call_id = call_id;
    event.sip_call_id = metadata.sip_call_id;
    
    // Channel name matches Deepgram format: "call-<callId>"
    const char* channel_prefix = std::getenv("PUSHER_CHANNEL_PREFIX");
    if (!channel_prefix) channel_prefix = "call-";
    event.channel_name = std::string(channel_prefix) + (metadata.sip_call_id.empty() ? call_id : metadata.sip_call_id);
    
    // Event name matches Deepgram
    const char* event_session_stop = std::getenv("PUSHER_EVENT_SESSION_STOP");
    if (!event_session_stop) event_session_stop = "session-stop";
    event.event_name = event_session_stop;
    
    event.is_final = true;
    event.app_id = app_id;
    event.app_key = app_key;
    event.app_secret = app_secret;
    event.cluster = cluster;
    return event;
}

} // namespace google_transcribe
