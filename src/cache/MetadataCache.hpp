#pragma once
#include <string>
#include <optional>
#include <list>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include "../parser/MetadataParser.hpp"
#include "../interfaces/IMetadataCache.hpp"

namespace LinkEmbed {
    class MetadataCache : public IMetadataCache {
    public:
        MetadataCache(size_t max_size, int ttl_minutes, size_t max_bytes);
        std::optional<Metadata> Get(const std::string& url) override;
        void Put(const std::string& url, const Metadata& metadata) override;

    private:
        struct CacheEntry {
            std::string url;
            Metadata metadata;
            std::chrono::steady_clock::time_point expiry_time;
            size_t payload_bytes;
        };

        size_t EstimateEntrySize(const std::string& url, const Metadata& metadata) const;
        void ShrinkToFit(size_t incoming_bytes);

        // NOTE: Expiry is enforced lazily within Get/Put. No explicit sweep is required.

        size_t max_size_;
        size_t max_bytes_;
        size_t current_bytes_ = 0;
        std::chrono::minutes ttl_;
        std::list<CacheEntry> cache_list_;
        std::unordered_map<std::string, decltype(cache_list_.begin())> cache_map_;
        std::mutex cache_mutex_;
    };
}
