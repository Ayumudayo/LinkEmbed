#pragma once
#include "../interfaces/IMetadataParser.hpp"

namespace LinkEmbed {

class DefaultMetadataParser : public IMetadataParser {
public:
    std::optional<Metadata> Parse(const std::string& html_content) override;
};

}

