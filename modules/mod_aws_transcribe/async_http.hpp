#ifndef __ASYNC_HTTP_HPP__
#define __ASYNC_HTTP_HPP__

#include <string>
#include <vector>
#include <memory>
#include <functional>

// Forward declaration
struct curl_slist;

class AsyncHttp {
public:
    AsyncHttp();
    ~AsyncHttp();

    void start();
    void stop();

    // Non-blocking post. Returns immediately.
    void post(const std::string& url, const std::vector<std::string>& headers, const std::string& body);

private:
    // PIMPL idiom to hide curl implementation details
    class AsyncHttpImpl;
    std::unique_ptr<AsyncHttpImpl> m_pimpl;
};

#endif // __ASYNC_HTTP_HPP__
