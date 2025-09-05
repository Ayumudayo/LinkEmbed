#include <catch2/catch_all.hpp>
#include "utils/UrlUtil.hpp"

using namespace LinkEmbed;

TEST_CASE("ResolveAgainst handles protocol and relative URLs") {
    using UrlUtil::ResolveAgainst;

    std::string base = "https://example.com/path/page.html";
    CHECK(ResolveAgainst(base, "https://cdn.example.com/a.jpg") == "https://cdn.example.com/a.jpg");
    CHECK(ResolveAgainst(base, "//cdn.example.com/a.jpg") == "https://cdn.example.com/a.jpg");
    CHECK(ResolveAgainst(base, "/img/a.png") == "https://example.com/img/a.png");
    CHECK(ResolveAgainst(base, "img/a.png") == "https://example.com/path/img/a.png");
    CHECK(ResolveAgainst("https://example.com", "img.png").rfind("https://example.com/", 0) == 0);
}
