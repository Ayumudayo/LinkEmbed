#include <dpp/dpp.h>
#include <iostream>
#include <cstdlib>
#include <curl/curl.h>
#include <filesystem>
#include "../config/Config.hpp"
#include "core/LinkEmbedHandler.hpp"
#include "utils/Logger.hpp"
#include "utils/ThreadPool.hpp"
#include "utils/RateLimiter.hpp"
#include "cache/MetadataCache.hpp"
#include "core/JobScheduler.hpp"

int main(int argc, char* argv[]) {
    if (argc == 0 || argv[0] == nullptr) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "Cannot determine executable path.");
        return 1;
    }
    std::filesystem::path exe_path(argv[0]);
    std::filesystem::path exe_dir = exe_path.parent_path();
    std::filesystem::path config_path = exe_dir / "config" / "config.json";
    const std::string config_path_str = config_path.string();

    // Initialize global resources
    curl_global_init(CURL_GLOBAL_ALL);

    // Load Config
    try {
        LinkEmbed::Config::GetInstance().Load(config_path_str);
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Info, "Configuration loaded from: " + config_path_str);
    } catch (const std::runtime_error& e) {
        std::string error_message = e.what();
        if (error_message.find("Could not open config file") != std::string::npos) {
            LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Warn, "config.json not found. Creating a default one at: " + config_path_str);
            try {
                LinkEmbed::Config::GetInstance().CreateDefault(config_path_str);
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

    // Determine thread pool size
    const unsigned int hardware_cores = std::thread::hardware_concurrency();
    const unsigned int default_threads = std::max(1u, hardware_cores / 2);
    unsigned int worker_threads = 0;

    if (config.max_concurrency == 0) {
        worker_threads = default_threads;
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Info, "max_concurrency is 0, defaulting to half of system cores: " + std::to_string(worker_threads));
    } else if (config.max_concurrency < 0 || config.max_concurrency > hardware_cores) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "Configured max_concurrency (" + std::to_string(config.max_concurrency) + ") is invalid. It must be between 1 and the number of system cores (" + std::to_string(hardware_cores) + ").");
        return 1;
    } else {
        worker_threads = config.max_concurrency;
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Info, "Using configured max_concurrency: " + std::to_string(worker_threads));
    }

    // Get Bot Token
    if (config.bot_token == "YOUR_BOT_TOKEN_HERE" || config.bot_token.empty()) {
        LinkEmbed::Logger::Log(LinkEmbed::LogLevel::Error, "Please set your bot_token in " + config_path_str);
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
    LinkEmbed::ThreadPool thread_pool(worker_threads);
    LinkEmbed::HTMLFetcher html_fetcher;
    LinkEmbed::RateLimiter rate_limiter(config.rate_per_sec);
    LinkEmbed::MetadataCache metadata_cache(config.cache_max_size, config.cache_ttl_minutes);
    LinkEmbed::JobScheduler job_scheduler(thread_pool);
    LinkEmbed::LinkEmbedHandler handler(bot, thread_pool, html_fetcher, rate_limiter, metadata_cache, job_scheduler);

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
