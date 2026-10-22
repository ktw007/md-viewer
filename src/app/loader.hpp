#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>
#include <string>

namespace mdv {

struct LoadRequest {
    std::filesystem::path path;
    double scroll = 0;
    std::string anchor;
    bool dark = false;
    bool welcome = false;
    bool force_scroll = false;
};

struct LoadResult {
    uint64_t generation = 0;
    std::filesystem::path source;
    std::filesystem::path html_file;
    std::filesystem::path doc_root;
    std::wstring title;
    bool welcome = false;
};

class Loader {
public:
    explicit Loader(HWND hwnd);
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    uint64_t request(LoadRequest request);
    uint64_t generation() const;
    void shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mdv
