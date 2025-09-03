#include "LinkEmbedHandler.hpp"
#include "parser/MetadataParser.hpp"
#include "utils/Logger.hpp"
#include "../../config/Config.hpp"
#include <regex>
#include <future>
#include <dpp/dpp.h>

namespace LinkEmbed {

// Holds all the state for processing a single URL.
struct LinkEmbedHandler::ProcessContext {
    std::string url;
    dpp::snowflake channel_id;
    dpp::snowflake message_id;
    size_t fetch_attempt_bytes = 0;
};

LinkEmbedHandler::LinkEmbedHandler(dpp::cluster& bot, ThreadPool& pool, HTMLFetcher& fetcher, RateLimiter& limiter, MetadataCache& cache, JobScheduler& scheduler)
    : bot(bot), thread_pool(pool), html_fetcher_(fetcher), rate_limiter(limiter), metadata_cache(cache), job_scheduler(scheduler) {}

void LinkEmbedHandler::OnMessageCreate(const dpp::message_create_t& event) {
    if (event.msg.author.is_bot()) return;

    auto urls = ExtractUrls(event.msg.content);
    if (urls.empty()) return;

    if (!event.msg.embeds.empty()) {
        Logger::Log(LogLevel::Debug, "Ignoring message with existing embeds: " + std::to_string(event.msg.id));
        return;
    }

    for (const auto& url : urls) {
        Logger::Log(LogLevel::Info, "Scheduling job for URL: " + url);
        auto ctx = std::make_shared<ProcessContext>();
        ctx->url = url;
        ctx->channel_id = event.msg.channel_id;
        ctx->message_id = event.msg.id;

        job_scheduler.Schedule(ctx->message_id, Config::GetInstance().embed_delay_seconds, [this, ctx]() {
            ProcessUrl(ctx);
        });
    }
}

void LinkEmbedHandler::OnMessageUpdate(const dpp::message_update_t& event) {
    if (event.msg.author.is_bot()) return;

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

void LinkEmbedHandler::ProcessUrl(std::shared_ptr<ProcessContext> ctx) {
    if (!rate_limiter.TryAcquire()) {
        Logger::Log(LogLevel::Warn, "Rate limit exceeded. Dropping request for URL: " + ctx->url);
        return;
    }

    Logger::Log(LogLevel::Info, "Processing URL: " + ctx->url);

    // 1. Check cache
    if (auto cached_meta = metadata_cache.Get(ctx->url)) {
        Logger::Log(LogLevel::Info, "Cache hit for URL: " + ctx->url);
        SendEmbed(ctx, *cached_meta);
        return;
    }

    // 2. Fetch HTML (Range-based progressive fetching)
    Logger::Log(LogLevel::Debug, "Cache miss. Fetching URL: " + ctx->url);
    const auto& cfg = Config::GetInstance();
    ctx->fetch_attempt_bytes = std::min(static_cast<size_t>(cfg.html_initial_range_bytes), static_cast<size_t>(cfg.max_html_bytes));
    if (ctx->fetch_attempt_bytes == 0) ctx->fetch_attempt_bytes = cfg.max_html_bytes;

    // Pass a shared_ptr to the context to the fetcher so it stays alive.
    // Avoid dangling: pass a heap-allocated shared_ptr holder via user_data instead of a raw pointer.
    auto* ctx_holder = new std::shared_ptr<ProcessContext>(ctx);
    html_fetcher_.Fetch(ctx->url, ctx->fetch_attempt_bytes, true, static_cast<void*>(ctx_holder),
        [this](HTMLFetcher::FetchResult result) {
            OnFetchComplete(std::move(result));
        }
    );
}

void LinkEmbedHandler::OnFetchComplete(HTMLFetcher::FetchResult result) {
    // Reclaim the per-callback heap-allocated shared_ptr holder.
    std::unique_ptr<std::shared_ptr<ProcessContext>> ctx_holder(
        static_cast<std::shared_ptr<ProcessContext>*>(result.user_data)
    );
    std::shared_ptr<ProcessContext> ctx = *ctx_holder; // keep actual context ownership
    Logger::Log(LogLevel::Debug, "OnFetchComplete called for URL: " + ctx->url);

    if (!result.error.empty()) {
        Logger::Log(LogLevel::Error, "Failed to fetch " + ctx->url + ": " + result.error);
        return;
    }

    // Enqueue the parsing to our thread pool
    Logger::Log(LogLevel::Debug, "Enqueuing parsing to thread pool for URL: " + ctx->url);
    thread_pool.enqueue([this, result = std::move(result), ctx]() mutable {
        Logger::Log(LogLevel::Debug, "Parsing started in thread pool for URL: " + ctx->url);
        auto metadata = MetadataParser::Parse(result.content);

        if (metadata) {
            // Success, cache and send
            metadata_cache.Put(ctx->url, *metadata);
            if (!result.effective_url.empty() && result.effective_url != ctx->url) {
                metadata_cache.Put(result.effective_url, *metadata);
            }
            SendEmbed(ctx, *metadata);
        } else {
            // Parsing failed, maybe we need more data?
            const auto& cfg = Config::GetInstance();
            size_t cap = static_cast<size_t>(cfg.max_html_bytes);
            if (ctx->fetch_attempt_bytes >= cap) {
                Logger::Log(LogLevel::Warn, "Could not parse metadata within max bytes from: " + ctx->url);
                return;
            }

            size_t next = static_cast<size_t>(ctx->fetch_attempt_bytes * std::max(1.0, cfg.html_range_growth_factor));
            ctx->fetch_attempt_bytes = std::min(next > ctx->fetch_attempt_bytes ? next : ctx->fetch_attempt_bytes + 1, cap);
            Logger::Log(LogLevel::Debug, "Metadata incomplete, increasing range to " + std::to_string(ctx->fetch_attempt_bytes) + " bytes for URL: " + ctx->url);

            // Re-fetch with larger size
            auto* next_holder = new std::shared_ptr<ProcessContext>(ctx);
            html_fetcher_.Fetch(ctx->url, ctx->fetch_attempt_bytes, true, static_cast<void*>(next_holder),
                [this](HTMLFetcher::FetchResult new_result) {
                    OnFetchComplete(std::move(new_result));
                }
            );
        }
    });
}

void LinkEmbedHandler::SendEmbed(std::shared_ptr<ProcessContext> ctx, const Metadata& metadata) {
    // Re-check for Discord embed right before sending
    if (HasDiscordEmbed(ctx->channel_id, ctx->message_id, 1500)) {
        Logger::Log(LogLevel::Info, "Discord already attached an embed. Skipping bot embed for: " + ctx->url);
        return;
    }

    dpp::embed msg_embed;
    msg_embed.set_title(metadata.title);
    msg_embed.set_url(ctx->url);
    if (!metadata.description.empty()) {
        msg_embed.set_description(metadata.description);
    }
    if (!metadata.site_name.empty()) {
        msg_embed.set_footer(dpp::embed_footer().set_text(metadata.site_name));
    }
    if (!metadata.image_url.empty()) {
        msg_embed.set_thumbnail(metadata.image_url);
    }

    dpp::message reply_msg(ctx->channel_id, msg_embed);
    reply_msg.set_reference(ctx->message_id);
    bot.message_create(reply_msg, [this, message_id = ctx->message_id](const dpp::confirmation_callback_t& cc){
        if (cc.is_error()) return;
        try {
            const dpp::message& m = std::get<dpp::message>(cc.value);
            std::lock_guard<std::mutex> lk(bot_embeds_mutex_);
            bot_embeds_[message_id] = m.id;
        } catch (...) {}
    });
}

// This can be slow if it does a DNS lookup. It's also blocking.
// A fully async solution would use dpp's message_get cache or async DNS.
bool LinkEmbedHandler::HasDiscordEmbed(dpp::snowflake channel_id, dpp::snowflake message_id, int timeout_ms) {
    auto pp = std::make_shared<std::promise<bool>>();
    auto fut = pp->get_future();
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

std::vector<std::string> LinkEmbedHandler::ExtractUrls(const std::string& text) {
    // For Discord to hyperlink the embed title, embed.url must be a valid absolute URL.
    // The original regex can include trailing punctuation (e.g., closing parentheses, periods),
    // which makes Discord consider the URL invalid. Clean trailing punctuation after matching.

    auto clean_url = [](std::string s) {
        auto rtrim_any = [](std::string& x, const std::string& chars) {
            while (!x.empty() && chars.find(x.back()) != std::string::npos) x.pop_back();
        };

        // 1) Strip common trailing punctuation first
        rtrim_any(s, ")],.!?;:");

        // 2) Fix parenthesis balance: if there are more ')' than '(', drop trailing ')'
        auto count_char = [](const std::string& x, char c){ return static_cast<int>(std::count(x.begin(), x.end(), c)); };
        while (!s.empty() && count_char(s, ')') > count_char(s, '(') && s.back() == ')') {
            s.pop_back();
        }

        return s;
    };

    std::vector<std::string> urls;
    std::regex url_regex(R"((https?://[^\s<>"']+))");
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), url_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        auto u = clean_url(i->str());
        if (!u.empty()) urls.push_back(std::move(u));
    }
    return urls;
}

} // namespace LinkEmbed
