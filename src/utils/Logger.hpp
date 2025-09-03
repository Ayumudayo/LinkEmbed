#pragma once
#include <string>
#include <mutex>

#include <filesystem>

namespace LinkEmbed {
    enum class LogLevel {
        Debug,
        Info,
        Warn,
        Error
    };

    class Logger {
    public:
        static void Init(const std::string& base_dir, LogLevel min_level);
        static void SetMinLevel(LogLevel level);
        static LogLevel FromString(const std::string& s);
        static void Log(LogLevel level, const std::string& message);
    private:
        static std::mutex log_mutex;
        static LogLevel min_level_;
        static std::filesystem::path logs_dir_;
        static std::string current_date_;
        static void EnsureLogFileUnlocked(const std::tm& now_tm);
        static void OpenLogFileForDate(const std::string& date);
    };
}
