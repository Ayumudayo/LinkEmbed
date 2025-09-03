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

    embed_delay_seconds = data.value("embed_delay_seconds", 5);
    cache_ttl_minutes = data.value("cache_ttl_minutes", 60);
    cache_max_size = data.value("cache_max_size", 1000);
    http_timeout_ms = data.value("http_timeout_ms", 4000);
    http_max_redirects = data.value("http_max_redirects", 5);
    http_user_agent = data.value("http_user_agent", "LinkEbdBot/1.0");
    max_concurrency = data.value("max_concurrency", 0);
    rate_per_sec = data.value("rate_per_sec", 2.0);
    max_html_bytes = data.value("max_html_bytes", 8388608);
    html_initial_range_bytes = data.value("html_initial_range_bytes", 524288);
    html_range_growth_factor = data.value("html_range_growth_factor", 2.0);
    bot_token = data.value("bot_token", "YOUR_BOT_TOKEN_HERE");
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
