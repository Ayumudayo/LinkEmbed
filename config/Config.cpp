#include "Config.hpp"
#include <fstream>
#include <iomanip>
#include <stdexcept>
#include <filesystem>

namespace LinkEmbed {

void Config::Load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        throw std::runtime_error("Could not open config file: " + path);
    }
    nlohmann::json data = nlohmann::json::parse(f);
    nlohmann::json original = data; // Keep a copy to detect missing keys

    embed_delay_seconds = data.value("embed_delay_seconds", 5);
    cache_ttl_minutes = data.value("cache_ttl_minutes", 60);
    cache_max_size = data.value("cache_max_size", 1000);
    http_timeout_ms = data.value("http_timeout_ms", 4000);
    http_max_redirects = data.value("http_max_redirects", 5);
    http_user_agent = data.value("http_user_agent", "LinkEmbedBot/1.0");
    max_concurrency = data.value("max_concurrency", 0);
    rate_per_sec = data.value("rate_per_sec", 2.0);
    max_html_bytes = data.value("max_html_bytes", 8388608);
    html_initial_range_bytes = data.value("html_initial_range_bytes", 524288);
    html_range_growth_factor = data.value("html_range_growth_factor", 2.0);
    bot_token = data.value("bot_token", "YOUR_BOT_TOKEN_HERE");
    log_level = data.value("log_level", "info");

    // Load image proxy settings
    image_proxy_enabled = data.value("image_proxy_enabled", true);
    image_proxy_base = data.value("image_proxy_base", std::string("https://images.weserv.nl"));
    image_proxy_query = data.value("image_proxy_query", std::string("w=1200&h=630&fit=inside"));
    if (data.contains("image_proxy_hosts") && data["image_proxy_hosts"].is_array()) {
        image_proxy_hosts.clear();
        for (const auto& v : data["image_proxy_hosts"]) {
            if (v.is_string()) image_proxy_hosts.push_back(v.get<std::string>());
        }
        if (image_proxy_hosts.empty()) {
            image_proxy_hosts.push_back("dcinside.co.kr");
        }
    }

    // Write back missing keys so existing config.json reflects newly added options.
    // This is non-destructive: preserves unknown keys and only appends missing ones.
    bool changed = false;
    auto ensure_key = [&](const char* key, const nlohmann::json& value) {
        if (!data.contains(key)) { data[key] = value; changed = true; }
    };

    ensure_key("embed_delay_seconds", embed_delay_seconds);
    ensure_key("cache_ttl_minutes", cache_ttl_minutes);
    ensure_key("cache_max_size", cache_max_size);
    ensure_key("http_timeout_ms", http_timeout_ms);
    ensure_key("http_max_redirects", http_max_redirects);
    ensure_key("http_user_agent", http_user_agent);
    ensure_key("max_concurrency", max_concurrency);
    ensure_key("rate_per_sec", rate_per_sec);
    ensure_key("max_html_bytes", max_html_bytes);
    ensure_key("html_initial_range_bytes", html_initial_range_bytes);
    ensure_key("html_range_growth_factor", html_range_growth_factor);
    ensure_key("bot_token", bot_token);
    ensure_key("log_level", log_level);
    ensure_key("image_proxy_enabled", image_proxy_enabled);
    ensure_key("image_proxy_base", image_proxy_base);
    ensure_key("image_proxy_query", image_proxy_query);
    if (!data.contains("image_proxy_hosts")) { data["image_proxy_hosts"] = image_proxy_hosts; changed = true; }

    if (changed) {
        try {
            // Backup existing config
            std::filesystem::path p(path);
            std::filesystem::path bak = p;
            bak += ".bak";
            std::error_code ec;
            std::filesystem::copy_file(p, bak, std::filesystem::copy_options::overwrite_existing, ec);
            (void)ec; // best-effort backup

            // Write updated config with pretty formatting
            std::ofstream o(path, std::ios::trunc);
            o << std::setw(4) << data << std::endl;
        } catch (...) {
            // Swallow write errors: do not prevent startup if we can't update the file
        }
    }
}

void Config::CreateDefault(const std::string& path_str) {
    std::filesystem::path path(path_str);

    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path());
    }

    nlohmann::json data;
    Config defaultConfig;
    data["embed_delay_seconds"] = defaultConfig.embed_delay_seconds;
    data["cache_ttl_minutes"] = defaultConfig.cache_ttl_minutes;
    data["cache_max_size"] = defaultConfig.cache_max_size;
    data["http_timeout_ms"] = defaultConfig.http_timeout_ms;
    data["http_max_redirects"] = defaultConfig.http_max_redirects;
    data["http_user_agent"] = defaultConfig.http_user_agent;
    data["max_concurrency"] = defaultConfig.max_concurrency;
    data["rate_per_sec"] = defaultConfig.rate_per_sec;
    data["max_html_bytes"] = defaultConfig.max_html_bytes;
    data["html_initial_range_bytes"] = defaultConfig.html_initial_range_bytes;
    data["html_range_growth_factor"] = defaultConfig.html_range_growth_factor;
    data["bot_token"] = defaultConfig.bot_token;
    data["log_level"] = defaultConfig.log_level;

    // Write default image proxy settings
    data["image_proxy_enabled"] = defaultConfig.image_proxy_enabled;
    data["image_proxy_base"] = defaultConfig.image_proxy_base;
    data["image_proxy_query"] = defaultConfig.image_proxy_query;
    data["image_proxy_hosts"] = defaultConfig.image_proxy_hosts;

    std::ofstream o(path);
    if (!o.is_open()) {
        throw std::runtime_error("Could not open config file for writing: " + path_str);
    }
    o << std::setw(4) << data << std::endl;
    if (!o.good()) {
        throw std::runtime_error("Failed to write to config file: " + path_str);
    }
}

}
