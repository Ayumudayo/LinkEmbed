#include <catch2/catch_all.hpp>
#include <thread>
#include "utils/RateLimiter.hpp"
#include "cache/MetadataCache.hpp"

using namespace LinkEmbed;

TEST_CASE("RateLimiter basic token bucket behavior") {
    RateLimiter rl(2.0); // 2 tokens per second, starts full (2)
    REQUIRE(rl.TryAcquire());
    REQUIRE(rl.TryAcquire());
    CHECK_FALSE(rl.TryAcquire()); // exhausted
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    CHECK(rl.TryAcquire()); // regained at least 1 token
}

TEST_CASE("MetadataCache LRU eviction") {
    MetadataCache cache(1, /*ttl_minutes=*/10);
    Metadata a; a.title = "A";
    Metadata b; b.title = "B";
    cache.Put("http://a", a);
    cache.Put("http://b", b); // evicts A
    auto ga = cache.Get("http://a");
    auto gb = cache.Get("http://b");
    CHECK_FALSE(ga.has_value());
    REQUIRE(gb.has_value());
    CHECK(gb->title == "B");
}

