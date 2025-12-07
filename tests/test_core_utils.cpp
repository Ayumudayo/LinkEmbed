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
    MetadataCache cache(1, /*ttl_minutes=*/10, /*max_bytes=*/256);
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

TEST_CASE("MetadataCache byte budget enforcement") {
    MetadataCache cache(10, /*ttl_minutes=*/10, /*max_bytes=*/60);

    Metadata small;
    small.description = std::string(20, 's');
    cache.Put("http://s1", small);
    cache.Put("http://s2", small);
    REQUIRE(cache.Get("http://s1").has_value());
    REQUIRE(cache.Get("http://s2").has_value());

    Metadata large;
    large.description = std::string(80, 'l');
    cache.Put("http://large", large);
    CHECK_FALSE(cache.Get("http://large").has_value());

    Metadata medium;
    medium.description = std::string(30, 'm');
    cache.Put("http://s3", medium);

    CHECK_FALSE(cache.Get("http://s1").has_value());
    CHECK_FALSE(cache.Get("http://s2").has_value());
    auto stored = cache.Get("http://s3");
    REQUIRE(stored.has_value());
    CHECK(stored->description == std::string(30, 'm'));
}
