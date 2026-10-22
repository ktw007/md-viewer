#pragma once

#include <string>
#include <string_view>

namespace mdv {

struct Rendered {
    std::string html;
    std::string title;
};

// CommonMark plus GitHub tables, strikethrough, task lists, and autolinks.
// Raw HTML is escaped and shown as text.
Rendered render_markdown(std::string_view utf8);

}  // namespace mdv
