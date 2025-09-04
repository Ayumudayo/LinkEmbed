#include "HTMLFetcher.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <curl/curl.h>
#include <algorithm>
#include <stdexcept>
#include <vector>
#include <iostream>
#include "../../config/Config.hpp"
#include "../utils/Logger.hpp"

namespace {

// Context for a single cURL easy handle transfer
struct TransferContext {
    std::string buffer;
    size_t max_bytes;
    bool truncated = false;
    LinkEmbed::IHTMLFetcher::Callback callback;
    void* user_data = nullptr;
    char error_buffer[CURL_ERROR_SIZE] = {0};
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t chunk = size * nmemb;
    auto* ctx = static_cast<TransferContext*>(userp);
    if (!ctx) return 0;

    size_t current_size = ctx->buffer.size();
    if (current_size >= ctx->max_bytes) {
        ctx->truncated = true;
        return chunk; // Still need to "receive" it to complete the transfer
    }

    size_t remaining_space = ctx->max_bytes - current_size;
    size_t to_copy = std::min(chunk, remaining_space);

    if (to_copy > 0) {
        try {
            ctx->buffer.append(static_cast<char*>(contents), to_copy);
        } catch (...) {
            return 0; // Indicates an error
        }
    }

    if (to_copy < chunk) {
        ctx->truncated = true;
    }

    return chunk;
}

// Helper to create and configure a cURL easy handle
CURL* CreateEasyHandle(const std::string& url, size_t max_bytes, bool use_range, TransferContext* transfer_ctx) {
    CURL* curl = curl_easy_init();
    if (!curl) return nullptr;

    const auto& config = LinkEmbed::Config::GetInstance();

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, transfer_ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config.http_user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, config.http_max_redirects);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.http_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip, deflate");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, transfer_ctx->error_buffer);
    curl_easy_setopt(curl, CURLOPT_PRIVATE, transfer_ctx);

    long allowed_protocols = CURLPROTO_HTTP | CURLPROTO_HTTPS;
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, allowed_protocols);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, allowed_protocols);

    if (use_range && max_bytes > 0) {
        std::string range = "0-" + std::to_string(max_bytes - 1);
        curl_easy_setopt(curl, CURLOPT_RANGE, range.c_str());
    }

    return curl;
}

} // anonymous namespace

namespace LinkEmbed {

HTMLFetcher::HTMLFetcher() {
    multi_handle_ = curl_multi_init();
    if (!multi_handle_) {
        throw std::runtime_error("Failed to initialize cURL multi handle");
    }
    worker_thread_ = std::thread(&HTMLFetcher::Run, this);
}

HTMLFetcher::~HTMLFetcher() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    cv_.notify_one();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    if (multi_handle_) {
        curl_multi_cleanup(multi_handle_);
    }
}

void HTMLFetcher::Fetch(const std::string& url, size_t attempt_max_bytes, bool use_range, void* user_data, Callback cb) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        pending_requests_.push_back({url, attempt_max_bytes, use_range, user_data, std::move(cb)});
    }
    cv_.notify_one();
}

void HTMLFetcher::Run() {
    Logger::Log(LogLevel::Debug, "HTMLFetcher worker thread started.");
    int still_running = 0;

    while (!stop_) {
        std::vector<Request> current_requests;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this, &still_running] { return stop_ || !pending_requests_.empty() || still_running > 0; });
            if (stop_ && pending_requests_.empty() && still_running == 0) break;
            std::swap(current_requests, pending_requests_);
        }

        for (const auto& req : current_requests) {
            auto* transfer_ctx = new TransferContext{ "", req.max_bytes, false, req.callback, req.user_data };
            CURL* easy_handle = CreateEasyHandle(req.url, req.max_bytes, req.use_range, transfer_ctx);
            if (easy_handle) {
                curl_multi_add_handle(multi_handle_, easy_handle);
                Logger::Log(LogLevel::Debug, "Added easy handle for URL: " + req.url);
            } else {
                delete transfer_ctx;
                Logger::Log(LogLevel::Error, "Failed to create cURL easy handle for: " + req.url);
            }
        }

        curl_multi_perform(multi_handle_, &still_running);
        Logger::Log(LogLevel::Debug, "curl_multi_perform called. Still running: " + std::to_string(still_running));

        int msgs_in_queue;
        CURLMsg* msg;
        while ((msg = curl_multi_info_read(multi_handle_, &msgs_in_queue))) {
            if (msg->msg == CURLMSG_DONE) {
                Logger::Log(LogLevel::Debug, "CURLMSG_DONE received.");
                CURL* easy_handle = msg->easy_handle;
                TransferContext* transfer_ctx = nullptr;
                curl_easy_getinfo(easy_handle, CURLINFO_PRIVATE, &transfer_ctx);

                FetchResult result;
                result.user_data = transfer_ctx->user_data;
                result.content = std::move(transfer_ctx->buffer);
                result.truncated = transfer_ctx->truncated;

                if (msg->data.result == CURLE_OK) {
                    curl_easy_getinfo(easy_handle, CURLINFO_RESPONSE_CODE, &result.status_code);
                    char* eff_url = nullptr;
                    curl_easy_getinfo(easy_handle, CURLINFO_EFFECTIVE_URL, &eff_url);
                    if (eff_url) result.effective_url = eff_url;
                } else {
                    result.error = transfer_ctx->error_buffer;
                    if (result.error.empty()) {
                        result.error = curl_easy_strerror(msg->data.result);
                    }
                }
                
                // Execute callback
                if (transfer_ctx->callback) {
                    try {
                        transfer_ctx->callback(std::move(result));
                    } catch (const std::exception& e) {
                        Logger::Log(LogLevel::Error, "Exception in fetch callback: " + std::string(e.what()));
                    } catch (...) {
                        Logger::Log(LogLevel::Error, "Unknown exception in fetch callback");
                    }
                }

                curl_multi_remove_handle(multi_handle_, easy_handle);
                curl_easy_cleanup(easy_handle);
                delete transfer_ctx;
            }
        }

        if (still_running > 0) {
            curl_multi_wait(multi_handle_, nullptr, 0, 1000, nullptr);
        }
    }
}

}
