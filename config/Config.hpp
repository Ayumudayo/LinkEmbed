#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace LinkEmbed {
    struct Config {
        int embed_delay_seconds = 5;
        int cache_ttl_minutes = 10;
        long http_timeout_ms = 4000;
        long http_max_redirects = 5;
        std::string http_user_agent = "LinkEbdBot/1.0";
        int max_concurrency = 4;
        double rate_per_sec = 2.0;
        size_t max_html_bytes = 8388608; // 8MB 기본 상향
        size_t html_initial_range_bytes = 524288; // 512KB
        double html_range_growth_factor = 2.0; // 단계 확대 배수
        std::string bot_token = "YOUR_BOT_TOKEN_HERE";

        static Config& GetInstance() {
            static Config instance;
            return instance;
        }

        void Load(const std::string& path);
        void CreateDefault(const std::string& path);
    };
}