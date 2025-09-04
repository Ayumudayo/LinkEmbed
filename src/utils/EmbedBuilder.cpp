#include "EmbedBuilder.hpp"

namespace LinkEmbed {

dpp::embed BuildEmbed(const Metadata& metadata, const std::string& url) {
    dpp::embed e;
    if (!metadata.title.empty()) e.set_title(metadata.title);
    if (!url.empty()) e.set_url(url);
    if (!metadata.description.empty()) e.set_description(metadata.description);
    if (!metadata.site_name.empty()) e.set_footer(dpp::embed_footer().set_text(metadata.site_name));
    if (!metadata.image_url.empty()) e.set_thumbnail(metadata.image_url);
    return e;
}

}

