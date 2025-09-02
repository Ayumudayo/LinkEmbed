#pragma once
#include <string>
#include <mutex>

namespace LinkEmbed {
    enum class LogLevel {
        Debug,
        Info,
        Warn,
        Error
    };

    class Logger {
    public:
        static void Log(LogLevel level, const std::string& message);
    private:
        static std::mutex log_mutex;
    };
}