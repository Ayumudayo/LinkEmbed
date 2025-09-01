
#include "RateLimiter.hpp"

namespace LinkEmbed {

RateLimiter::RateLimiter(double rate_per_second) : rate(rate_per_second) {
    allowance = rate;
    last_check = std::chrono::steady_clock::now();
}

bool RateLimiter::TryAcquire() {
    std::lock_guard<std::mutex> lock(mutex);
    auto now = std::chrono::steady_clock::now();
    double time_passed = std::chrono::duration<double>(now - last_check).count();
    last_check = now;

    allowance += time_passed * rate;
    if (allowance > rate) {
        allowance = rate;
    }

    if (allowance >= 1.0) {
        allowance -= 1.0;
        return true;
    }
    return false;
}

}
