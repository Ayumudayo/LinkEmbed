#pragma once
#include <dpp/dpp.h>
#include "JobScheduler.hpp"
#include "../utils/ThreadPool.hpp"
#include "../utils/RateLimiter.hpp"
#include "../cache/MetadataCache.hpp"
#include <unordered_map>
#include <mutex>

namespace LinkEmbed {
    class LinkEmbedHandler {
    public:
        LinkEmbedHandler(dpp::cluster& bot, ThreadPool& pool, RateLimiter& limiter, MetadataCache& cache, JobScheduler& scheduler);

        void OnMessageCreate(const dpp::message_create_t& event);
        void OnMessageUpdate(const dpp::message_update_t& event);

    private:
        void ProcessUrl(const std::string& url, dpp::snowflake channel_id, dpp::snowflake message_id);
        std::vector<std::string> ExtractUrls(const std::string& text);
        void MaybeDeleteBotEmbed(dpp::snowflake original_message_id, dpp::snowflake channel_id);
        bool HasDiscordEmbed(dpp::snowflake channel_id, dpp::snowflake message_id, int timeout_ms);

        dpp::cluster& bot;
        ThreadPool& thread_pool;
        RateLimiter& rate_limiter;
        MetadataCache& metadata_cache;
        JobScheduler& job_scheduler;
        std::unordered_map<dpp::snowflake, dpp::snowflake> bot_embeds_;
        std::mutex bot_embeds_mutex_;
    };
}
