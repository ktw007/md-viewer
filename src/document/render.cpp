#include "document/render.hpp"

#include "document/escape.hpp"

#include "md4c-html.h"

#include <cctype>
#include <string>
#include <unordered_map>
#include <utility>

namespace mdv {
namespace {

void append_output(const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(text, static_cast<size_t>(size));
}

std::string strip_tags(std::string_view html) {
    std::string text;
    bool in_tag = false;
    for (const char c : html) {
        if (c == '<') {
            in_tag = true;
            continue;
        }
        if (c == '>') {
            in_tag = false;
            continue;
        }
        if (!in_tag) {
            text.push_back(c);
        }
    }
    return unescape_basic(text);
}

std::string collapse_space(std::string_view text) {
    std::string out;
    bool pending = false;
    for (const unsigned char c : text) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            pending = !out.empty();
            continue;
        }
        if (pending) {
            out.push_back(' ');
            pending = false;
        }
        out.push_back(static_cast<char>(c));
    }
    return out;
}

std::string first_heading_text(std::string_view html) {
    const auto open = html.find("<h1");
    if (open == std::string_view::npos) {
        return {};
    }
    const auto content = html.find('>', open);
    if (content == std::string_view::npos) {
        return {};
    }
    const auto close = html.find("</h1>", content);
    if (close == std::string_view::npos) {
        return {};
    }
    return collapse_space(strip_tags(html.substr(content + 1, close - content - 1)));
}

void append_utf8(std::string& out, std::string_view text, size_t& index) {
    const auto lead = static_cast<unsigned char>(text[index]);
    size_t length = 1;
    if ((lead & 0xE0) == 0xC0) {
        length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        length = 4;
    }
    if (index + length > text.size()) {
        length = 1;
    }
    out.append(text.substr(index, length));
    index += length;
}

std::string slug_from(std::string_view text) {
    std::string slug;
    bool dash = false;
    for (size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x80) {
            if (std::isalnum(c) != 0) {
                slug.push_back(static_cast<char>(std::tolower(c)));
                dash = false;
            } else if (!slug.empty() && !dash) {
                slug.push_back('-');
                dash = true;
            }
            ++i;
            continue;
        }
        append_utf8(slug, text, i);
        dash = false;
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    if (slug.empty()) {
        slug = "section";
    }
    return slug;
}

std::string unique_slug(std::string base, std::unordered_map<std::string, int>& seen) {
    const int n = seen[base]++;
    if (n == 0) {
        return base;
    }
    return base + "-" + std::to_string(n);
}

// md4c writes headings as "<hN>". Give each one a stable id so in-page links work.
std::string add_heading_ids(std::string html) {
    std::unordered_map<std::string, int> seen;
    std::string out;
    out.reserve(html.size() + 64);
    size_t i = 0;
    while (i < html.size()) {
        const bool heading = html[i] == '<' && i + 3 < html.size() && html[i + 1] == 'h' &&
                             html[i + 2] >= '1' && html[i + 2] <= '6' && html[i + 3] == '>';
        if (!heading) {
            out.push_back(html[i]);
            ++i;
            continue;
        }
        const auto content = i + 4;
        const std::string close = std::string("</h") + html[i + 2] + ">";
        const auto end = html.find(close, content);
        if (end == std::string::npos) {
            out.push_back(html[i]);
            ++i;
            continue;
        }
        const auto text = collapse_space(strip_tags(std::string_view(html).substr(content, end - content)));
        const auto id = unique_slug(slug_from(text), seen);
        out += "<h";
        out.push_back(html[i + 2]);
        out += " id=\"";
        out += escape_html(id);
        out += "\">";
        out.append(html, content, end - content);
        out += close;
        i = end + close.size();
    }
    return out;
}

}  // namespace

Rendered render_markdown(std::string_view utf8) {
    Rendered rendered;
    const unsigned parser_flags = MD_DIALECT_GITHUB | MD_FLAG_NOHTML;
    const int rc = md_html(utf8.data(), static_cast<MD_SIZE>(utf8.size()), append_output, &rendered.html,
                           parser_flags, 0);
    if (rc != 0) {
        rendered.html.clear();
        return rendered;
    }
    rendered.html = add_heading_ids(std::move(rendered.html));
    rendered.title = first_heading_text(rendered.html);
    return rendered;
}

}  // namespace mdv
