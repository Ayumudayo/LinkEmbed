#include "LinkEmbedHandler.hpp"
#include "network/HTMLFetcher.hpp"
#include "parser/MetadataParser.hpp"
#include "utils/Logger.hpp"
#include "../../config/Config.hpp"
#include <regex>
#include <future>
#include <dpp/dpp.h>

namespace LinkEmbed {

LinkEmbedHandler::LinkEmbedHandler(dpp::cluster& bot, ThreadPool& pool, RateLimiter& limiter, MetadataCache& cache, JobScheduler& scheduler)
    : bot(bot), thread_pool(pool), rate_limiter(limiter), metadata_cache(cache), job_scheduler(scheduler) {}

void LinkEmbedHandler::OnMessageCreate(const dpp::message_create_t& event) {
    if (event.msg.author.is_bot()) return;

    auto urls = ExtractUrls(event.msg.content);
    if (urls.empty()) return;

    // If Discord already made an embed, do nothing.
    if (!event.msg.embeds.empty()) {
        Logger::Log(LogLevel::Debug, "Ignoring message with existing embeds: " + std::to_string(event.msg.id));
        return;
    }

    for (const auto& url : urls) {
        Logger::Log(LogLevel::Info, "Scheduling job for URL: " + url);
        const auto channel_id = event.msg.channel_id;
        const auto message_id = event.msg.id;
        job_scheduler.Schedule(message_id, Config::GetInstance().embed_delay_seconds, [this, url, channel_id, message_id]() {
            ProcessUrl(url, channel_id, message_id);
        });
    }
}

void LinkEmbedHandler::OnMessageUpdate(const dpp::message_update_t& event) {
    if (event.msg.author.is_bot()) return;

    // If Discord added an embed after our initial check, cancel our job and delete our embed if posted.
    if (!event.msg.embeds.empty()) {
        Logger::Log(LogLevel::Info, "Message updated with embed, cancelling job for: " + std::to_string(event.msg.id));
        job_scheduler.Cancel(event.msg.id);
        MaybeDeleteBotEmbed(event.msg.id, event.msg.channel_id);
    }
}

void LinkEmbedHandler::MaybeDeleteBotEmbed(dpp::snowflake original_message_id, dpp::snowflake channel_id) {
    dpp::snowflake bot_msg_id{};
    {
        std::lock_guard<std::mutex> lk(bot_embeds_mutex_);
        auto it = bot_embeds_.find(original_message_id);
        if (it != bot_embeds_.end()) {
            bot_msg_id = it->second;
            bot_embeds_.erase(it);
        }
    }
    if (bot_msg_id) {
        bot.message_delete(bot_msg_id, channel_id);
        Logger::Log(LogLevel::Info, "Deleted bot embed because Discord added its own for: " + std::to_string(original_message_id));
    }
}

bool LinkEmbedHandler::HasDiscordEmbed(dpp::snowflake channel_id, dpp::snowflake message_id, int timeout_ms) {
    // Wrap in a shared_ptr so the std::function callback can be copied.
    auto pp = std::make_shared<std::promise<bool>>();
    auto fut = pp->get_future();
    // Note that the dpp::cluster::message_get signature is (message_id, channel_id).
    bot.message_get(message_id, channel_id, [pp](const dpp::confirmation_callback_t& cc) mutable {
        bool has = false;
        if (!cc.is_error()) {
            try {
                const dpp::message& m = std::get<dpp::message>(cc.value);
                has = !m.embeds.empty();
            } catch (...) {}
        }
        pp->set_value(has);
    });
    if (timeout_ms <= 0) timeout_ms = 1500;
    if (fut.wait_for(std::chrono::milliseconds(timeout_ms)) == std::future_status::ready) {
        return fut.get();
    }
    return false;
}

void LinkEmbedHandler::ProcessUrl(const std::string& url, dpp::snowflake channel_id, dpp::snowflake message_id) {
    thread_pool.enqueue([this, url, channel_id, message_id] {
        if (!rate_limiter.TryAcquire()) {
            Logger::Log(LogLevel::Warn, "Rate limit exceeded. Dropping request for URL: " + url);
            return;
        }

        Logger::Log(LogLevel::Info, "Processing URL: " + url);

        // 1. Check cache
        if (auto cached_meta = metadata_cache.Get(url)) {
            Logger::Log(LogLevel::Info, "Cache hit for URL: " + url);
            if (HasDiscordEmbed(channel_id, message_id, 1500)) {
                Logger::Log(LogLevel::Info, "Discord already attached an embed. Skipping bot embed for: " + url);
                return;
            }

            dpp::embed msg_embed;
            msg_embed.set_title(cached_meta->title);
            msg_embed.set_url(url);
            if (!cached_meta->description.empty()) {
                msg_embed.set_description(cached_meta->description);
            }
            if (!cached_meta->site_name.empty()) {
                msg_embed.set_footer(dpp::embed_footer().set_text(cached_meta->site_name));
            }
            if (!cached_meta->image_url.empty()) {
                msg_embed.set_thumbnail(cached_meta->image_url);
            }
            bot.message_create(dpp::message(channel_id, msg_embed), [this, message_id](const dpp::confirmation_callback_t& cc){
                if (cc.is_error()) return;
                try {
                    const dpp::message& m = std::get<dpp::message>(cc.value);
                    std::lock_guard<std::mutex> lk(bot_embeds_mutex_);
                    bot_embeds_[message_id] = m.id;
                } catch (...) {}
            });
            return;
        }

        // 2. Fetch HTML (Range-based progressive fetching)
        Logger::Log(LogLevel::Debug, "Cache miss. Fetching URL: " + url);
        const auto& cfg = Config::GetInstance();
        size_t cap = static_cast<size_t>(cfg.max_html_bytes);
        size_t attempt = std::min(static_cast<size_t>(cfg.html_initial_range_bytes), cap);
        if (attempt == 0) attempt = cap;
        std::optional<Metadata> metadata;
        HTMLFetcher::FetchResult fetch_result;
        while (true) {
            fetch_result = HTMLFetcher::Fetch(url, attempt, true);
            if (!fetch_result.error.empty()) {
                Logger::Log(LogLevel::Error, "Failed to fetch " + url + ": " + fetch_result.error);
                return;
            }
            metadata = MetadataParser::Parse(fetch_result.content);
            if (metadata) {
                break;
            }
            if (attempt >= cap) {
                Logger::Log(LogLevel::Warn, "Could not parse metadata within max bytes from: " + url);
                return;
            }
            size_t next = static_cast<size_t>(attempt * std::max(1.0, cfg.html_range_growth_factor));
            attempt = std::min(next > attempt ? next : attempt + 1, cap);
            Logger::Log(LogLevel::Debug, "Metadata incomplete, increasing range to " + std::to_string(attempt) + " bytes for URL: " + url);
        }

        // 4. Cache the result (cache for both original and final URLs)
        metadata_cache.Put(url, *metadata);
        if (!fetch_result.effective_url.empty() && fetch_result.effective_url != url) {
            metadata_cache.Put(fetch_result.effective_url, *metadata);
        }

        // 5. Send embed (re-check for Discord embed right before sending)
        if (HasDiscordEmbed(channel_id, message_id, 1500)) {
            Logger::Log(LogLevel::Info, "Discord already attached an embed. Skipping bot embed for: " + url);
            return;
        }

        // Closer to Discord style: title+description+thumbnail
        dpp::embed msg_embed;
        msg_embed.set_title(metadata->title);
        msg_embed.set_url(url);
        if (!metadata->description.empty()) {
            msg_embed.set_description(metadata->description);
        }
        if (!metadata->site_name.empty()) {
            msg_embed.set_footer(dpp::embed_footer().set_text(metadata->site_name));
        }
        if (!metadata->image_url.empty()) {
            msg_embed.set_thumbnail(metadata->image_url);
        }

        bot.message_create(dpp::message(channel_id, msg_embed), [this, message_id](const dpp::confirmation_callback_t& cc){
            if (cc.is_error()) return;
            try {
                const dpp::message& m = std::get<dpp::message>(cc.value);
                std::lock_guard<std::mutex> lk(bot_embeds_mutex_);
                bot_embeds_[message_id] = m.id;
            } catch (...) {}
        });
    });
}

std::vector<std::string> LinkEmbedHandler::ExtractUrls(const std::string& text) {
    std::vector<std::string> urls;
    // A more robust regex might be needed, but this is a good start.
    // The existing escaped string caused a syntax error due to extra quotes.
    // Replace with a raw string literal to ensure readability and safety.
    std::regex url_regex(R"((https?://[^\s<>"']+))");
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), url_regex);
    auto words_end = std::sregex_iterator();


    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        urls.push_back(i->str());
    }
    return urls;
}

}
