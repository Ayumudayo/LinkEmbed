#pragma once
#include <chrono>
#include <mutex>
#include "../interfaces/IRateLimiter.hpp"

namespace LinkEmbed {
    class RateLimiter : public IRateLimiter {
    public:
        RateLimiter(double rate_per_second);
        bool TryAcquire() override;
    private:
        double rate;
        double allowance;
        std::chrono::steady_clock::time_point last_check;
        std::mutex mutex;
    };
}
