
#include "Logger.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>

namespace LinkEmbed {

std::mutex Logger::log_mutex;

void Logger::Log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex);
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

    std::cout << std::put_time(&buf, "%Y-%m-%d %X") << " [" << level_str << "] " << message << std::endl;
}

}
