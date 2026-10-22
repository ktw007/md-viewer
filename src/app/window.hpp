#pragma once

#include <filesystem>
#include <optional>

namespace mdv {

int run_app(const std::optional<std::filesystem::path>& initial, int show);

}  // namespace mdv
