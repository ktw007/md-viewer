#pragma once

#include <string>
#include <string_view>

namespace mdv {

std::wstring utf8_to_wide(std::string_view utf8);
std::string wide_to_utf8(std::wstring_view wide);

}  // namespace mdv
