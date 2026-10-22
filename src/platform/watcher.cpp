#include "platform/watcher.hpp"

#include <windows.h>

#include <condition_variable>
#include <mutex>
#include <thread>

namespace mdv {

struct FileWatcher::Impl {
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    std::filesystem::path file;
    HWND hwnd = nullptr;
    UINT message = 0;
    uint64_t generation = 0;
    bool stop = false;
    HANDLE wakeup = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE directory = INVALID_HANDLE_VALUE;

    void run();

    ~Impl() {
        stop = true;
        if (wakeup != nullptr) {
            SetEvent(wakeup);
        }
        if (thread.joinable()) {
            thread.join();
        }
        if (directory != INVALID_HANDLE_VALUE) {
            CloseHandle(directory);
        }
        if (wakeup != nullptr) {
            CloseHandle(wakeup);
        }
    }
};

namespace {

bool same_name(const std::wstring& left, const std::wstring& right) {
    if (left.size() != right.size()) {
        return false;
    }
    return CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()), right.c_str(),
                                 static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
}

}  // namespace

void FileWatcher::Impl::run() {
    std::filesystem::path opened_for;
    uint64_t opened_generation = 0;
    while (true) {
        std::filesystem::path watched;
        HWND target = nullptr;
        UINT notify = 0;
        uint64_t stamp = 0;
        {
            std::lock_guard lock(mutex);
            if (stop) {
                return;
            }
            watched = file;
            target = hwnd;
            notify = message;
            stamp = generation;
        }
        if (watched.empty() || target == nullptr) {
            WaitForSingleObject(wakeup, 400);
            continue;
        }
        if (directory == INVALID_HANDLE_VALUE || opened_for != watched.parent_path() ||
            opened_generation != stamp) {
            if (directory != INVALID_HANDLE_VALUE) {
                CloseHandle(directory);
                directory = INVALID_HANDLE_VALUE;
            }
            directory = CreateFileW(watched.parent_path().c_str(), FILE_LIST_DIRECTORY,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                     FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
            if (directory == INVALID_HANDLE_VALUE) {
                WaitForSingleObject(wakeup, 400);
                continue;
            }
            opened_for = watched.parent_path();
            opened_generation = stamp;
        }

        alignas(DWORD) char buffer[8192];
        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (overlapped.hEvent == nullptr) {
            return;
        }
        const BOOL armed = ReadDirectoryChangesW(
            directory, buffer, sizeof buffer, FALSE,
            FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE, nullptr,
            &overlapped, nullptr);
        if (armed == FALSE) {
            CloseHandle(overlapped.hEvent);
            CloseHandle(directory);
            directory = INVALID_HANDLE_VALUE;
            WaitForSingleObject(wakeup, 400);
            continue;
        }

        const HANDLE waits[2] = {overlapped.hEvent, wakeup};
        const DWORD which = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (which != WAIT_OBJECT_0) {
            CancelIoEx(directory, &overlapped);
            WaitForSingleObject(overlapped.hEvent, 200);
            CloseHandle(overlapped.hEvent);
            continue;
        }
        DWORD bytes = 0;
        const BOOL completed = GetOverlappedResult(directory, &overlapped, &bytes, FALSE);
        CloseHandle(overlapped.hEvent);
        if (completed == FALSE || bytes == 0) {
            continue;
        }

        bool matched = false;
        const auto wanted = watched.filename().wstring();
        auto* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(buffer);
        for (;;) {
            const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
            if (same_name(name, wanted)) {
                matched = true;
            }
            if (info->NextEntryOffset == 0) {
                break;
            }
            info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(reinterpret_cast<unsigned char*>(info) +
                                                               info->NextEntryOffset);
        }
        if (matched) {
            PostMessageW(target, notify, 0, 0);
        }
    }
}

FileWatcher::FileWatcher() : impl_(std::make_unique<Impl>()) {
    impl_->thread = std::thread(&Impl::run, impl_.get());
}

FileWatcher::~FileWatcher() { stop(); }

void FileWatcher::watch(HWND hwnd, UINT message, const std::filesystem::path& file) {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->hwnd = hwnd;
        impl_->message = message;
        impl_->file = file;
        ++impl_->generation;
    }
    SetEvent(impl_->wakeup);
}

void FileWatcher::unwatch() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->file.clear();
        impl_->hwnd = nullptr;
        ++impl_->generation;
    }
    SetEvent(impl_->wakeup);
}

void FileWatcher::stop() {
    if (!impl_) {
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
        impl_->file.clear();
    }
    if (impl_->wakeup != nullptr) {
        SetEvent(impl_->wakeup);
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

}  // namespace mdv
