#include "HTMLFetcher.hpp"
#include <curl/curl.h>
#include <stdexcept>
#include <vector>
#include "../../config/Config.hpp"

namespace {

struct WriteContext {
    std::string* buffer;
    size_t max_bytes;
    bool truncated;
};

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    const size_t chunk = size * nmemb;
    auto* ctx = static_cast<WriteContext*>(userp);
    if (!ctx || !ctx->buffer) {
        return 0; 
    }
    size_t current = ctx->buffer->size();
    if (current >= ctx->max_bytes) {
        ctx->truncated = true;
        return chunk; // 전송은 계속 받되 버퍼엔 더 이상 저장하지 않음
    }
    size_t remain = ctx->max_bytes - current;
    size_t to_copy = chunk <= remain ? chunk : remain;
    if (to_copy > 0) {
        try {
            ctx->buffer->append(static_cast<char*>(contents), to_copy);
        } catch (...) {
            return 0;
        }
    }
    if (to_copy < chunk) {
        ctx->truncated = true;
    }
    return chunk;
}

bool parse_scheme_and_host(const std::string& url, std::string& scheme, std::string& host) {
    auto pos = url.find("://");
    if (pos == std::string::npos) return false;
    scheme = url.substr(0, pos);
    if (scheme != "http" && scheme != "https") return false;
    auto start = pos + 3;
    auto end = url.find_first_of("/ ?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!authority.empty() && authority.front() == '[') {
        auto rb = authority.find(']');
        if (rb != std::string::npos) host = authority.substr(1, rb - 1);
        else return false;
    } else {
        auto colon = authority.find(':');
        host = authority.substr(0, colon);
    }
    return !host.empty();
}

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

bool is_private_sockaddr(const sockaddr* sa) {
    if (!sa) return true;
    if (sa->sa_family == AF_INET) {
        const sockaddr_in* a = reinterpret_cast<const sockaddr_in*>(sa);
        uint32_t ip = ntohl(a->sin_addr.s_addr);
        if ((ip >> 24) == 127) return true;              // 127.0.0.0/8 loopback
        if ((ip >> 24) == 10) return true;               // 10.0.0.0/8
        if ((ip >> 20) == ((172 << 4) | 1)) return true; // 172.16.0.0/12
        if ((ip >> 16) == ((192 << 8) | 168)) return true; // 192.168.0.0/16
        if ((ip >> 16) == ((169 << 8) | 254)) return true; // 169.254.0.0/16 link-local
        return false;
    } else if (sa->sa_family == AF_INET6) {
        const sockaddr_in6* a6 = reinterpret_cast<const sockaddr_in6*>(sa);
        const uint8_t* b = a6->sin6_addr.s6_addr;
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80) return true; // fe80::/10 link-local
        if ((b[0] & 0xfe) == 0xfc) return true;                 // fc00::/7 ULA
        bool loopback = true;                                   // ::1
        for (int i = 0; i < 15; ++i) if (b[i] != 0) { loopback = false; break; }
        if (loopback && b[15] == 1) return true;
        return false;
    }
    return true;
}

bool host_resolves_to_private(const std::string& host) {
    addrinfo hints{}; hints.ai_socktype = SOCK_STREAM; hints.ai_family = AF_UNSPEC;
    addrinfo* res = nullptr;
    int rc = getaddrinfo(host.c_str(), nullptr, &hints, &res);
    if (rc != 0 || !res) return true; 
    bool private_any = true; 
    for (addrinfo* p = res; p; p = p->ai_next) {
        if (!is_private_sockaddr(p->ai_addr)) { private_any = false; break; }
    }
    freeaddrinfo(res);
    return private_any;
}
#endif

} 

namespace {

LinkEmbed::HTMLFetcher::FetchResult DoFetch(const std::string& url, size_t attempt_max_bytes, bool use_range) {
    CURL* curl;
    CURLcode res;
    LinkEmbed::HTMLFetcher::FetchResult result;
    result.status_code = 0;

    const auto& config = LinkEmbed::Config::GetInstance();

    curl = curl_easy_init();
    if (!curl) {
        result.error = "Failed to initialize cURL";
        return result;
    }

    std::string read_buffer;
    const size_t buffer_cap = attempt_max_bytes > 0 ? attempt_max_bytes : static_cast<size_t>(config.max_html_bytes);
    WriteContext write_ctx{ &read_buffer, buffer_cap, false };
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, config.http_user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L); 
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, config.http_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    long allowed_protocols = CURLPROTO_HTTP | CURLPROTO_HTTPS;
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, allowed_protocols);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, allowed_protocols);

    std::string current_url = url;
    long redirects_left = config.http_max_redirects;
    while (true) {
        curl_easy_setopt(curl, CURLOPT_URL, current_url.c_str());
        long allowed_protocols = CURLPROTO_HTTP | CURLPROTO_HTTPS;
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS, allowed_protocols);
        curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, allowed_protocols);

        std::string scheme, host;
        if (!parse_scheme_and_host(current_url, scheme, host)) {
            result.error = "Unsupported or invalid URL";
            break;
        }
#ifndef _WIN32
        if (host_resolves_to_private(host)) {
            result.error = "Blocked private address";
            break;
        }
#endif

        read_buffer.clear();
        struct curl_slist* headers = nullptr;
        if (use_range && buffer_cap > 0) {
            std::string range = "Range: bytes=0-" + std::to_string(buffer_cap - 1);
            headers = curl_slist_append(headers, range.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, nullptr);
        }
        res = curl_easy_perform(curl);
        if (headers) { curl_slist_free_all(headers); headers = nullptr; }
        if (res != CURLE_OK) {
            const char* err = curl_easy_strerror(res);
            if (res == CURLE_WRITE_ERROR || res == CURLE_ABORTED_BY_CALLBACK) {
                result.error = "Content size exceeded limit";
            } else {
                result.error = err ? err : "cURL error";
            }
            break;
        }

        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status_code);
        char* eff = nullptr;
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
        if (eff) result.effective_url = eff;

        if (result.status_code >= 300 && result.status_code < 400 && redirects_left-- > 0) {
            char* redir = nullptr;
            curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redir);
            if (!redir) {
                result.error = "Redirect without Location";
                break;
            }
            std::string next_url = redir;
            std::string s, h;
            if (!parse_scheme_and_host(next_url, s, h)) {
                result.error = "Blocked redirect scheme";
                break;
            }
            current_url = next_url;
            continue;
        }

        if (result.status_code >= 200 && result.status_code < 300) {
            result.content = read_buffer;
            result.truncated = write_ctx.truncated;
        } else if (result.status_code >= 300 && result.status_code < 400) {
            result.error = "Too many redirects";
        } else {
            result.error = std::string("HTTP status code: ") + std::to_string(result.status_code);
        }
        break;
    }

    curl_easy_cleanup(curl);
    return result;
}

} // anonymous namespace

namespace LinkEmbed {

HTMLFetcher::FetchResult HTMLFetcher::Fetch(const std::string& url) {
    const auto& config = Config::GetInstance();
    return DoFetch(url, static_cast<size_t>(config.max_html_bytes), false);
}

HTMLFetcher::FetchResult HTMLFetcher::Fetch(const std::string& url, size_t attempt_max_bytes, bool use_range) {
    return DoFetch(url, attempt_max_bytes, use_range);
}

}
