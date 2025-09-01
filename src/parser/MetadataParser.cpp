#include "MetadataParser.hpp"
#include <regex>

namespace LinkEmbed {

	std::optional<Metadata> MetadataParser::Parse(const std::string& html_content) {
		// Note: Parsing HTML with regex is not robust. Using a proper HTML parsing library
		// (e.g., gumbo-parser) would be a better solution.
		Metadata meta;
		bool title_found = false;
		bool image_found = false;
		bool desc_found = false;
		bool site_found = false;

		std::regex og_title_regex(R"(<meta\s+property=\"og:title\"\s+content=\"([^\"]*)\")");
		std::regex title_regex(R"(<title>([^<]*)</title>)");
		std::regex og_image_regex(R"(<meta\s+property=\"og:image\"\s+content=\"([^\"]*)\")");
		std::regex og_desc_regex(R"(<meta\s+property=\"og:description\"\s+content=\"([^\"]*)\")");
		std::regex meta_desc_regex(R"(<meta\s+name=\"description\"\s+content=\"([^\"]*)\")");
		std::regex og_site_regex(R"(<meta\s+property=\"og:site_name\"\s+content=\"([^\"]*)\")");
		std::regex twitter_title_regex(R"(<meta\s+name=\"twitter:title\"\s+content=\"([^\"]*)\")");
		std::regex twitter_desc_regex(R"(<meta\s+name=\"twitter:description\"\s+content=\"([^\"]*)\")");
		std::regex twitter_image_regex(R"(<meta\s+name=\"twitter:image\"\s+content=\"([^\"]*)\")");

		std::smatch match;

		// Title (OG -> twitter -> title tag)
		if (std::regex_search(html_content, match, og_title_regex) && match.size() > 1) {
			meta.title = match[1].str();
			title_found = true;
		}
		if (!title_found && std::regex_search(html_content, match, twitter_title_regex) && match.size() > 1) {
			meta.title = match[1].str();
			title_found = true;
		}
		if (!title_found && std::regex_search(html_content, match, title_regex) && match.size() > 1) {
			meta.title = match[1].str();
			title_found = true;
		}

		// Image (OG -> twitter)
		if (std::regex_search(html_content, match, og_image_regex) && match.size() > 1) {
			meta.image_url = match[1].str();
			image_found = true;
		}
		if (!image_found && std::regex_search(html_content, match, twitter_image_regex) && match.size() > 1) {
			meta.image_url = match[1].str();
			image_found = true;
		}

		// Description (OG -> meta description -> twitter)
		if (std::regex_search(html_content, match, og_desc_regex) && match.size() > 1) {
			meta.description = match[1].str();
			desc_found = true;
		}
		if (!desc_found && std::regex_search(html_content, match, meta_desc_regex) && match.size() > 1) {
			meta.description = match[1].str();
			desc_found = true;
		}
		if (!desc_found && std::regex_search(html_content, match, twitter_desc_regex) && match.size() > 1) {
			meta.description = match[1].str();
			desc_found = true;
		}

		// Site name
		if (std::regex_search(html_content, match, og_site_regex) && match.size() > 1) {
			meta.site_name = match[1].str();
			site_found = true;
		}

		if (title_found || image_found || desc_found || site_found) {
			return meta;
		}

		return std::nullopt;
	}

}
