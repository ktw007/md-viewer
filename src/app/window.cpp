#include "app/window.hpp"

#include "app/loader.hpp"
#include "app/messages.hpp"
#include "platform/paths.hpp"
#include "platform/settings.hpp"
#include "platform/text.hpp"
#include "platform/watcher.hpp"
#include "webview/host.hpp"

#include <windows.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_BORDER_COLOR 34
#define DWMWA_CAPTION_COLOR 35
#define DWMWA_TEXT_COLOR 36
#endif

namespace mdv {
namespace {

constexpr wchar_t kWindowClass[] = L"MdViewerWindow";

bool system_dark() {
    DWORD value = 1;
    DWORD size = sizeof value;
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return status == ERROR_SUCCESS && value == 0;
}

enum class Chrome { paper, ink, mist };

COLORREF bg_color(Chrome chrome) {
    switch (chrome) {
        case Chrome::ink:
            return RGB(0x1B, 0x1A, 0x18);
        case Chrome::mist:
            return RGB(0xF2, 0xF3, 0xF5);
        default:
            return RGB(0xF4, 0xF1, 0xEA);
    }
}

COLORREF frame_color(Chrome chrome) {
    switch (chrome) {
        case Chrome::ink:
            return RGB(0x34, 0x31, 0x2D);
        case Chrome::mist:
            return RGB(0xD5, 0xD9, 0xE0);
        default:
            return RGB(0xE2, 0xDD, 0xD2);
    }
}

COLORREF ink_color(Chrome chrome) {
    switch (chrome) {
        case Chrome::ink:
            return RGB(0xE6, 0xE2, 0xDA);
        case Chrome::mist:
            return RGB(0x1C, 0x1E, 0x22);
        default:
            return RGB(0x26, 0x23, 0x1F);
    }
}

COLORREF ink_soft_color(Chrome chrome) {
    switch (chrome) {
        case Chrome::ink:
            return RGB(0xA3, 0x9D, 0x92);
        case Chrome::mist:
            return RGB(0x5C, 0x63, 0x70);
        default:
            return RGB(0x6B, 0x65, 0x5C);
    }
}

COLORREF hover_color(Chrome chrome) {
    switch (chrome) {
        case Chrome::ink:
            return RGB(0x2C, 0x2A, 0x27);
        case Chrome::mist:
            return RGB(0xE3, 0xE6, 0xEB);
        default:
            return RGB(0xE7, 0xE2, 0xD6);
    }
}

std::filesystem::path normalize_path(const std::filesystem::path& path) {
    std::error_code error;
    auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        absolute = path;
    }
    auto canonical = std::filesystem::weakly_canonical(absolute, error);
    return error ? absolute.lexically_normal() : canonical;
}

std::wstring lower_copy(std::wstring text) {
    for (wchar_t& c : text) {
        if (c >= L'A' && c <= L'Z') {
            c = static_cast<wchar_t>(c - L'A' + L'a');
        }
    }
    return text;
}

bool is_markdown(const std::filesystem::path& path) {
    const auto ext = lower_copy(path.extension().wstring());
    return ext == L".md" || ext == L".markdown" || ext == L".mdown" || ext == L".mkd";
}

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hex_value(text[i + 1]);
            const int lo = hex_value(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::filesystem::path path_from_utf8(std::string_view utf8) {
    const std::u8string bytes(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return std::filesystem::path(bytes);
}

std::optional<std::string> json_string(std::string_view json, std::string_view key) {
    const auto token = std::string("\"") + std::string(key) + "\"";
    const auto key_at = json.find(token);
    if (key_at == std::string_view::npos) {
        return std::nullopt;
    }
    const auto colon = json.find(':', key_at + token.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const auto quote = json.find('"', colon + 1);
    if (quote == std::string_view::npos) {
        return std::nullopt;
    }
    std::string out;
    for (size_t i = quote + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            out.push_back(json[++i]);
            continue;
        }
        if (json[i] == '"') {
            return out;
        }
        out.push_back(json[i]);
    }
    return std::nullopt;
}

std::optional<double> json_number(std::string_view json, std::string_view key) {
    const auto token = std::string("\"") + std::string(key) + "\"";
    const auto key_at = json.find(token);
    if (key_at == std::string_view::npos) {
        return std::nullopt;
    }
    const auto colon = json.find(':', key_at + token.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    size_t i = colon + 1;
    while (i < json.size() && std::isspace(static_cast<unsigned char>(json[i])) != 0) {
        ++i;
    }
    try {
        size_t used = 0;
        const double value = std::stod(std::string(json.substr(i)), &used);
        if (used == 0) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

struct DocLink {
    std::filesystem::path path;
    std::string anchor;
};

std::optional<DocLink> link_from_doc_url(const std::wstring& uri, const std::filesystem::path& root) {
    const auto utf8 = wide_to_utf8(uri);
    constexpr std::string_view kPrefix = "https://doc.local/";
    if (!utf8.starts_with(kPrefix)) {
        return std::nullopt;
    }
    auto rest = utf8.substr(kPrefix.size());
    std::string anchor;
    const auto hash = rest.find('#');
    if (hash != std::string::npos) {
        anchor = percent_decode(rest.substr(hash + 1));
        rest.resize(hash);
    }
    const auto question = rest.find('?');
    if (question != std::string::npos) {
        rest.resize(question);
    }
    DocLink link;
    link.path = (root / path_from_utf8(percent_decode(rest))).lexically_normal();
    link.anchor = std::move(anchor);
    return link;
}

std::optional<std::filesystem::path> path_from_file_url(std::wstring uri) {
    const auto hash = uri.find(L'#');
    if (hash != std::wstring::npos) {
        uri.resize(hash);
    }
    DWORD count = 32768;
    std::wstring path(count, L'\0');
    if (FAILED(PathCreateFromUrlW(uri.c_str(), path.data(), &count, 0))) {
        return std::nullopt;
    }
    path.resize(count);
    if (!path.empty() && path.back() == L'\0') {
        path.pop_back();
    }
    if (path.empty()) {
        return std::nullopt;
    }
    return path;
}

}  // namespace

class App {
public:
    int run(const std::optional<std::filesystem::path>& initial, int show);

private:
    static LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    LRESULT handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
    void create_window(int show);
    void open_initial(const std::filesystem::path& path);
    void open_link(const std::filesystem::path& path, const std::string& anchor);
    void go(int delta);
    void reload_current();
    void begin_load(const std::filesystem::path& path, double scroll, const std::string& anchor, bool welcome,
                    bool force_scroll);
    void pick_file();
    void on_parsed(LoadResult* result);
    void on_webview_ready(bool ok, const std::wstring& error);
    void on_navigate(const std::wstring& uri, bool user, bool& cancel);
    void on_message(std::string_view json);
    bool on_key(unsigned key, bool ctrl, bool alt, bool shift, bool repeated);
    void on_drop(HDROP drop);
    void open_dropped(const std::wstring& path);
    void apply_chrome(bool page);
    void extend_frame();
    void suppress_system_caption();
    int caption_px() const;
    RECT caption_button(int index) const;
    int caption_button_at(POINT pt) const;
    void layout_webview();
    void redraw_caption();
    void ensure_fonts();
    void paint_window(HDC dc);
    void fill_caption_rect(HDC dc, int left, int top, int right, int bottom, COLORREF color) const;
    void draw_caption_glyph(HDC dc, const RECT& button, int id, COLORREF color) const;
    void remember_window();
    void save_soon();
    void navigate_ready(const std::wstring& url);

    struct Place {
        std::filesystem::path path;
        double scroll = 0;
        std::string anchor;
    };

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HBRUSH brush_ = nullptr;
    bool dark_ = false;
    Chrome chrome_ = Chrome::paper;
    bool alive_ = true;
    bool web_ready_ = false;
    Settings settings_;
    WebViewHost host_;
    std::unique_ptr<Loader> loader_;
    std::unique_ptr<FileWatcher> watcher_;
    std::filesystem::path current_;
    std::filesystem::path doc_root_;
    std::wstring pending_url_;
    double last_scroll_ = 0;
    std::vector<Place> history_;
    size_t history_index_ = 0;
    std::wstring last_drop_path_;
    ULONGLONG last_drop_tick_ = 0;
    HFONT caption_font_ = nullptr;
    int fonts_dpi_ = 0;
    int hover_button_ = 0;
    int pressed_button_ = 0;
    bool tracking_leave_ = false;
};

int App::run(const std::optional<std::filesystem::path>& initial, int show) {
    instance_ = GetModuleHandleW(nullptr);
    settings_.load();
    dark_ = system_dark();
    chrome_ = dark_ ? Chrome::ink : Chrome::paper;
    create_window(show);
    if (initial) {
        open_initial(*initial);
    } else {
        begin_load({}, 0, {}, true, false);
    }
    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

void App::create_window(int show) {
    brush_ = CreateSolidBrush(bg_color(chrome_));
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof window_class;
    window_class.lpfnWndProc = wndproc;
    window_class.hInstance = instance_;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = brush_;
    window_class.hIcon = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                       GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    window_class.hIconSm = static_cast<HICON>(
        LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                   GetSystemMetrics(SM_CYSMICON), 0));
    window_class.lpszClassName = kWindowClass;
    window_class.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&window_class);

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 0;
    int height = 0;
    bool maximized = false;
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int dpi = GetDpiForSystem();
    const int default_width = MulDiv(920, dpi, 96);
    const int default_height = MulDiv(840, dpi, 96);
    if (settings_.window.valid) {
        x = settings_.window.x;
        y = settings_.window.y;
        width = settings_.window.width;
        height = settings_.window.height;
        maximized = settings_.window.maximized;
        RECT placed{x, y, x + width, y + height};
        if (MonitorFromRect(&placed, MONITOR_DEFAULTTONULL) == nullptr) {
            settings_.window.valid = false;
        }
    }
    if (!settings_.window.valid) {
        const int work_width = work.right - work.left;
        const int work_height = work.bottom - work.top;
        width = default_width < work_width ? default_width : work_width;
        height = default_height < work_height ? default_height : work_height;
        x = work.left + (work_width - width) / 2;
        y = work.top + (work_height - height) / 2;
        maximized = false;
    } else {
        const int work_width = work.right - work.left;
        const int work_height = work.bottom - work.top;
        if (width > work_width) {
            width = work_width;
        }
        if (height > work_height) {
            height = work_height;
        }
        if (x < work.left) {
            x = work.left;
        }
        if (y < work.top) {
            y = work.top;
        }
        if (x + width > work.right) {
            x = work.right - width;
        }
        if (y + height > work.bottom) {
            y = work.bottom - height;
        }
    }

    hwnd_ = CreateWindowExW(0, kWindowClass, L"阅读", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, width, height,
                            nullptr, nullptr, instance_, this);
    suppress_system_caption();
    extend_frame();
    apply_chrome(false);
    DragAcceptFiles(hwnd_, TRUE);
    int command = show;
    if (maximized && show != SW_SHOWMINIMIZED && show != SW_SHOWMINNOACTIVE) {
        command = SW_SHOWMAXIMIZED;
    }
    ShowWindow(hwnd_, command);
    UpdateWindow(hwnd_);

    loader_ = std::make_unique<Loader>(hwnd_);
    watcher_ = std::make_unique<FileWatcher>();
    host_.on_navigate([this](const std::wstring& uri, bool user, bool& cancel) { on_navigate(uri, user, cancel); });
    host_.on_message([this](std::string_view json) { on_message(json); });
    host_.on_key([this](unsigned key, bool ctrl, bool alt, bool shift, bool repeated) {
        return on_key(key, ctrl, alt, shift, repeated);
    });
    host_.on_zoom([this](double zoom) {
        settings_.zoom = zoom;
        save_soon();
    });
    host_.on_navigated([this] { apply_chrome(true); });
    host_.initialize(hwnd_, webview_data_directory(),
                     [this](bool ok, const std::wstring& error) { on_webview_ready(ok, error); });
}

LRESULT CALLBACK App::wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* created = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return self->handle(hwnd, message, wparam, lparam);
}

LRESULT App::handle(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_NCCALCSIZE:
            if (wparam == 0) {
                return DefWindowProcW(hwnd, message, wparam, lparam);
            }
            if (IsZoomed(hwnd)) {
                auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lparam);
                const int dpi = GetDpiForWindow(hwnd);
                const int pad = GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
                const int frame_x = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + pad;
                const int frame_y = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + pad;
                params->rgrc[0].left += frame_x;
                params->rgrc[0].top += frame_y;
                params->rgrc[0].right -= frame_x;
                params->rgrc[0].bottom -= frame_y;
            }
            return 0;
        case WM_NCHITTEST: {
            POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            RECT client{};
            GetClientRect(hwnd, &client);
            const int dpi = GetDpiForWindow(hwnd);
            const int border = MulDiv(8, dpi, 96);
            if (!IsZoomed(hwnd)) {
                const bool left = pt.x < border;
                const bool right = pt.x >= client.right - border;
                const bool top = pt.y < border;
                const bool bottom = pt.y >= client.bottom - border;
                if (top && left) {
                    return HTTOPLEFT;
                }
                if (top && right) {
                    return HTTOPRIGHT;
                }
                if (bottom && left) {
                    return HTBOTTOMLEFT;
                }
                if (bottom && right) {
                    return HTBOTTOMRIGHT;
                }
                if (left) {
                    return HTLEFT;
                }
                if (right) {
                    return HTRIGHT;
                }
                if (top) {
                    return HTTOP;
                }
                if (bottom) {
                    return HTBOTTOM;
                }
            }
            if (pt.y >= 0 && pt.y < caption_px()) {
                if (caption_button_at(pt) != 0) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_NCACTIVATE:
            return DefWindowProcW(hwnd, message, wparam, static_cast<LPARAM>(-1));
        case WM_NCPAINT:
        case 0x00AE:  // WM_NCUAHDRAWCAPTION
        case 0x00AF:  // WM_NCUAHDRAWFRAME
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            const HDC dc = BeginPaint(hwnd, &paint);
            paint_window(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_SETTEXT: {
            const LRESULT result = DefWindowProcW(hwnd, message, wparam, lparam);
            redraw_caption();
            return result;
        }
        case WM_ACTIVATE: {
            const LRESULT result = DefWindowProcW(hwnd, message, wparam, lparam);
            redraw_caption();
            return result;
        }
        case WM_MOUSEMOVE: {
            const POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const int hit = caption_button_at(pt);
            if (hit != hover_button_) {
                hover_button_ = hit;
                redraw_caption();
            }
            if (!tracking_leave_) {
                TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&track);
                tracking_leave_ = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_leave_ = false;
            if (hover_button_ != 0) {
                hover_button_ = 0;
                redraw_caption();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            const POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            const int hit = caption_button_at(pt);
            if (hit != 0) {
                pressed_button_ = hit;
                SetCapture(hwnd);
                redraw_caption();
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        case WM_LBUTTONUP: {
            if (pressed_button_ != 0) {
                const POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                const int hit = caption_button_at(pt);
                const int which = pressed_button_;
                pressed_button_ = 0;
                ReleaseCapture();
                redraw_caption();
                if (hit == which) {
                    if (which == 1) {
                        SendMessageW(hwnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
                    } else if (which == 2) {
                        SendMessageW(hwnd, WM_SYSCOMMAND, IsZoomed(hwnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
                    } else {
                        SendMessageW(hwnd, WM_SYSCOMMAND, SC_CLOSE, 0);
                    }
                }
                return 0;
            }
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        case WM_SIZE:
            layout_webview();
            redraw_caption();
            return 0;
        case WM_MOVE:
            host_.notify_moved();
            return 0;
        case WM_EXITSIZEMOVE:
            remember_window();
            save_soon();
            return 0;
        case WM_DPICHANGED: {
            const auto* suggested = reinterpret_cast<RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                         suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
            ensure_fonts();
            extend_frame();
            layout_webview();
            redraw_caption();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            info->ptMinTrackSize.x = 640;
            info->ptMinTrackSize.y = 480;
            return 0;
        }
        case WM_SETTINGCHANGE:
            if (lparam != 0 && _wcsicmp(reinterpret_cast<const wchar_t*>(lparam), L"ImmersiveColorSet") == 0) {
                dark_ = system_dark();
                apply_chrome(true);
            }
            return 0;
        case WM_DROPFILES:
            on_drop(reinterpret_cast<HDROP>(wparam));
            return 0;
        case WM_COPYDATA: {
            const auto* data = reinterpret_cast<COPYDATASTRUCT*>(lparam);
            if (data != nullptr && data->dwData == 1 && data->lpData != nullptr && data->cbData >= sizeof(wchar_t)) {
                const auto chars = data->cbData / sizeof(wchar_t);
                std::wstring path(static_cast<const wchar_t*>(data->lpData), chars);
                while (!path.empty() && path.back() == L'\0') {
                    path.pop_back();
                }
                if (!path.empty()) {
                    open_initial(path);
                }
            }
            if (IsIconic(hwnd)) {
                ShowWindow(hwnd, SW_RESTORE);
            }
            SetForegroundWindow(hwnd);
            return TRUE;
        }
        case WM_APP_FILECHANGED:
            if (!current_.empty()) {
                SetTimer(hwnd, kTimerReload, 120, nullptr);
            }
            return 0;
        case WM_TIMER:
            if (wparam == kTimerReload) {
                KillTimer(hwnd, kTimerReload);
                if (!current_.empty() && web_ready_) {
                    std::error_code missing;
                    if (std::filesystem::exists(current_, missing) && !missing) {
                        host_.execute(L"window.mdvFileChanged&&window.mdvFileChanged()");
                    } else {
                        host_.execute(L"window.mdvFileMissing&&window.mdvFileMissing()");
                    }
                }
            } else if (wparam == kTimerSave) {
                KillTimer(hwnd, kTimerSave);
                settings_.save();
            }
            return 0;
        case WM_APP_PARSED: {
            auto* result = reinterpret_cast<LoadResult*>(lparam);
            if (!alive_) {
                delete result;
                return 0;
            }
            on_parsed(result);
            return 0;
        }
        case WM_CLOSE:
            remember_window();
            settings_.save();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            alive_ = false;
            if (loader_) {
                loader_->shutdown();
            }
            if (watcher_) {
                watcher_->stop();
            }
            host_.close();
            if (caption_font_ != nullptr) {
                DeleteObject(caption_font_);
                caption_font_ = nullptr;
            }
            if (brush_ != nullptr) {
                DeleteObject(brush_);
                brush_ = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

void App::open_initial(const std::filesystem::path& path) {
    const auto absolute = normalize_path(path);
    history_.clear();
    history_.push_back(Place{absolute, settings_.scroll_for(absolute), {}});
    history_index_ = 0;
    current_ = absolute;
    last_scroll_ = history_.back().scroll;
    watcher_->watch(hwnd_, WM_APP_FILECHANGED, current_);
    begin_load(absolute, last_scroll_, {}, false, false);
}

void App::open_link(const std::filesystem::path& path, const std::string& anchor) {
    const auto absolute = normalize_path(path);
    if (!history_.empty()) {
        history_[history_index_].scroll = last_scroll_;
        if (history_index_ + 1 < history_.size()) {
            history_.resize(history_index_ + 1);
        }
    }
    const double scroll = anchor.empty() ? settings_.scroll_for(absolute) : 0;
    history_.push_back(Place{absolute, scroll, anchor});
    history_index_ = history_.size() - 1;
    current_ = absolute;
    last_scroll_ = scroll;
    watcher_->watch(hwnd_, WM_APP_FILECHANGED, current_);
    begin_load(absolute, scroll, anchor, false, !anchor.empty());
}

void App::go(int delta) {
    if (history_.empty()) {
        return;
    }
    const auto next = static_cast<int>(history_index_) + delta;
    if (next < 0 || next >= static_cast<int>(history_.size())) {
        return;
    }
    history_[history_index_].scroll = last_scroll_;
    history_index_ = static_cast<size_t>(next);
    const auto& place = history_[history_index_];
    current_ = place.path;
    last_scroll_ = place.scroll;
    watcher_->watch(hwnd_, WM_APP_FILECHANGED, current_);
    begin_load(place.path, place.scroll, place.anchor, false, true);
}

void App::reload_current() {
    if (current_.empty()) {
        return;
    }
    begin_load(current_, last_scroll_, {}, false, true);
}

void App::begin_load(const std::filesystem::path& path, double scroll, const std::string& anchor, bool welcome,
                     bool force_scroll) {
    if (!loader_) {
        return;
    }
    LoadRequest request;
    request.path = path;
    request.scroll = scroll;
    request.anchor = anchor;
    request.dark = dark_;
    request.welcome = welcome;
    request.force_scroll = force_scroll;
    if (welcome) {
        current_.clear();
        history_.clear();
        watcher_->unwatch();
    }
    loader_->request(std::move(request));
}

void App::on_parsed(LoadResult* result) {
    std::unique_ptr<LoadResult> owned(result);
    if (result->generation != loader_->generation()) {
        return;
    }
    doc_root_ = result->doc_root;
    if (!result->title.empty()) {
        SetWindowTextW(hwnd_, result->title.c_str());
    }
    if (web_ready_ && !doc_root_.empty()) {
        host_.map_folder(L"doc.local", doc_root_, WebViewHost::FolderAccess::deny_cors);
    }
    if (result->html_file.empty()) {
        return;
    }
    navigate_ready(L"https://page.local/" + result->html_file.filename().wstring());
}

void App::navigate_ready(const std::wstring& url) {
    if (web_ready_) {
        host_.navigate(url);
        pending_url_.clear();
    } else {
        pending_url_ = url;
    }
}

void App::on_webview_ready(bool ok, const std::wstring& error) {
    if (!alive_) {
        return;
    }
    if (!ok) {
        MessageBoxW(hwnd_, (error + L"\n\n需要安装 Microsoft Edge WebView2 运行时。").c_str(), L"阅读",
                    MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd_);
        return;
    }
    web_ready_ = true;
    host_.set_background(bg_color(chrome_));
    host_.set_zoom(settings_.zoom);
    host_.map_folder(L"asset.local", assets_directory(), WebViewHost::FolderAccess::allow);
    host_.map_folder(L"page.local", cache_directory(), WebViewHost::FolderAccess::deny_cors);
    if (!doc_root_.empty()) {
        host_.map_folder(L"doc.local", doc_root_, WebViewHost::FolderAccess::deny_cors);
    }
    host_.enable_file_drop([this](const std::wstring& path) { open_dropped(path); });
    layout_webview();
    if (!pending_url_.empty()) {
        host_.navigate(pending_url_);
        pending_url_.clear();
    }
}

void App::on_navigate(const std::wstring& uri, bool user, bool& cancel) {
    if (!user) {
        return;
    }
    if (uri.starts_with(L"https://page.local/") || uri.starts_with(L"https://asset.local/")) {
        return;
    }
    cancel = true;
    if (uri.starts_with(L"https://doc.local/")) {
        if (const auto link = link_from_doc_url(uri, doc_root_)) {
            if (is_markdown(link->path)) {
                open_link(link->path, link->anchor);
            }
        }
        return;
    }
    if (uri.starts_with(L"file:")) {
        if (const auto path = path_from_file_url(uri)) {
            open_initial(*path);
        }
        return;
    }
    if (uri.starts_with(L"http://") || uri.starts_with(L"https://")) {
        ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void App::on_message(std::string_view json) {
    const auto kind = json_string(json, "kind");
    if (!kind) {
        return;
    }
    if (*kind == "scroll" && !current_.empty()) {
        const auto y = json_number(json, "y");
        if (!y || !std::isfinite(*y)) {
            return;
        }
        last_scroll_ = std::clamp(*y, 0.0, 1.0);
        settings_.set_scroll(current_, last_scroll_);
        save_soon();
        return;
    }
    if (*kind == "theme") {
        const auto name = json_string(json, "name");
        if (!name) {
            return;
        }
        if (*name == "ink") {
            chrome_ = Chrome::ink;
        } else if (*name == "mist") {
            chrome_ = Chrome::mist;
        } else if (*name == "paper") {
            chrome_ = Chrome::paper;
        } else {
            return;
        }
        apply_chrome(false);
        return;
    }
    if (*kind == "open") {
        pick_file();
        return;
    }
    if (*kind == "open-path") {
        const auto path = json_string(json, "path");
        if (!path || path->empty()) {
            return;
        }
        open_initial(path_from_utf8(*path));
        return;
    }
    if (*kind == "reload") {
        reload_current();
    }
}

void App::pick_file() {
    wchar_t buffer[32768] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof dialog;
    dialog.hwndOwner = hwnd_;
    dialog.lpstrFilter = L"Markdown\0*.md;*.markdown;*.mdown;*.mkd\0\0";
    dialog.lpstrFile = buffer;
    dialog.nMaxFile = static_cast<DWORD>(sizeof buffer / sizeof buffer[0]);
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"打开 Markdown";
    if (GetOpenFileNameW(&dialog) == TRUE) {
        open_initial(buffer);
    }
}

bool App::on_key(unsigned key, bool ctrl, bool alt, bool shift, bool repeated) {
    if (repeated) {
        return false;
    }
    if (ctrl && key == 'F') {
        host_.execute(L"window.mdvFind&&window.mdvFind()");
        return true;
    }
    if (ctrl && key == 'K') {
        host_.execute(L"window.mdvPalette&&window.mdvPalette()");
        return true;
    }
    if (ctrl && key == 'P') {
        host_.print_page();
        return true;
    }
    if (ctrl && key == '0') {
        settings_.zoom = 1;
        host_.set_zoom(1);
        save_soon();
        return true;
    }
    if (ctrl && (key == VK_OEM_PLUS || key == VK_ADD)) {
        settings_.zoom *= 1.1;
        host_.set_zoom(settings_.zoom);
        return true;
    }
    if (ctrl && (key == VK_OEM_MINUS || key == VK_SUBTRACT)) {
        settings_.zoom /= 1.1;
        host_.set_zoom(settings_.zoom);
        return true;
    }
    if (ctrl && key == VK_OEM_5) {
        host_.execute(L"window.mdvOutline&&window.mdvOutline()");
        return true;
    }
    if (alt && !ctrl && key == VK_LEFT) {
        go(-1);
        return true;
    }
    if (alt && !ctrl && key == VK_RIGHT) {
        go(1);
        return true;
    }
    if (key == VK_F5) {
        reload_current();
        return true;
    }
    (void)shift;
    return false;
}

void App::on_drop(HDROP drop) {
    const UINT chars = DragQueryFileW(drop, 0, nullptr, 0);
    std::wstring path;
    if (chars > 0) {
        path.assign(static_cast<size_t>(chars) + 1, L'\0');
        const UINT copied = DragQueryFileW(drop, 0, path.data(), chars + 1);
        path.resize(copied);
    }
    DragFinish(drop);
    open_dropped(path);
}

void App::open_dropped(const std::wstring& path) {
    if (path.empty() || !is_markdown(path)) {
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (path == last_drop_path_ && now - last_drop_tick_ < 800) {
        return;
    }
    last_drop_path_ = path;
    last_drop_tick_ = now;
    open_initial(path);
}

void App::extend_frame() {
    // A 1px bottom margin keeps the drop shadow without turning the client into glass.
    // Negative margins make the whole window transparent on Windows 10, so caption
    // fills disappear and whatever sits behind the window shows through.
    const MARGINS margins{0, 0, 0, 1};
    DwmExtendFrameIntoClientArea(hwnd_, &margins);
    DWM_BLURBEHIND blur{};
    blur.dwFlags = DWM_BB_ENABLE;
    blur.fEnable = FALSE;
    DwmEnableBlurBehindWindow(hwnd_, &blur);
}

void App::suppress_system_caption() {
    WTA_OPTIONS options{};
    options.dwFlags = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON | WTNCA_NOSYSMENU;
    options.dwMask = options.dwFlags;
    SetWindowThemeAttribute(hwnd_, WTA_NONCLIENT, &options, sizeof options);
}

int App::caption_px() const {
    const int dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
    return MulDiv(40, dpi, 96);
}

RECT App::caption_button(int index) const {
    RECT client{};
    if (hwnd_ != nullptr) {
        GetClientRect(hwnd_, &client);
    }
    const int dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
    const int width = MulDiv(46, dpi, 96);
    RECT button{};
    button.top = 0;
    button.bottom = caption_px();
    button.right = client.right - (2 - index) * width;
    button.left = button.right - width;
    return button;
}

int App::caption_button_at(POINT pt) const {
    if (pt.y < 0 || pt.y >= caption_px()) {
        return 0;
    }
    for (int index = 0; index < 3; ++index) {
        const RECT button = caption_button(index);
        if (pt.x >= button.left && pt.x < button.right) {
            return index + 1;
        }
    }
    return 0;
}

void App::layout_webview() {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT bounds{};
    GetClientRect(hwnd_, &bounds);
    bounds.top += caption_px();
    if (bounds.top > bounds.bottom) {
        bounds.top = bounds.bottom;
    }
    host_.resize(bounds);
}

void App::redraw_caption() {
    if (hwnd_ == nullptr) {
        return;
    }
    RECT strip{};
    GetClientRect(hwnd_, &strip);
    strip.bottom = caption_px();
    InvalidateRect(hwnd_, &strip, FALSE);
}

void App::ensure_fonts() {
    const int dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
    if (caption_font_ != nullptr && fonts_dpi_ == dpi) {
        return;
    }
    if (caption_font_ != nullptr) {
        DeleteObject(caption_font_);
        caption_font_ = nullptr;
    }
    caption_font_ = CreateFontW(-MulDiv(13, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    fonts_dpi_ = dpi;
}

void App::fill_caption_rect(HDC dc, int left, int top, int right, int bottom, COLORREF color) const {
    if (right <= left || bottom <= top) {
        return;
    }
    RECT rect{left, top, right, bottom};
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void App::draw_caption_glyph(HDC dc, const RECT& button, int id, COLORREF color) const {
    const int dpi = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
    const int unit = std::max(6, MulDiv(6, dpi, 96));
    const int thick = std::max(2, MulDiv(2, dpi, 96));
    const int cx = (button.left + button.right) / 2;
    const int cy = (button.top + button.bottom) / 2;
    if (id == 1) {
        fill_caption_rect(dc, cx - unit, cy - thick / 2, cx + unit + 1, cy - thick / 2 + thick, color);
        return;
    }
    if (id == 2 && IsZoomed(hwnd_)) {
        const int size = std::max(3, unit - 1);
        fill_caption_rect(dc, cx - size + 2, cy - size, cx + size + 1, cy - size + thick, color);
        fill_caption_rect(dc, cx + size - thick + 1, cy - size, cx + size + 1, cy + size - 1, color);
        fill_caption_rect(dc, cx - size, cy - size + 2, cx - size + thick, cy + size + 1, color);
        fill_caption_rect(dc, cx - size, cy + size - thick + 1, cx + size - 1, cy + size + 1, color);
        fill_caption_rect(dc, cx - size + 2, cy - size + 2, cx + size - 1, cy - size + 2 + thick, color);
        return;
    }
    if (id == 2) {
        fill_caption_rect(dc, cx - unit, cy - unit, cx + unit + 1, cy - unit + thick, color);
        fill_caption_rect(dc, cx + unit - thick + 1, cy - unit, cx + unit + 1, cy + unit + 1, color);
        fill_caption_rect(dc, cx - unit, cy - unit, cx - unit + thick, cy + unit + 1, color);
        fill_caption_rect(dc, cx - unit, cy + unit - thick + 1, cx + unit + 1, cy + unit + 1, color);
        return;
    }
    for (int step = -unit; step <= unit; ++step) {
        fill_caption_rect(dc, cx + step, cy + step - thick / 2, cx + step + thick, cy + step - thick / 2 + thick, color);
        fill_caption_rect(dc, cx + step, cy - step - thick / 2, cx + step + thick, cy - step - thick / 2 + thick, color);
    }
}

void App::paint_window(HDC dc) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (brush_ != nullptr) {
        FillRect(dc, &client, brush_);
    }
    ensure_fonts();
    const int dpi = GetDpiForWindow(hwnd_);
    const int cap = caption_px();
    const bool active = GetActiveWindow() == hwnd_;
    const COLORREF ink = active ? ink_color(chrome_) : ink_soft_color(chrome_);
    const int icon_px = MulDiv(16, dpi, 96);
    const int pad = MulDiv(14, dpi, 96);
    const auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd_, GCLP_HICONSM));
    if (icon != nullptr) {
        DrawIconEx(dc, pad, (cap - icon_px) / 2, icon, icon_px, icon_px, 0, nullptr, DI_NORMAL);
    }
    wchar_t title[512];
    const int copied = GetWindowTextW(hwnd_, title, 512);
    if (copied > 0 && caption_font_ != nullptr) {
        RECT text{pad + icon_px + MulDiv(10, dpi, 96), 0, caption_button(0).left - MulDiv(8, dpi, 96), cap};
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ink);
        const auto previous = SelectObject(dc, caption_font_);
        DrawTextW(dc, title, copied, &text, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        SelectObject(dc, previous);
    }
    for (int index = 0; index < 3; ++index) {
        const RECT button = caption_button(index);
        const int id = index + 1;
        const bool hot = hover_button_ == id && (pressed_button_ == 0 || pressed_button_ == id);
        if (hot) {
            const COLORREF fill = id == 3 ? RGB(0xC4, 0x2B, 0x1C) : hover_color(chrome_);
            HBRUSH fill_brush = CreateSolidBrush(fill);
            FillRect(dc, &button, fill_brush);
            DeleteObject(fill_brush);
        }
        const COLORREF glyph = id == 3 && hot ? RGB(255, 255, 255) : ink;
        draw_caption_glyph(dc, button, id, glyph);
    }
}

void App::apply_chrome(bool page) {
    const COLORREF desk = bg_color(chrome_);
    const COLORREF frame = frame_color(chrome_);
    const COLORREF ink = ink_color(chrome_);
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &desk, sizeof desk);
    DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &ink, sizeof ink);
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &frame, sizeof frame);
    extend_frame();
    const HBRUSH next = CreateSolidBrush(desk);
    SetClassLongPtrW(hwnd_, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(next));
    if (brush_ != nullptr) {
        DeleteObject(brush_);
    }
    brush_ = next;
    host_.set_background(desk);
    if (page && web_ready_) {
        host_.execute(dark_ ? L"window.mdvSystemTheme&&window.mdvSystemTheme('dark')"
                            : L"window.mdvSystemTheme&&window.mdvSystemTheme('light')");
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void App::remember_window() {
    WINDOWPLACEMENT placement{};
    placement.length = sizeof placement;
    if (GetWindowPlacement(hwnd_, &placement) == FALSE) {
        return;
    }
    const RECT& rect = placement.rcNormalPosition;
    settings_.window.x = rect.left;
    settings_.window.y = rect.top;
    settings_.window.width = rect.right - rect.left;
    settings_.window.height = rect.bottom - rect.top;
    settings_.window.maximized = placement.showCmd == SW_SHOWMAXIMIZED;
    settings_.window.valid = settings_.window.width >= 480 && settings_.window.height >= 320;
}

void App::save_soon() { SetTimer(hwnd_, kTimerSave, 800, nullptr); }

int run_app(const std::optional<std::filesystem::path>& initial, int show) {
    App app;
    return app.run(initial, show);
}

}  // namespace mdv
