#include "DefaultMetadataParser.hpp"
#include "MetadataParser.hpp"

namespace LinkEmbed {

std::optional<Metadata> DefaultMetadataParser::Parse(const std::string& html_content) {
    return MetadataParser::Parse(html_content);
}

}

