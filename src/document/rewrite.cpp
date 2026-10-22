#include "document/rewrite.hpp"

#include "document/escape.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace mdv {
namespace {

struct LocalRef {
    size_t value_begin = 0;
    size_t value_end = 0;
    std::filesystem::path resolved;
    std::string query;
    std::string fragment;
    bool usable = false;
};

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_value(text[i + 1]);
            const int lo = hex_value(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

bool valid_utf8(std::string_view text) {
    size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        size_t need = 0;
        if (c <= 0x7F) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) {
            need = 1;
        } else if ((c & 0xF0) == 0xE0) {
            need = 2;
        } else if ((c & 0xF8) == 0xF0) {
            need = 3;
        } else {
            return false;
        }
        if (i + need >= text.size()) {
            return false;
        }
        for (size_t n = 1; n <= need; ++n) {
            if ((static_cast<unsigned char>(text[i + n]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += need + 1;
    }
    return true;
}

bool is_relative_url(std::string_view url) {
    if (url.empty() || url.front() == '#' || url.starts_with("//") || url.starts_with('/')) {
        return false;
    }
    const auto colon = url.find(':');
    if (colon == std::string_view::npos) {
        return true;
    }
    if (colon == 1) {
        return false;
    }
    for (size_t i = 0; i < colon; ++i) {
        const unsigned char c = static_cast<unsigned char>(url[i]);
        if (std::isalnum(c) == 0 && c != '+' && c != '.' && c != '-') {
            return true;
        }
    }
    return false;
}

void split_url(std::string_view url, std::string_view& path, std::string& query, std::string& fragment) {
    const auto hash = url.find('#');
    const auto question = url.find('?');
    size_t path_end = url.size();
    if (question != std::string_view::npos) {
        path_end = question;
        const auto query_end = hash != std::string_view::npos && hash > question ? hash : url.size();
        query.assign(url.substr(question + 1, query_end - question - 1));
    }
    if (hash != std::string_view::npos) {
        if (hash < path_end) {
            path_end = hash;
        }
        fragment.assign(url.substr(hash + 1));
    }
    path = url.substr(0, path_end);
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
    const std::u8string bytes(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return std::filesystem::path(bytes);
}

std::string utf8_from_path(const std::filesystem::path& path) {
    const auto bytes = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

bool same_volume(const std::filesystem::path& a, const std::filesystem::path& b) {
    const auto left = a.root_name().wstring();
    const auto right = b.root_name().wstring();
    if (left.size() != right.size()) {
        return false;
    }
    if (left.empty()) {
        return true;
    }
    for (size_t i = 0; i < left.size(); ++i) {
        wchar_t ca = left[i];
        wchar_t cb = right[i];
        if (ca >= L'A' && ca <= L'Z') {
            ca = static_cast<wchar_t>(ca - L'A' + L'a');
        }
        if (cb >= L'A' && cb <= L'Z') {
            cb = static_cast<wchar_t>(cb - L'A' + L'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

bool is_within(const std::filesystem::path& root, const std::filesystem::path& target) {
    const auto relative = target.lexically_relative(root);
    if (relative.empty()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}

void append_encoded(std::string& out, std::string_view text) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    for (const unsigned char c : text) {
        if (std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
}

std::string encode_relative_url(const std::filesystem::path& relative) {
    std::string out;
    bool first = true;
    for (const auto& part : relative) {
        if (part == "." || part.empty()) {
            continue;
        }
        if (part == "..") {
            return {};
        }
        if (!first) {
            out.push_back('/');
        }
        first = false;
        append_encoded(out, utf8_from_path(part));
    }
    return out;
}

std::vector<LocalRef> collect_refs(const std::string& html, const std::filesystem::path& base) {
    std::vector<LocalRef> refs;
    const char* const keys[] = {"href=\"", "src=\""};
    for (const char* key : keys) {
        const auto key_len = std::char_traits<char>::length(key);
        size_t from = 0;
        while (true) {
            const auto at = html.find(key, from);
            if (at == std::string::npos) {
                break;
            }
            const auto begin = at + key_len;
            const auto end = html.find('"', begin);
            if (end == std::string::npos) {
                break;
            }
            from = end + 1;
            LocalRef ref;
            ref.value_begin = begin;
            ref.value_end = end;
            const auto raw = unescape_basic(std::string_view(html).substr(begin, end - begin));
            if (!is_relative_url(raw)) {
                refs.push_back(std::move(ref));
                continue;
            }
            std::string_view path_part;
            split_url(raw, path_part, ref.query, ref.fragment);
            const auto decoded = percent_decode(path_part);
            if (!valid_utf8(decoded)) {
                refs.push_back(std::move(ref));
                continue;
            }
            ref.resolved = (base / path_from_utf8(decoded)).lexically_normal();
            ref.usable = true;
            refs.push_back(std::move(ref));
        }
    }
    return refs;
}

}  // namespace

Rewritten rewrite_local_urls(std::string html, const std::filesystem::path& document_path) {
    Rewritten rewritten;
    const auto base = document_path.parent_path().lexically_normal();
    auto refs = collect_refs(html, base);
    std::sort(refs.begin(), refs.end(), [](const LocalRef& a, const LocalRef& b) {
        return a.value_begin < b.value_begin;
    });
    auto root = base;
    for (const auto& ref : refs) {
        if (!ref.usable || !same_volume(root, ref.resolved)) {
            continue;
        }
        auto candidate = root;
        while (!is_within(candidate, ref.resolved)) {
            const auto parent = candidate.parent_path();
            if (parent == candidate) {
                break;
            }
            candidate = parent;
        }
        root = candidate;
    }

    rewritten.root = root;
    std::string out;
    out.reserve(html.size() + 64);
    size_t cursor = 0;
    for (const auto& ref : refs) {
        if (ref.value_begin < cursor) {
            continue;
        }
        out.append(html, cursor, ref.value_begin - cursor);
        std::string replacement(html.substr(ref.value_begin, ref.value_end - ref.value_begin));
        if (ref.usable && same_volume(root, ref.resolved) && is_within(root, ref.resolved)) {
            const auto relative = ref.resolved.lexically_relative(root);
            auto encoded = encode_relative_url(relative);
            if (!encoded.empty() || relative.empty() || relative == ".") {
                std::string url = "https://doc.local/";
                url += encoded;
                if (!ref.query.empty()) {
                    url.push_back('?');
                    url += ref.query;
                }
                if (!ref.fragment.empty()) {
                    url.push_back('#');
                    url += ref.fragment;
                }
                replacement = escape_html(url);
            }
        }
        out += replacement;
        cursor = ref.value_end;
    }
    out.append(html, cursor, std::string::npos);
    rewritten.html = std::move(out);
    return rewritten;
}

}  // namespace mdv
