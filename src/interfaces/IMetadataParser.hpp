#pragma once
#include <string>
#include <optional>
#include "../parser/MetadataParser.hpp"

namespace LinkEmbed {

class IMetadataParser {
public:
    virtual ~IMetadataParser() = default;
    virtual std::optional<Metadata> Parse(const std::string& html_content) = 0;
};

}

