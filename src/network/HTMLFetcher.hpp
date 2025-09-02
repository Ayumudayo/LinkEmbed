#pragma once
#include <string>
#include <optional>

namespace LinkEmbed {
    class HTMLFetcher {
    public:
        struct FetchResult {
            std::string content;
            long status_code;
            std::string error;
            std::string effective_url;
            bool truncated = false;
        };

        // Default full request (no range used). Max bytes are taken from config's max_html_bytes.
        static FetchResult Fetch(const std::string& url);

        // Variant that allows controlling range requests/partial reception.
        // attempt_max_bytes: The maximum number of bytes to store in the buffer for this attempt (buffer limit).
        // use_range: If true, attempts to request with HTTP Range header: bytes=0-(attempt_max_bytes-1).
        static FetchResult Fetch(const std::string& url, size_t attempt_max_bytes, bool use_range);
    };
}