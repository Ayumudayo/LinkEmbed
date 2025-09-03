#pragma once
#include <string>
#include <vector>

namespace LinkEmbed {
namespace UrlUtil {

// Extract absolute URLs from text and sanitize trailing punctuation
std::vector<std::string> ExtractUrls(const std::string& text);

}
}

