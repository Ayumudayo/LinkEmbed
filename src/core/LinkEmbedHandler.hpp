#pragma once
#include <dpp/dpp.h>
#include "JobScheduler.hpp"
#include "../utils/ThreadPool.hpp"
#include "../interfaces/IRateLimiter.hpp"
#include "../interfaces/IMetadataCache.hpp"
#include "../interfaces/IHTMLFetcher.hpp"
#include "../interfaces/IMetadataParser.hpp"
#include <unordered_map>
#include <mutex>

namespace LinkEmbed {
    class LinkEmbedHandler {
    public:
        LinkEmbedHandler(dpp::cluster& bot, ThreadPool& pool, IHTMLFetcher& fetcher, IRateLimiter& limiter, IMetadataCache& cache, IMetadataParser& parser, JobScheduler& scheduler);

        void OnMessageCreate(const dpp::message_create_t& event);
        void OnMessageUpdate(const dpp::message_update_t& event);

    private:
        struct ProcessContext;
        void ProcessUrl(std::shared_ptr<ProcessContext> ctx);
        void OnFetchComplete(FetchResult result);
        void SendEmbed(std::shared_ptr<ProcessContext> ctx, const Metadata& metadata);

        std::vector<std::string> ExtractUrls(const std::string& text);
        void MaybeDeleteBotEmbed(dpp::snowflake original_message_id, dpp::snowflake channel_id);
        bool HasDiscordEmbed(dpp::snowflake channel_id, dpp::snowflake message_id, int timeout_ms);

        dpp::cluster& bot;
        ThreadPool& thread_pool;
        IHTMLFetcher& html_fetcher_;
        IRateLimiter& rate_limiter;
        IMetadataCache& metadata_cache;
        IMetadataParser& parser_;
        JobScheduler& job_scheduler;
        std::unordered_map<dpp::snowflake, dpp::snowflake> bot_embeds_;
        std::mutex bot_embeds_mutex_;
    };
}
