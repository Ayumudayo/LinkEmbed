#include "MetadataCache.hpp"

namespace LinkEmbed {

MetadataCache::MetadataCache(size_t max_size, int ttl_minutes, size_t max_bytes) 
    : max_size_(max_size), max_bytes_(max_bytes), ttl_(ttl_minutes) {}

size_t MetadataCache::EstimateEntrySize(const std::string& url, const Metadata& metadata) const {
    size_t total = url.size();
    total += metadata.title.size();
    total += metadata.image_url.size();
    total += metadata.description.size();
    total += metadata.site_name.size();
    return total;
}

void MetadataCache::ShrinkToFit(size_t incoming_bytes) {
    const bool enforce_entry_limit = max_size_ > 0;
    const bool enforce_byte_limit = max_bytes_ > 0;

    while (!cache_list_.empty() && ((enforce_entry_limit && cache_map_.size() >= max_size_) || (enforce_byte_limit && current_bytes_ + incoming_bytes > max_bytes_))) {
        const auto& lru_entry = cache_list_.back();
        current_bytes_ = current_bytes_ >= lru_entry.payload_bytes ? current_bytes_ - lru_entry.payload_bytes : 0;
        cache_map_.erase(lru_entry.url);
        cache_list_.pop_back();
    }
}

std::optional<Metadata> MetadataCache::Get(const std::string& url) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = cache_map_.find(url);

    if (it == cache_map_.end()) {
        return std::nullopt; // Not found
    }

    // Check for expiry
    if (std::chrono::steady_clock::now() >= it->second->expiry_time) {
        current_bytes_ = current_bytes_ >= it->second->payload_bytes ? current_bytes_ - it->second->payload_bytes : 0;
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
        current_bytes_ = current_bytes_ >= it->second->payload_bytes ? current_bytes_ - it->second->payload_bytes : 0;
        cache_list_.erase(it->second);
        cache_map_.erase(it);
    }

    const size_t entry_size = EstimateEntrySize(url, metadata);
    if (max_bytes_ > 0 && entry_size > max_bytes_) {
        return; // Skip caching entries that exceed the configured byte budget
    }

    ShrinkToFit(entry_size);

    // Add the new entry to the front of the list
    auto expiry_time = std::chrono::steady_clock::now() + ttl_;
    cache_list_.push_front({url, metadata, expiry_time, entry_size});
    cache_map_[url] = cache_list_.begin();
    current_bytes_ += entry_size;
}

}
