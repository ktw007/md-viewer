#pragma once

#include <filesystem>
#include <string>

namespace mdv {

struct Rewritten {
    std::string html;
    std::filesystem::path root;
};

// Rewrite relative image and link URLs to https://doc.local/... under `root`.
// `root` starts at the document directory and rises to the common parent of
// relative targets on the same volume. Absolute URLs and other volumes stay.
Rewritten rewrite_local_urls(std::string html, const std::filesystem::path& document_path);

}  // namespace mdv
