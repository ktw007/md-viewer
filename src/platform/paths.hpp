#pragma once

#include <filesystem>
#include <string>

namespace mdv {

std::filesystem::path exe_directory();
std::filesystem::path assets_directory();
std::filesystem::path roaming_directory();
std::filesystem::path local_directory();
std::filesystem::path cache_directory();
std::filesystem::path webview_data_directory();

// Stable settings key: canonical absolute path, generic slashes, ASCII lowercased.
std::string path_key(const std::filesystem::path& path);

}  // namespace mdv
