#include "UrlUtil.hpp"
#include "../../config/Config.hpp"
#include <regex>
#include <algorithm>
#include <cstring>

namespace LinkEmbed {
namespace UrlUtil {

static inline std::string CleanUrl(std::string s) {
    auto rtrim_any = [](std::string& x, const std::string& chars) {
        while (!x.empty() && chars.find(x.back()) != std::string::npos) x.pop_back();
    };

    // 1) Strip common trailing punctuation
    rtrim_any(s, ")],.!?;:");

    // 2) Fix parenthesis balance: drop extra trailing ')'
    auto count_char = [](const std::string& x, char c){ return static_cast<int>(std::count(x.begin(), x.end(), c)); };
    while (!s.empty() && count_char(s, ')') > count_char(s, '(') && s.back() == ')') {
        s.pop_back();
    }

    return s;
}

std::vector<std::string> ExtractUrls(const std::string& text) {
    std::vector<std::string> urls;
    std::regex url_regex(R"((https?://[^\s<>"']+))");
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), url_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        auto u = CleanUrl(i->str());
        if (!u.empty()) urls.push_back(std::move(u));
    }
    return urls;
}

static inline bool starts_with(const std::string& s, const char* pfx) {
    size_t n = strlen(pfx);
    return s.size() >= n && memcmp(s.data(), pfx, n) == 0;
}

static inline std::string get_scheme_host(const std::string& url) {
    // Very small parser: scheme://host[:port]
    auto pos_scheme = url.find("://");
    if (pos_scheme == std::string::npos) return {};
    auto start_host = pos_scheme + 3;
    auto pos_end = url.find_first_of("/\\?#", start_host);
    if (pos_end == std::string::npos) pos_end = url.size();
    return url.substr(0, pos_end);
}

static inline std::string get_base_dir(const std::string& url) {
    // Returns scheme://host[:port]/path/dir (without filename)
    auto scheme_host = get_scheme_host(url);
    if (scheme_host.empty()) return {};
    std::string rest = url.substr(scheme_host.size());
    // rest starts with '/' or empty
    auto qpos = rest.find_first_of("?#");
    if (qpos != std::string::npos) rest = rest.substr(0, qpos);
    // remove filename segment
    if (!rest.empty()) {
        if (rest.back() != '/') {
            auto slash = rest.find_last_of('/');
            if (slash != std::string::npos) rest = rest.substr(0, slash + 1);
            else rest = "/";
        }
    } else {
        rest = "/";
    }
    return scheme_host + rest;
}

std::string ResolveAgainst(const std::string& base_url, const std::string& candidate) {
    if (candidate.empty()) return candidate;
    if (starts_with(candidate, "http://") || starts_with(candidate, "https://")) return candidate;
    if (starts_with(candidate, "//")) return std::string("https:") + candidate;

    auto scheme_host = get_scheme_host(base_url);
    if (scheme_host.empty()) return candidate; // fallback

    if (!candidate.empty() && candidate[0] == '/') {
        return scheme_host + candidate;
    }

    auto base_dir = get_base_dir(base_url);
    if (base_dir.empty()) return candidate;
    // Ensure single slash join
    if (!base_dir.empty() && base_dir.back() != '/') {
        return base_dir + "/" + candidate;
    }
    return base_dir + candidate;
}

// Percent-encode URL (simple RFC 3986: encode everything except unreserved)
static inline std::string UrlEncodeAll(const std::string& s) {
    auto is_unreserved = [](unsigned char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
    };
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (is_unreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0xF]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

// Simple substring matching: return true if host contains any of patterns
static inline bool HostMatchesAny(const std::string& host, const std::vector<std::string>& patterns) {
    for (const auto& p : patterns) {
        if (p.empty()) continue;
        if (host.find(p) != std::string::npos) return true;
    }
    return false;
}

// Extract host in lowercase
static inline std::string extract_host_lower(const std::string& url) {
    auto scheme_end = url.find("://");
    if (scheme_end == std::string::npos) return {};
    auto start = scheme_end + 3;
    auto end = url.find_first_of("/\\?#", start);
    if (end == std::string::npos) end = url.size();
    std::string host = url.substr(start, end - start);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c){ return (char)std::tolower(c); });
    return host;
}

std::string ProxyImageIfNeeded(const std::string& image_url) {
    if (image_url.empty()) return image_url;
    const auto& cfg = Config::GetInstance();
    if (!cfg.image_proxy_enabled) return image_url;

    std::string host = extract_host_lower(image_url);
    if (host.empty()) return image_url;

    // Match if host contains any configured pattern or path contains viewimage.php
    bool matched = HostMatchesAny(host, cfg.image_proxy_hosts) || (image_url.find("viewimage.php") != std::string::npos);
    if (!matched) return image_url;

    // Build proxy URL: <base>?url=<ENCODED_ORIGIN>[&extra]
    std::string base = cfg.image_proxy_base.empty() ? std::string("https://images.weserv.nl") : cfg.image_proxy_base;
    // Trim trailing slash in base
    if (!base.empty() && base.back() == '/') base.pop_back();
    std::string encoded = UrlEncodeAll(image_url);
    std::string proxied = base + "?url=" + encoded;
    if (!cfg.image_proxy_query.empty()) {
        // We already added ?url=, append extra params with &
        proxied += "&" + cfg.image_proxy_query;
    }
    return proxied;
}

}
}
