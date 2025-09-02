
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
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Info, "Configuration loaded.");
    } catch (const std::runtime_error& e) {
        std::string error_message = e.what();
        if (error_message.find("Could not open config file") != std::string::npos) {
            LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Warn, "config.json not found. Creating a default one.");
            try {
                LinkEmbed::Config::GetInstance().CreateDefault("config/config.json");
                LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Info, "Default config.json created. Please review it, set your bot token, and restart the bot.");
                curl_global_cleanup();
                return 0; 
            } catch (const std::exception& create_e) {
                LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "Failed to create default config: " + std::string(create_e.what()));
                curl_global_cleanup();
                return 1;
            }
        } else {
            LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "Failed to load config: " + error_message);
            curl_global_cleanup();
            return 1;
        }
    }
    const auto& config = LinkEmbed::Config::GetInstance();

    // Get Bot Token
    if (config.bot_token == "YOUR_BOT_TOKEN_HERE") {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "Please set your bot_token in config/config.json");
        curl_global_cleanup();
        return 1;
    }


    // Setup Bot
    dpp::cluster bot(config.bot_token, dpp::i_default_intents | dpp::i_message_content);
    bot.on_log([](const dpp::log_t& event) {
        LinkEmbed::LogLevel level = LinkEmbed::LogLevel::Debug;
        if (event.severity > dpp::ll_debug) {
            switch (event.severity) {
                case dpp::ll_info:    level = LinkEmbed::LogLevel::Info; break;
                case dpp::ll_warning: level = LinkEmbed::LogLevel::Warn; break;
                case dpp::ll_error:
                case dpp::ll_critical:level = LinkEmbed::LogLevel::Error; break;
                default:              level = LinkEmbed::LogLevel::Debug; break;
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
    // If the original message is deleted, clean up the embed created by the bot.
    bot.on_message_delete([&handler](const dpp::message_delete_t& event) {
        // Deletion is handled only in message_update, so no simple wrapping is needed for the handler to have direct access from channel events.
        // No additional action is required here, but room for expansion is left.
    });

    bot.on_ready([&bot](const dpp::ready_t& event) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Info, "Bot is ready! Logged in as " + bot.me.username);
    });

    // Start Bot
    try {
        bot.start(dpp::st_wait);
    } catch (const dpp::exception& e) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "DPP Exception: " + std::string(e.what()));
        curl_global_cleanup();
        return 1;
    }

    // Cleanup global resources
    curl_global_cleanup();
    return 0;
}
