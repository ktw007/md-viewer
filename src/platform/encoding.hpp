#pragma once

#include <filesystem>
#include <string>

namespace mdv {

struct FileText {
    std::string utf8;
    std::wstring error;
    bool ok = false;
};

// UTF-8, then UTF-16 BOM, then GB18030, then the system ANSI code page.
// Rejects files larger than 32 MB and files that contain NUL bytes.
FileText read_markdown_file(const std::filesystem::path& path);

}  // namespace mdv
