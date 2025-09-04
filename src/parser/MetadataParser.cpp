#include "MetadataParser.hpp"
#include <lexbor/html/html.h>
#include <lexbor/dom/dom.h>
#include <optional>
#include <string>
#include <cstring> // For strlen
#include <algorithm> // for std::transform

namespace {

// Helper to convert lxb_char_t* to std::string
std::string to_std_string(const lxb_char_t* lxb_str, size_t len) {
    if (lxb_str && len > 0) {
        return std::string(reinterpret_cast<const char*>(lxb_str), len);
    }
    return "";
}

// Helper to get an attribute value by key
std::string get_attribute_value(lxb_dom_element_t* element, const char* key) {
    size_t len;
    const lxb_char_t* value = lxb_dom_element_get_attribute(element, reinterpret_cast<const lxb_char_t*>(key), strlen(key), &len);
    return to_std_string(value, len);
}

// ASCII lowercase helper
static inline void ascii_tolower_inplace(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : static_cast<char>(c);
    });
}

} // anonymous namespace

namespace LinkEmbed {

std::optional<Metadata> MetadataParser::Parse(const std::string& html_content) {
    // Initialize lexbor parser
    lxb_html_document_t* document = lxb_html_document_create();
    if (!document) return std::nullopt;

    // Parse HTML
    lxb_status_t status = lxb_html_document_parse(document, 
        reinterpret_cast<const lxb_char_t*>(html_content.c_str()), 
        html_content.length());

    if (status != LXB_STATUS_OK) {
        lxb_html_document_destroy(document);
        return std::nullopt;
    }

    Metadata meta;
    // 1) title: use lexbor API for robustness
    {
        size_t tlen = 0;
        const lxb_char_t* t = lxb_html_document_title(document, &tlen);
        if (t && tlen > 0) {
            meta.title = to_std_string(t, tlen);
        }
    }

    // Prepare DOM document ref for collections
    lxb_dom_document_t* dom_doc = lxb_html_document_original_ref(document);

    // 2) meta tags: prioritize scanning in the head element
    lxb_dom_collection_t* col = lxb_dom_collection_make(dom_doc, 32);
    if (col != nullptr) {
        auto* head_el = lxb_html_document_head_element(document);
        if (head_el != nullptr) {
            (void) lxb_dom_elements_by_tag_name(lxb_dom_interface_element(head_el), col,
                                                reinterpret_cast<const lxb_char_t*>("meta"), 4);
        }

        const size_t count = lxb_dom_collection_length(col);
        for (size_t i = 0; i < count; ++i) {
            lxb_dom_element_t* el = lxb_dom_collection_element(col, i);
            if (!el) continue;

            std::string prop = get_attribute_value(el, "property");
            if (prop.empty()) prop = get_attribute_value(el, "name");
            std::string content = get_attribute_value(el, "content");

            if (prop.empty() || content.empty()) continue;
            ascii_tolower_inplace(prop);

            if (prop == "og:title" && meta.title.empty()) meta.title = content;
            else if (prop == "og:description" && meta.description.empty()) meta.description = content;
            else if ((prop == "og:image" || prop == "og:image:url" || prop == "og:image:secure_url") && meta.image_url.empty()) meta.image_url = content;
            else if (prop == "og:site_name" && meta.site_name.empty()) meta.site_name = content;
            else if (prop == "twitter:title" && meta.title.empty()) meta.title = content;
            else if (prop == "twitter:description" && meta.description.empty()) meta.description = content;
            else if ((prop == "twitter:image" || prop == "twitter:image:src") && meta.image_url.empty()) meta.image_url = content;
            else if (prop == "description" && meta.description.empty()) meta.description = content;
        }

        lxb_dom_collection_destroy(col, true);
    }

    // Destroy the document to free memory
    lxb_html_document_destroy(document);

    if (!meta.title.empty() || !meta.description.empty() || !meta.image_url.empty() || !meta.site_name.empty()) {
        return meta;
    }

    return std::nullopt;
}

}
