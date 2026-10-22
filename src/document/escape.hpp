#pragma once

#include <string>
#include <string_view>

namespace mdv {

std::string escape_html(std::string_view text);
std::string unescape_basic(std::string_view text);

}  // namespace mdv
