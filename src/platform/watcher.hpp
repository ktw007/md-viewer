#pragma once

#include <windows.h>

#include <filesystem>
#include <memory>

namespace mdv {

class FileWatcher {
public:
    FileWatcher();
    ~FileWatcher();
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    void watch(HWND hwnd, UINT message, const std::filesystem::path& file);
    void unwatch();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mdv
