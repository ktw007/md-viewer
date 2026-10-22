#include "app/window.hpp"

#include <windows.h>
#include <ole2.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <optional>
#include <string>

namespace {

std::optional<std::filesystem::path> initial_path() {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return std::nullopt;
    }
    std::optional<std::filesystem::path> path;
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == L'-') {
            continue;
        }
        path = argv[i];
        break;
    }
    LocalFree(argv);
    return path;
}

bool send_to_existing(const std::optional<std::filesystem::path>& path) {
    HWND other = nullptr;
    for (int i = 0; i < 50 && other == nullptr; ++i) {
        other = FindWindowW(L"MdViewerWindow", nullptr);
        if (other == nullptr) {
            Sleep(40);
        }
    }
    if (other == nullptr) {
        return false;
    }
    if (path) {
        const std::wstring text = path->wstring();
        COPYDATASTRUCT data{};
        data.dwData = 1;
        data.cbData = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
        data.lpData = const_cast<wchar_t*>(text.c_str());
        SendMessageW(other, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data));
    }
    if (IsIconic(other)) {
        ShowWindow(other, SW_RESTORE);
    }
    SetForegroundWindow(other);
    return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (FAILED(OleInitialize(nullptr))) {
        return 1;
    }
    SetCurrentProcessExplicitAppUserModelID(L"MdViewer.Reader");

    const auto path = initial_path();
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MdViewer.SingleInstance.v1");
    const bool primary = mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
    if (!primary) {
        send_to_existing(path);
        if (mutex != nullptr) {
            CloseHandle(mutex);
        }
        OleUninitialize();
        return 0;
    }

    const int code = mdv::run_app(path, show);
    OleUninitialize();
    return code;
}
