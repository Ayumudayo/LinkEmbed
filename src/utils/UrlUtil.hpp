#pragma once
#include <string>
#include <vector>

namespace LinkEmbed {
namespace UrlUtil {

// Extract absolute URLs from text and sanitize trailing punctuation
std::vector<std::string> ExtractUrls(const std::string& text);

// Resolve possibly-relative or protocol-relative URL against a base URL (page URL).
// Rules:
// - If candidate starts with http:// or https://, return as-is.
// - If candidate starts with //, prefix https:.
// - If candidate starts with /, return base_scheme://base_host + candidate.
// - Otherwise, append to base directory: base_scheme://base_host/base_dir/ + candidate.
// On parse failure, returns candidate unchanged.
std::string ResolveAgainst(const std::string& base_url, const std::string& candidate);

// Wrap image URLs for specific hosts (e.g., dcinside) with a public proxy to bypass Referer restrictions.
// Respects Config fields: image_proxy_enabled/image_proxy_base/image_proxy_query/image_proxy_hosts.
// Returns the original URL if proxying is disabled or the host doesn't match.
std::string ProxyImageIfNeeded(const std::string& image_url);

}
}
