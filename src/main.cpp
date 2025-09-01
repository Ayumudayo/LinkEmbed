
#include <dpp/dpp.h>
#include <iostream>
#include <cstdlib>
#include <curl/curl.h>
#include "../config/Config.hpp"
#include "core/LinkEmbedHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/ThreadPool.hpp"
#include "utils/RateLimiter.hpp"
#include "cache/MetadataCache.hpp"
#include "core/JobScheduler.hpp"

int main() {
    // Initialize global resources
    curl_global_init(CURL_GLOBAL_ALL);

    // Load Config
    try {
        LinkEmbed::Config::GetInstance().Load("config/config.json");
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::INFO, "Configuration loaded.");
    } catch (const std::runtime_error& e) {
        std::string error_message = e.what();
        if (error_message.find("Could not open config file") != std::string::npos) {
            LinkEmbed::Logger::Log(LinkEmbed::LogLevel::WARN, "config.json not found. Creating a default one.");
            try {
                LinkEmbed::Config::GetInstance().CreateDefault("config/config.json");
                LinkEmbed::Logger::Log(LinkEmbed::LogLevel::INFO, "Default config.json created. Please review it, set your bot token, and restart the bot.");
                curl_global_cleanup();
                return 0; 
            } catch (const std::exception& create_e) {
                LinkEmbed::Logger::Log(LinkEmbed::LogLevel::LOG_ERROR, "Failed to create default config: " + std::string(create_e.what()));
                curl_global_cleanup();
                return 1;
            }
        } else {
            LinkEmbed::Logger::Log(LinkEmbed::LogLevel::LOG_ERROR, "Failed to load config: " + error_message);
            curl_global_cleanup();
            return 1;
        }
    }
    const auto& config = LinkEmbed::Config::GetInstance();

    // Get Bot Token
    if (config.bot_token == "YOUR_BOT_TOKEN_HERE") {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::LOG_ERROR, "Please set your bot_token in config/config.json");
        curl_global_cleanup();
        return 1;
    }


    // Setup Bot
    dpp::cluster bot(config.bot_token, dpp::i_default_intents | dpp::i_message_content);
    bot.on_log([](const dpp::log_t& event) {
        LinkEmbed::LogLevel level = LinkEmbed::LogLevel::DEBUG;
        if (event.severity > dpp::ll_debug) {
            switch (event.severity) {
                case dpp::ll_info:    level = LinkEmbed::LogLevel::INFO; break;
                case dpp::ll_warning: level = LinkEmbed::LogLevel::WARN; break;
                case dpp::ll_error:
                case dpp::ll_critical:level = LinkEmbed::LogLevel::LOG_ERROR; break;
                default:              level = LinkEmbed::LogLevel::DEBUG; break;
            }
        }
        LinkEmbed::Logger::Log(level, "[DPP] " + event.message);
    });

    // Setup Core Components
    LinkEmbed::ThreadPool thread_pool(config.max_concurrency);
    LinkEmbed::RateLimiter rate_limiter(config.rate_per_sec);
    LinkEmbed::MetadataCache metadata_cache(config.cache_ttl_minutes);
    LinkEmbed::JobScheduler job_scheduler(thread_pool);
    LinkEmbed::LinkEmbedHandler handler(bot, thread_pool, rate_limiter, metadata_cache, job_scheduler);

    // Register Event Handlers
    bot.on_message_create([&handler](const dpp::message_create_t& event) {
        handler.OnMessageCreate(event);
    });

    bot.on_message_update([&handler](const dpp::message_update_t& event) {
        handler.OnMessageUpdate(event);
    });
    // 원본 메시지가 삭제되면, 봇이 만든 임베드도 정리
    bot.on_message_delete([&handler](const dpp::message_delete_t& event) {
        // handler가 채널 이벤트에서 직접 접근할 수 있도록 간단한 래핑 필요 없이, 메시지 업데이트에서만 삭제 처리.
        // 여기서는 추가 작업 없이도 무방하지만, 확장 여지를 남겨둠.
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::INFO, "Bot is ready! Logged in as " + bot.me.username);
    });

    // Start Bot
    try {
        bot.start(dpp::st_wait);
    } catch (const dpp::exception& e) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::LOG_ERROR, "DPP Exception: " + std::string(e.what()));
        curl_global_cleanup();
        return 1;
    }

    // Cleanup global resources
    curl_global_cleanup();
    return 0;
}
