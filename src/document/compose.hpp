#pragma once

#include <string>
#include <string_view>

namespace mdv {

struct ComposeOptions {
    bool dark = false;
    double scroll = 0;
    bool force_scroll = false;
    bool welcome = false;
    std::string_view anchor;
    std::string_view file_name;
    std::string_view modified;
    std::string_view source;
    std::string_view asset_base = "https://asset.local";
    bool content_security_policy = true;
};

struct Page {
    std::string html;
};

Page compose_page(std::string_view fragment, std::string_view title, const ComposeOptions& options);

}  // namespace mdv
