#include "platform/paths.hpp"

#include "platform/text.hpp"

#include <windows.h>
#include <shlobj.h>

#include <cctype>
#include <system_error>

namespace mdv {
namespace {

std::filesystem::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    std::filesystem::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw != nullptr) {
        path = raw;
    }
    CoTaskMemFree(raw);
    return path;
}

std::filesystem::path ensure_dir(std::filesystem::path path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    return path;
}

}  // namespace

std::filesystem::path exe_directory() {
    wchar_t buffer[32768];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, 32768);
    if (n == 0 || n >= 32768) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path assets_directory() {
    return exe_directory() / "assets";
}

std::filesystem::path roaming_directory() {
    auto base = known_folder(FOLDERID_RoamingAppData);
    if (base.empty()) {
        base = exe_directory();
    }
    return ensure_dir(base / "MdViewer");
}

std::filesystem::path local_directory() {
    auto base = known_folder(FOLDERID_LocalAppData);
    if (base.empty()) {
        base = exe_directory();
    }
    return ensure_dir(base / "MdViewer");
}

std::filesystem::path cache_directory() {
    return ensure_dir(local_directory() / "cache");
}

std::filesystem::path webview_data_directory() {
    return ensure_dir(local_directory() / "WebView2");
}

std::string path_key(const std::filesystem::path& path) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error) {
        canonical = std::filesystem::absolute(path, error).lexically_normal();
    }
    auto key = wide_to_utf8(canonical.generic_wstring());
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return key;
}

}  // namespace mdv
