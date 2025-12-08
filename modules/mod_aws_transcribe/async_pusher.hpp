#ifndef __ASYNC_PUSHER_HPP__
#define __ASYNC_PUSHER_HPP__

#include <string>
#include <vector>
#include <memory>
#include "async_http.hpp"

class AsyncPusher {
public:
    AsyncPusher(const std::string& app_id, const std::string& key, const std::string& secret, const std::string& cluster);

    void send(const std::string& channel, const std::string& event, const std::string& data);

private:
    std::string m_app_id;
    std::string m_key;
    std::string m_secret;
    std::string m_cluster;
    std::shared_ptr<AsyncHttp> m_http_client;
};

#endif // __ASYNC_PUSHER_HPP__
