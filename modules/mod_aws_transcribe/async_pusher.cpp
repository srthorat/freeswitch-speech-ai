#include "async_pusher.hpp"
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <switch.h>

// Helper to create MD5 hash (required for Pusher auth)
static std::string md5_hash(const std::string& input) {
    unsigned char hash[MD5_DIGEST_LENGTH];
    MD5_CTX md5_ctx;
    MD5_Init(&md5_ctx);
    MD5_Update(&md5_ctx, input.c_str(), input.length());
    MD5_Final(hash, &md5_ctx);
    
    std::stringstream ss;
    for(int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

// Helper to create HMAC-SHA256 signature
static std::string hmac_sha256(const std::string& key, const std::string& msg) {
    unsigned char hash[32];
    HMAC_CTX *hmac = HMAC_CTX_new();
    HMAC_Init_ex(hmac, &key[0], key.length(), EVP_sha256(), NULL);
    HMAC_Update(hmac, (unsigned char*)&msg[0], msg.length());
    unsigned int len = 32;
    HMAC_Final(hmac, hash, &len);
    HMAC_CTX_free(hmac);

    std::stringstream ss;
    for(unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }
    return ss.str();
}

AsyncPusher::AsyncPusher(const std::string& app_id, const std::string& key, const std::string& secret, const std::string& cluster) :
    m_app_id(app_id),
    m_key(key),
    m_secret(secret),
    m_cluster(cluster),
    m_http_client(std::make_shared<AsyncHttp>())
{
    m_http_client->start();
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_INFO, 
        "AsyncPusher initialized for app_id=%s, cluster=%s\n", 
        app_id.c_str(), cluster.c_str());
}

void AsyncPusher::send(const std::string& channel, const std::string& event, const std::string& data) {
    std::string path = "/apps/" + m_app_id + "/events";
    std::string host = "api-" + m_cluster + ".pusher.com";
    std::string url = "https://" + host + path;
    
    // Escape data for JSON (robust escaping matching Deepgram implementation)
    std::string escaped_data;
    escaped_data.reserve(data.length() * 2);
    for (char c : data) {
        switch (c) {
            case '"':  escaped_data += "\\\""; break;
            case '\\': escaped_data += "\\\\"; break;
            case '\n': escaped_data += "\\n"; break;
            case '\r': escaped_data += "\\r"; break;
            case '\t': escaped_data += "\\t"; break;
            default:   escaped_data += c; break;
        }
    }
    
    // Build Pusher request body matching Deepgram implementation (use channels array)
    std::string body = "{\"name\":\"" + event + "\",\"channels\":[\"" + channel + "\"],\"data\":\"" + escaped_data + "\"}";
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Pusher request body: %s\n", body.c_str());
    
    long timestamp = time(nullptr);
    std::string body_md5 = md5_hash(body);
    
    // Create authentication signature according to Pusher spec
    std::string query_string = "auth_key=" + m_key + "&auth_timestamp=" + std::to_string(timestamp) + "&auth_version=1.0&body_md5=" + body_md5;
    std::string string_to_sign = "POST\n" + path + "\n" + query_string;
    
    std::string signature = hmac_sha256(m_secret, string_to_sign);
    
    // Complete URL with authentication
    std::string auth_url = url + "?" + query_string + "&auth_signature=" + signature;
    
    switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "Pusher URL: %s\n", auth_url.c_str());
    
    std::vector<std::string> headers;
    headers.push_back("Content-Type: application/json");
    headers.push_back("User-Agent: FreeSWITCH-AWS-Transcribe/1.0");
    headers.push_back("Connection: keep-alive"); // Enable connection pooling
    
    m_http_client->post(auth_url, headers, body);
}
