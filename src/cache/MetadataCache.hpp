#pragma once
#include <string>
#include <optional>
#include <list>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include "../parser/MetadataParser.hpp"

namespace LinkEmbed {
    class MetadataCache {
    public:
        MetadataCache(size_t max_size, int ttl_minutes);
        std::optional<Metadata> Get(const std::string& url);
        void Put(const std::string& url, const Metadata& metadata);

    private:
        struct CacheEntry {
            std::string url;
            Metadata metadata;
            std::chrono::steady_clock::time_point expiry_time;
        };

        // NOTE: Expiry is enforced lazily within Get/Put. No explicit sweep is required.

        size_t max_size_;
        std::chrono::minutes ttl_;
        std::list<CacheEntry> cache_list_;
        std::unordered_map<std::string, decltype(cache_list_.begin())> cache_map_;
        std::mutex cache_mutex_;
    };
}
