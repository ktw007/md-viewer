#pragma once

#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace mdv {

class WebViewHost {
public:
    using ReadyCallback = std::function<void(bool ok, const std::wstring& error)>;
    using NavigateCallback = std::function<void(const std::wstring& uri, bool user, bool& cancel)>;
    using MessageCallback = std::function<void(std::string_view json)>;
    using KeyCallback = std::function<bool(unsigned key, bool ctrl, bool alt, bool shift, bool repeated)>;

    WebViewHost();
    ~WebViewHost();
    WebViewHost(const WebViewHost&) = delete;
    WebViewHost& operator=(const WebViewHost&) = delete;

    void initialize(HWND parent, std::filesystem::path user_data, ReadyCallback ready);
    void resize(RECT bounds);
    void set_background(COLORREF color);
    // allow: styles, scripts, and fonts. deny_cors: images and navigation, not fetch.
    enum class FolderAccess { allow, deny_cors };

    void map_folder(const wchar_t* host, const std::filesystem::path& folder, FolderAccess access);
    void navigate(const std::wstring& url);
    void execute(const std::wstring& script);
    void print_page();
    void set_zoom(double zoom);
    void on_navigate(NavigateCallback callback);
    void on_message(MessageCallback callback);
    void on_key(KeyCallback callback);
    void on_zoom(std::function<void(double)> callback);
    void on_navigated(std::function<void()> callback);
    // Replaces Chromium's drop target so a dropped file is opened, not downloaded.
    void enable_file_drop(std::function<void(const std::wstring& path)> callback);
    void notify_moved();
    bool ready() const;
    void close();

private:
    struct Impl;
    void install_file_drop();
    std::shared_ptr<Impl> impl_;
};

}  // namespace mdv
