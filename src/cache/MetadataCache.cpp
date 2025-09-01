
#include "MetadataCache.hpp"

namespace LinkEmbed {

MetadataCache::MetadataCache(int ttl_minutes) : ttl(ttl_minutes) {}

std::optional<Metadata> MetadataCache::Get(const std::string& url) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = cache.find(url);
    if (it != cache.end()) {
        if (std::chrono::steady_clock::now() < it->second.expiry_time) {
            return it->second.metadata;
        } else {
            cache.erase(it);
        }
    }
    return std::nullopt;
}

void MetadataCache::Put(const std::string& url, const Metadata& metadata) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto expiry_time = std::chrono::steady_clock::now() + ttl;
    cache[url] = {metadata, expiry_time};
}

}
