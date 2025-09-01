#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include "../parser/MetadataParser.hpp"

namespace LinkEmbed {
    class MetadataCache {
    public:
        struct CacheEntry {
            Metadata metadata;
            std::chrono::steady_clock::time_point expiry_time;
        };

        MetadataCache(int ttl_minutes);
        std::optional<Metadata> Get(const std::string& url);
        void Put(const std::string& url, const Metadata& metadata);

    private:
        std::unordered_map<std::string, CacheEntry> cache;
        std::chrono::minutes ttl;
        std::mutex cache_mutex;
    };
}