#pragma once

namespace LinkEmbed {

class IRateLimiter {
public:
    virtual ~IRateLimiter() = default;
    virtual bool TryAcquire() = 0;
};

}

