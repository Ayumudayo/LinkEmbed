#pragma once
#include <chrono>
#include <mutex>

namespace LinkEmbed {
    class RateLimiter {
    public:
        RateLimiter(double rate_per_second);
        bool TryAcquire();
    private:
        double rate;
        double allowance;
        std::chrono::steady_clock::time_point last_check;
        std::mutex mutex;
    };
}