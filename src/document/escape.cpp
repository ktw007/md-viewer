#include "document/escape.hpp"

namespace mdv {

std::string escape_html(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out.push_back(c);
                break;
        }
    }
    return out;
}

std::string unescape_basic(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '&') {
            const auto rest = text.substr(i);
            if (rest.starts_with("&amp;")) {
                out.push_back('&');
                i += 4;
                continue;
            }
            if (rest.starts_with("&lt;")) {
                out.push_back('<');
                i += 3;
                continue;
            }
            if (rest.starts_with("&gt;")) {
                out.push_back('>');
                i += 3;
                continue;
            }
            if (rest.starts_with("&quot;")) {
                out.push_back('"');
                i += 5;
                continue;
            }
            if (rest.starts_with("&#39;")) {
                out.push_back('\'');
                i += 4;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

}  // namespace mdv
