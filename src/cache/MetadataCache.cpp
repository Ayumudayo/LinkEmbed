#include "MetadataCache.hpp"

namespace LinkEmbed {

MetadataCache::MetadataCache(size_t max_size, int ttl_minutes) 
    : max_size_(max_size), ttl_(ttl_minutes) {}

std::optional<Metadata> MetadataCache::Get(const std::string& url) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_map_.find(url);

    if (it == cache_map_.end()) {
        return std::nullopt; // Not found
    }

    // Check for expiry
    if (std::chrono::steady_clock::now() >= it->second->expiry_time) {
        cache_list_.erase(it->second);
        cache_map_.erase(it);
        return std::nullopt;
    }

    // Move the accessed element to the front of the list (most recently used)
    cache_list_.splice(cache_list_.begin(), cache_list_, it->second);
    return it->second->metadata;
}

void MetadataCache::Put(const std::string& url, const Metadata& metadata) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_map_.find(url);

    // If entry already exists, remove it to update its position and value
    if (it != cache_map_.end()) {
        cache_list_.erase(it->second);
        cache_map_.erase(it);
    }

    // If cache is full, remove the least recently used item
    if (cache_map_.size() >= max_size_) {
        if (!cache_list_.empty()) {
            const auto& lru_entry = cache_list_.back();
            cache_map_.erase(lru_entry.url);
            cache_list_.pop_back();
        }
    }

    // Add the new entry to the front of the list
    auto expiry_time = std::chrono::steady_clock::now() + ttl_;
    cache_list_.push_front({url, metadata, expiry_time});
    cache_map_[url] = cache_list_.begin();
}

}