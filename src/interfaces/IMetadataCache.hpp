#pragma once
#include <string>
#include <optional>
#include "../parser/MetadataParser.hpp"

namespace LinkEmbed {

class IMetadataCache {
public:
    virtual ~IMetadataCache() = default;
    virtual std::optional<Metadata> Get(const std::string& url) = 0;
    virtual void Put(const std::string& url, const Metadata& metadata) = 0;
};

}

