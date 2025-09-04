#pragma once
#include <string>
#include <dpp/dpp.h>
#include "../parser/MetadataParser.hpp"

namespace LinkEmbed {

dpp::embed BuildEmbed(const Metadata& metadata, const std::string& url);

}

