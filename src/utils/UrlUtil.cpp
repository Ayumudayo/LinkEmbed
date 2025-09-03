#include "UrlUtil.hpp"
#include <regex>
#include <algorithm>

namespace LinkEmbed {
namespace UrlUtil {

static inline std::string CleanUrl(std::string s) {
    auto rtrim_any = [](std::string& x, const std::string& chars) {
        while (!x.empty() && chars.find(x.back()) != std::string::npos) x.pop_back();
    };

    // 1) Strip common trailing punctuation
    rtrim_any(s, ")],.!?;:");

    // 2) Fix parenthesis balance: drop extra trailing ')'
    auto count_char = [](const std::string& x, char c){ return static_cast<int>(std::count(x.begin(), x.end(), c)); };
    while (!s.empty() && count_char(s, ')') > count_char(s, '(') && s.back() == ')') {
        s.pop_back();
    }

    return s;
}

std::vector<std::string> ExtractUrls(const std::string& text) {
    std::vector<std::string> urls;
    std::regex url_regex(R"((https?://[^\s<>"']+))");
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), url_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        auto u = CleanUrl(i->str());
        if (!u.empty()) urls.push_back(std::move(u));
    }
    return urls;
}

}
}

