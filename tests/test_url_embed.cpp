#include <catch2/catch_all.hpp>
#include "utils/UrlUtil.hpp"
#include "utils/EmbedBuilder.hpp"

using namespace LinkEmbed;

TEST_CASE("UrlUtil cleans trailing punctuation and parentheses") {
    std::string text = "Check this out: (https://example.com/path). And also https://foo.bar/baz).";
    auto urls = UrlUtil::ExtractUrls(text);
    REQUIRE(urls.size() >= 2);
    CHECK(urls[0] == "https://example.com/path");
    CHECK(urls[1] == "https://foo.bar/baz");
}

TEST_CASE("EmbedBuilder copies metadata and url") {
    Metadata m;
    m.title = "Hello";
    m.description = "World";
    m.site_name = "Site";
    m.image_url = "https://img.example/x.png";
    std::string url = "https://example.com";

    auto e = BuildEmbed(m, url);
    CHECK(e.title == m.title);
    CHECK(e.description == m.description);
    CHECK(e.url == url);
    REQUIRE(e.footer.has_value());
    CHECK(e.footer->text == m.site_name);
    REQUIRE(e.thumbnail.has_value());
    CHECK(e.thumbnail->url == m.image_url);
}

