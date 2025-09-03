#pragma once
#include <string>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>

// Forward declare CURLM
typedef void CURLM;

namespace LinkEmbed {

class HTMLFetcher {
public:
    struct FetchResult {
        std::string content;
        long status_code = 0;
        std::string error;
        std::string effective_url;
        bool truncated = false;
        void* user_data = nullptr; // To track requests
    };

    using Callback = std::function<void(FetchResult)>;

    HTMLFetcher();
    ~HTMLFetcher();

    // Non-copyable
    HTMLFetcher(const HTMLFetcher&) = delete;
    HTMLFetcher& operator=(const HTMLFetcher&) = delete;

    void Fetch(const std::string& url, size_t attempt_max_bytes, bool use_range, void* user_data, Callback cb);

private:
    void Run();

    CURLM* multi_handle_ = nullptr;
    std::thread worker_thread_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    bool stop_ = false;

    struct Request {
        std::string url;
        size_t max_bytes;
        bool use_range;
        void* user_data;
        Callback callback;
    };
    std::vector<Request> pending_requests_;
};

}