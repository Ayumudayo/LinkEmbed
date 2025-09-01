#pragma once
#include <string>
#include <optional>

namespace LinkEmbed {
    struct Metadata {
        std::string title;
        std::string image_url;
        std::string description;
        std::string site_name;
    };

    class MetadataParser {
    public:
        static std::optional<Metadata> Parse(const std::string& html_content);
    };
}