#pragma once
#include <string>
#include <functional>

namespace LinkEmbed {

struct FetchResult {
    std::string content;
    long status_code = 0;
    std::string error;
    std::string effective_url;
    bool truncated = false;
    void* user_data = nullptr; // To track requests
};

class IHTMLFetcher {
public:
    using Callback = std::function<void(FetchResult)>;
    virtual ~IHTMLFetcher() = default;
    virtual void Fetch(const std::string& url, size_t attempt_max_bytes, bool use_range, void* user_data, Callback cb) = 0;
};

}

