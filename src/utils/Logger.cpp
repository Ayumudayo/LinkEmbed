
#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <fstream>

namespace LinkEmbed {

std::mutex Logger::log_mutex;
LogLevel Logger::min_level_ = LogLevel::Debug;
std::filesystem::path Logger::logs_dir_{};
std::string Logger::current_date_{};

static inline std::string TwoDigits(int v) {
    char buf[3];
    buf[0] = char('0' + (v / 10));
    buf[1] = char('0' + (v % 10));
    buf[2] = '\0';
    return std::string(buf, 2);
}

void Logger::Init(const std::string& base_dir, LogLevel min_level) {
    std::lock_guard<std::mutex> lock(log_mutex);
    logs_dir_ = std::filesystem::path(base_dir) / "logs";
    std::error_code ec;
    std::filesystem::create_directories(logs_dir_, ec);
    min_level_ = min_level;
    // Open today's log file lazily on first Log() call.
}

void Logger::SetMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(log_mutex);
    min_level_ = level;
}

LogLevel Logger::FromString(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (unsigned char c : s) t.push_back((c >= 'A' && c <= 'Z') ? char(c + 32) : char(c));
    if (t == "debug") return LogLevel::Debug;
    if (t == "info")  return LogLevel::Info;
    if (t == "warn" || t == "warning")  return LogLevel::Warn;
    if (t == "error" || t == "err") return LogLevel::Error;
    return LogLevel::Info;
}

static std::ofstream& GetFileStream() {
    static std::ofstream ofs;
    return ofs;
}

void Logger::OpenLogFileForDate(const std::string& date) {
    auto& ofs = GetFileStream();
    if (ofs.is_open()) ofs.close();
    std::filesystem::path file = logs_dir_ / (date + ".log");
    ofs.open(file, std::ios::out | std::ios::app);
}

void Logger::EnsureLogFileUnlocked(const std::tm& now_tm) {
    std::string date = std::to_string(1900 + now_tm.tm_year) + "-" + TwoDigits(now_tm.tm_mon + 1) + "-" + TwoDigits(now_tm.tm_mday);
    if (date != current_date_) {
        current_date_ = date;
        OpenLogFileForDate(date);
    }
}

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
    if (level < min_level_) return;
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);

    std::tm buf;
    #ifdef _WIN32
    localtime_s(&buf, &in_time_t);
    #else
    localtime_r(&in_time_t, &buf);
    #endif

    const char* level_str = "";
    switch (level) {
        case LogLevel::Debug: level_str = "Debug"; break;
        case LogLevel::Info:  level_str = "Info";  break;
        case LogLevel::Warn:  level_str = "Warn";  break;
        case LogLevel::Error: level_str = "Error"; break;
    }

    // Console
    std::cout << std::put_time(&buf, "%Y-%m-%d %X") << " [" << level_str << "] " << message << std::endl;

    // File (logs/YYYY-MM-DD.log)
    if (!logs_dir_.empty()) {
        EnsureLogFileUnlocked(buf);
        auto& ofs = GetFileStream();
        if (ofs.is_open()) {
            ofs << std::put_time(&buf, "%Y-%m-%d %X") << " [" << level_str << "] " << message << std::endl;
        }
    }
}

}
