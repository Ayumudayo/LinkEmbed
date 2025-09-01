#pragma once
#include <string>
#include <mutex>

namespace LinkEmbed {
    enum class LogLevel {
        DEBUG,
        INFO,
        WARN,
        LOG_ERROR
    };

    class Logger {
    public:
        static void Log(LogLevel level, const std::string& message);
    private:
        static std::mutex log_mutex;
    };
}