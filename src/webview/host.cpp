#include "webview/host.hpp"

#include <ole2.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <WebView2.h>
#include <wil/com.h>
#include <wrl.h>

#include <optional>
#include <string>
#include <vector>

namespace mdv {
namespace {

std::wstring error_text(const wchar_t* prefix, HRESULT hr) {
    wchar_t buffer[160];
    swprintf_s(buffer, L"%s（0x%08X）", prefix, static_cast<unsigned>(hr));
    return buffer;
}

std::string wide_utf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (bytes <= 0) {
        return {};
    }
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), bytes, nullptr,
                        nullptr);
    return utf8;
}

bool has_hdrop(IDataObject* data) {
    if (data == nullptr) {
        return false;
    }
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return SUCCEEDED(data->QueryGetData(&format));
}

std::wstring first_dropped_file(IDataObject* data) {
    if (data == nullptr) {
        return {};
    }
    FORMATETC format{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (FAILED(data->GetData(&format, &medium))) {
        return {};
    }
    std::wstring path;
    const auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
    if (drop != nullptr) {
        const UINT chars = DragQueryFileW(drop, 0, nullptr, 0);
        if (chars > 0) {
            path.assign(static_cast<size_t>(chars) + 1, L'\0');
            const UINT copied = DragQueryFileW(drop, 0, path.data(), chars + 1);
            path.resize(copied);
        }
        GlobalUnlock(medium.hGlobal);
    }
    ReleaseStgMedium(&medium);
    return path;
}

std::optional<std::wstring> path_from_file_uri(std::wstring uri) {
    const auto hash = uri.find(L'#');
    if (hash != std::wstring::npos) {
        uri.resize(hash);
    }
    const auto query = uri.find(L'?');
    if (query != std::wstring::npos) {
        uri.resize(query);
    }
    if (uri.size() < 5 || _wcsnicmp(uri.c_str(), L"file:", 5) != 0) {
        return std::nullopt;
    }
    DWORD count = 32768;
    std::wstring path(count, L'\0');
    if (FAILED(PathCreateFromUrlW(uri.c_str(), path.data(), &count, 0))) {
        return std::nullopt;
    }
    path.resize(count);
    while (!path.empty() && path.back() == L'\0') {
        path.pop_back();
    }
    if (path.empty()) {
        return std::nullopt;
    }
    return path;
}

struct FileDropTarget : IDropTarget {
    explicit FileDropTarget(std::function<void(std::wstring)> callback) : callback_(std::move(callback)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

    ULONG STDMETHODCALLTYPE Release() override {
        const auto left = InterlockedDecrement(&refs_);
        if (left == 0) {
            delete this;
        }
        return static_cast<ULONG>(left);
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        hot_ = has_hdrop(data);
        if (effect != nullptr) {
            *effect = hot_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override {
        if (effect != nullptr) {
            *effect = hot_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        hot_ = false;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL, DWORD* effect) override {
        std::wstring path = first_dropped_file(data);
        if (effect != nullptr) {
            *effect = path.empty() ? DROPEFFECT_NONE : DROPEFFECT_COPY;
        }
        if (!path.empty() && callback_) {
            callback_(std::move(path));
        }
        return S_OK;
    }

    std::function<void(std::wstring)> callback_;
    bool hot_ = false;
    LONG refs_ = 1;
};

void for_each_child(HWND parent, const std::function<void(HWND)>& visit) {
    struct Pack {
        const std::function<void(HWND)>* visit;
    } pack{&visit};
    EnumChildWindows(
        parent,
        [](HWND child, LPARAM param) -> BOOL {
            (*reinterpret_cast<Pack*>(param)->visit)(child);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&pack));
}

}  // namespace

struct WebViewHost::Impl {
    HWND parent = nullptr;
    wil::com_ptr<ICoreWebView2Environment> environment;
    wil::com_ptr<ICoreWebView2Controller> controller;
    wil::com_ptr<ICoreWebView2> webview;
    ReadyCallback ready;
    NavigateCallback navigate_cb;
    MessageCallback message_cb;
    KeyCallback key_cb;
    std::function<void(double)> zoom_cb;
    std::function<void()> navigated_cb;
    std::function<void(std::wstring)> file_cb;
    wil::com_ptr<IDropTarget> drop_target;
    std::vector<HWND> drop_hwnds;
    UINT_PTR drop_timer = 0;
    WebViewHost* self = nullptr;
    bool closed = false;
};

WebViewHost::WebViewHost() : impl_(std::make_shared<Impl>()) { impl_->self = this; }

WebViewHost::~WebViewHost() { close(); }

void WebViewHost::initialize(HWND parent, std::filesystem::path user_data, ReadyCallback ready) {
    impl_->parent = parent;
    impl_->ready = std::move(ready);
    std::weak_ptr<Impl> weak = impl_;
    const HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, user_data.c_str(), nullptr,
        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [weak](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
                const auto impl = weak.lock();
                if (!impl || impl->closed) {
                    return S_OK;
                }
                if (FAILED(result) || environment == nullptr) {
                    if (impl->ready) {
                        impl->ready(false, error_text(L"WebView2 运行时没有就绪", result));
                    }
                    return S_OK;
                }
                impl->environment = environment;
                return environment->CreateCoreWebView2Controller(
                    impl->parent,
                    Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                        [weak](HRESULT created, ICoreWebView2Controller* controller) -> HRESULT {
                            const auto impl = weak.lock();
                            if (!impl || impl->closed) {
                                return S_OK;
                            }
                            if (FAILED(created) || controller == nullptr) {
                                if (impl->ready) {
                                    impl->ready(false, error_text(L"无法创建页面", created));
                                }
                                return S_OK;
                            }
                            impl->controller = controller;
                            impl->controller->get_CoreWebView2(&impl->webview);
                            if (!impl->webview) {
                                if (impl->ready) {
                                    impl->ready(false, L"页面控件没有返回。");
                                }
                                return S_OK;
                            }

                            wil::com_ptr<ICoreWebView2Settings> settings;
                            impl->webview->get_Settings(&settings);
                            if (settings) {
                                settings->put_IsStatusBarEnabled(FALSE);
                                settings->put_AreDefaultScriptDialogsEnabled(FALSE);
                                settings->put_AreDevToolsEnabled(FALSE);
                                settings->put_IsZoomControlEnabled(TRUE);
                                settings->put_AreDefaultContextMenusEnabled(TRUE);
                                settings->put_IsBuiltInErrorPageEnabled(TRUE);
                                settings->put_AreHostObjectsAllowed(FALSE);
                                wil::com_ptr<ICoreWebView2Settings6> swipe;
                                if (SUCCEEDED(settings->QueryInterface(IID_PPV_ARGS(&swipe))) && swipe) {
                                    swipe->put_IsSwipeNavigationEnabled(FALSE);
                                }
                            }

                            RECT bounds{};
                            GetClientRect(impl->parent, &bounds);
                            impl->controller->put_Bounds(bounds);

                            EventRegistrationToken token{};
                            impl->webview->add_NavigationStarting(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
                                    [weak](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                                        const auto impl = weak.lock();
                                        if (!impl || impl->closed || !impl->navigate_cb) {
                                            return S_OK;
                                        }
                                        wil::unique_cotaskmem_string uri;
                                        args->get_Uri(&uri);
                                        BOOL user = FALSE;
                                        args->get_IsUserInitiated(&user);
                                        bool cancel = false;
                                        impl->navigate_cb(uri.get() != nullptr ? uri.get() : L"", user == TRUE, cancel);
                                        if (cancel) {
                                            args->put_Cancel(TRUE);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            impl->webview->add_NavigationCompleted(
                                Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                    [weak](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*) -> HRESULT {
                                        const auto impl = weak.lock();
                                        if (!impl || impl->closed) {
                                            return S_OK;
                                        }
                                        if (impl->self != nullptr) {
                                            impl->self->install_file_drop();
                                        }
                                        if (impl->navigated_cb) {
                                            impl->navigated_cb();
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            wil::com_ptr<ICoreWebView2_4> downloads;
                            if (SUCCEEDED(impl->webview->QueryInterface(IID_PPV_ARGS(&downloads))) && downloads) {
                                downloads->add_DownloadStarting(
                                    Microsoft::WRL::Callback<ICoreWebView2DownloadStartingEventHandler>(
                                        [weak](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs* args) -> HRESULT {
                                            if (args == nullptr) {
                                                return S_OK;
                                            }
                                            args->put_Cancel(TRUE);
                                            args->put_Handled(TRUE);
                                            const auto impl = weak.lock();
                                            if (!impl || impl->closed || !impl->file_cb) {
                                                return S_OK;
                                            }
                                            wil::com_ptr<ICoreWebView2DownloadOperation> operation;
                                            if (FAILED(args->get_DownloadOperation(&operation)) || !operation) {
                                                return S_OK;
                                            }
                                            wil::unique_cotaskmem_string uri;
                                            if (FAILED(operation->get_Uri(&uri)) || uri.get() == nullptr) {
                                                return S_OK;
                                            }
                                            if (const auto path = path_from_file_uri(uri.get())) {
                                                impl->file_cb(*path);
                                            }
                                            return S_OK;
                                        })
                                        .Get(),
                                    &token);
                            }
                            impl->webview->add_WebMessageReceived(
                                Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                    [weak](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                        const auto impl = weak.lock();
                                        if (!impl || impl->closed || !impl->message_cb) {
                                            return S_OK;
                                        }
                                        wil::unique_cotaskmem_string json;
                                        if (SUCCEEDED(args->get_WebMessageAsJson(&json)) && json.get() != nullptr) {
                                            impl->message_cb(wide_utf8(json.get()));
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            impl->controller->add_AcceleratorKeyPressed(
                                Microsoft::WRL::Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                                    [weak](ICoreWebView2Controller*,
                                           ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                                        const auto impl = weak.lock();
                                        if (!impl || impl->closed || !impl->key_cb) {
                                            return S_OK;
                                        }
                                        COREWEBVIEW2_KEY_EVENT_KIND kind = COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN;
                                        args->get_KeyEventKind(&kind);
                                        if (kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN &&
                                            kind != COREWEBVIEW2_KEY_EVENT_KIND_SYSTEM_KEY_DOWN) {
                                            return S_OK;
                                        }
                                        UINT key = 0;
                                        args->get_VirtualKey(&key);
                                        INT lparam = 0;
                                        args->get_KeyEventLParam(&lparam);
                                        const bool repeated = (lparam & (1 << 30)) != 0;
                                        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                                        const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
                                        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                                        if (impl->key_cb(key, ctrl, alt, shift, repeated)) {
                                            args->put_Handled(TRUE);
                                        }
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);
                            impl->controller->add_ZoomFactorChanged(
                                Microsoft::WRL::Callback<ICoreWebView2ZoomFactorChangedEventHandler>(
                                    [weak](ICoreWebView2Controller* sender, IUnknown*) -> HRESULT {
                                        const auto impl = weak.lock();
                                        if (!impl || impl->closed || !impl->zoom_cb || sender == nullptr) {
                                            return S_OK;
                                        }
                                        double zoom = 1;
                                        sender->get_ZoomFactor(&zoom);
                                        impl->zoom_cb(zoom);
                                        return S_OK;
                                    })
                                    .Get(),
                                &token);

                            impl->controller->put_IsVisible(TRUE);
                            if (impl->ready) {
                                impl->ready(true, L"");
                            }
                            return S_OK;
                        })
                        .Get());
            })
            .Get());
    if (FAILED(hr) && impl_->ready) {
        impl_->ready(false, error_text(L"WebView2 运行时没有就绪", hr));
    }
}

void WebViewHost::resize(RECT bounds) {
    if (impl_->controller) {
        impl_->controller->put_Bounds(bounds);
    }
}

void WebViewHost::set_background(COLORREF color) {
    if (!impl_->controller) {
        return;
    }
    wil::com_ptr<ICoreWebView2Controller2> controller;
    if (FAILED(impl_->controller->QueryInterface(IID_PPV_ARGS(&controller))) || !controller) {
        return;
    }
    COREWEBVIEW2_COLOR paper{255, GetRValue(color), GetGValue(color), GetBValue(color)};
    controller->put_DefaultBackgroundColor(paper);
}

void WebViewHost::map_folder(const wchar_t* host, const std::filesystem::path& folder, FolderAccess access) {
    if (!impl_->webview) {
        return;
    }
    wil::com_ptr<ICoreWebView2_3> webview;
    if (FAILED(impl_->webview->QueryInterface(IID_PPV_ARGS(&webview))) || !webview) {
        return;
    }
    const auto kind = access == FolderAccess::allow ? COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW
                                                     : COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS;
    webview->SetVirtualHostNameToFolderMapping(host, folder.c_str(), kind);
}

void WebViewHost::navigate(const std::wstring& url) {
    if (impl_->webview) {
        impl_->webview->Navigate(url.c_str());
    }
}

void WebViewHost::execute(const std::wstring& script) {
    if (impl_->webview) {
        impl_->webview->ExecuteScript(script.c_str(), nullptr);
    }
}

void WebViewHost::print_page() {
    if (!impl_->webview) {
        return;
    }
    wil::com_ptr<ICoreWebView2_16> webview;
    if (SUCCEEDED(impl_->webview->QueryInterface(IID_PPV_ARGS(&webview))) && webview) {
        webview->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER);
        return;
    }
    execute(L"window.print()");
}

void WebViewHost::set_zoom(double zoom) {
    if (!impl_->controller) {
        return;
    }
    if (zoom < 0.5) {
        zoom = 0.5;
    }
    if (zoom > 3.0) {
        zoom = 3.0;
    }
    impl_->controller->put_ZoomFactor(zoom);
}

void WebViewHost::on_navigate(NavigateCallback callback) { impl_->navigate_cb = std::move(callback); }

void WebViewHost::on_message(MessageCallback callback) { impl_->message_cb = std::move(callback); }

void WebViewHost::on_key(KeyCallback callback) { impl_->key_cb = std::move(callback); }

void WebViewHost::on_zoom(std::function<void(double)> callback) { impl_->zoom_cb = std::move(callback); }

void WebViewHost::on_navigated(std::function<void()> callback) { impl_->navigated_cb = std::move(callback); }

void WebViewHost::install_file_drop() {
    for (HWND hwnd : impl_->drop_hwnds) {
        RevokeDragDrop(hwnd);
    }
    impl_->drop_hwnds.clear();
    if (impl_->closed || impl_->parent == nullptr || !impl_->drop_target) {
        return;
    }
    for_each_child(impl_->parent, [this](HWND child) {
        RevokeDragDrop(child);
        if (SUCCEEDED(RegisterDragDrop(child, impl_->drop_target.get()))) {
            impl_->drop_hwnds.push_back(child);
        }
    });
}

void WebViewHost::enable_file_drop(std::function<void(const std::wstring& path)> callback) {
    impl_->file_cb = std::move(callback);
    if (impl_->parent == nullptr) {
        return;
    }
    std::weak_ptr<Impl> weak = impl_;
    auto* target = new FileDropTarget([weak](std::wstring path) {
        const auto impl = weak.lock();
        if (impl && !impl->closed && impl->file_cb) {
            impl->file_cb(std::move(path));
        }
    });
    impl_->drop_target.attach(target);
    install_file_drop();
    if (impl_->drop_timer != 0) {
        KillTimer(nullptr, impl_->drop_timer);
    }
    struct Rehook {
        static std::weak_ptr<Impl>& slot() {
            static std::weak_ptr<Impl> value;
            return value;
        }
        static void CALLBACK tick(HWND, UINT, UINT_PTR id, DWORD) {
            KillTimer(nullptr, id);
            const auto impl = slot().lock();
            if (!impl || impl->closed || impl->self == nullptr) {
                return;
            }
            impl->drop_timer = 0;
            impl->self->install_file_drop();
        }
    };
    Rehook::slot() = impl_;
    impl_->drop_timer = SetTimer(nullptr, 0, 250, Rehook::tick);
}

void WebViewHost::notify_moved() {
    if (impl_->controller) {
        impl_->controller->NotifyParentWindowPositionChanged();
    }
}

bool WebViewHost::ready() const { return impl_ && impl_->webview != nullptr; }

void WebViewHost::close() {
    if (!impl_ || impl_->closed) {
        return;
    }
    impl_->closed = true;
    impl_->self = nullptr;
    if (impl_->drop_timer != 0) {
        KillTimer(nullptr, impl_->drop_timer);
        impl_->drop_timer = 0;
    }
    for (HWND hwnd : impl_->drop_hwnds) {
        RevokeDragDrop(hwnd);
    }
    impl_->drop_hwnds.clear();
    impl_->drop_target.reset();
    impl_->file_cb = nullptr;
    if (impl_->controller) {
        impl_->controller->Close();
    }
    impl_->webview.reset();
    impl_->controller.reset();
    impl_->environment.reset();
}

}  // namespace mdv
