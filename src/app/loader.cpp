#include "app/loader.hpp"

#include "app/messages.hpp"
#include "document/compose.hpp"
#include "document/escape.hpp"
#include "document/render.hpp"
#include "document/rewrite.hpp"
#include "platform/encoding.hpp"
#include "platform/paths.hpp"
#include "platform/text.hpp"

#include <windows.h>
#include <objbase.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <exception>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace mdv {
namespace {

constexpr char kWelcome[] =
    "<div class=\"empty\">\n"
    "<button type=\"button\" class=\"empty-target\" id=\"empty-pick\">拖入 .md 文件，或点击选择</button>\n"
    "<div class=\"recent\" id=\"recent\" hidden>\n"
    "<p class=\"recent-label\">最近打开</p>\n"
    "<ul></ul>\n"
    "</div>\n"
    "</div>\n";

std::string format_mtime(const std::filesystem::path& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(path, error);
    if (error) {
        return {};
    }
    const auto system = std::chrono::clock_cast<std::chrono::system_clock>(stamp);
    const std::time_t time = std::chrono::system_clock::to_time_t(system);
    std::tm local{};
    if (localtime_s(&local, &time) != 0) {
        return {};
    }
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    return buffer;
}

std::string stem_utf8(const std::filesystem::path& path) {
    return wide_to_utf8(path.stem().wstring());
}

bool write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return static_cast<bool>(output);
}

LoadResult* build(const LoadRequest& request, uint64_t generation) {
    auto* result = new LoadResult{};
    result->generation = generation;
    result->source = request.path;
    result->welcome = request.welcome;

    std::string fragment;
    std::string title = "阅读";
    std::filesystem::path root;
    if (request.welcome || request.path.empty()) {
        fragment = kWelcome;
        result->welcome = true;
    } else {
        const FileText file = read_markdown_file(request.path);
        if (!file.ok) {
            fragment = "<h1>无法打开</h1>\n<p>";
            fragment += escape_html(wide_to_utf8(file.error));
            fragment += "</p>\n";
            title = "无法打开";
        } else {
            const Rendered rendered = render_markdown(file.utf8);
            const Rewritten rewritten = rewrite_local_urls(rendered.html, request.path);
            fragment = rewritten.html;
            root = rewritten.root;
            if (fragment.empty() && !file.utf8.empty()) {
                fragment = "<p class=\"parse-note\">无法解析，下面是原文。</p>\n<pre class=\"raw-fallback\">";
                fragment += escape_html(file.utf8);
                fragment += "</pre>\n";
                title = stem_utf8(request.path);
            } else if (fragment.empty()) {
                fragment = "<p class=\"quiet\">这份文档是空的。</p>\n";
                title = rendered.title.empty() ? stem_utf8(request.path) : rendered.title;
            } else {
                title = rendered.title.empty() ? stem_utf8(request.path) : rendered.title;
            }
        }
    }

    const std::string file_name =
        request.path.empty() ? std::string() : wide_to_utf8(request.path.filename().wstring());
    const std::string source = request.path.empty() ? std::string() : wide_to_utf8(request.path.generic_wstring());
    const std::string modified = format_mtime(request.path);
    ComposeOptions options;
    options.dark = request.dark;
    options.scroll = request.scroll;
    options.anchor = request.anchor;
    options.force_scroll = request.force_scroll;
    options.welcome = request.welcome || request.path.empty();
    options.file_name = file_name;
    options.modified = modified;
    options.source = source;
    const Page page = compose_page(fragment, title, options);
    const auto file = cache_directory() / ("page-" + std::to_string(generation) + ".html");
    if (!write_text(file, page.html)) {
        fragment = "<h1>无法打开</h1>\n<p>写不出阅读页面。</p>\n";
        title = "无法打开";
        const Page fallback = compose_page(fragment, title, options);
        const auto rescue = cache_directory() / "page-error.html";
        write_text(rescue, fallback.html);
        result->html_file = rescue;
    } else {
        result->html_file = file;
    }
    result->doc_root = root;
    result->title = utf8_to_wide(title);
    if (result->title.empty()) {
        result->title = L"阅读";
    }
    return result;
}

}  // namespace

struct Loader::Impl {
    HWND hwnd = nullptr;
    std::mutex mutex;
    std::condition_variable cv;
    std::thread thread;
    std::optional<LoadRequest> pending;
    uint64_t generation = 0;
    bool stop = false;
};

Loader::Loader(HWND hwnd) : impl_(std::make_unique<Impl>()) {
    impl_->hwnd = hwnd;
    impl_->thread = std::thread([this] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        while (true) {
            LoadRequest request;
            uint64_t generation = 0;
            {
                std::unique_lock lock(impl_->mutex);
                impl_->cv.wait(lock, [this] { return impl_->stop || impl_->pending.has_value(); });
                if (impl_->stop) {
                    break;
                }
                request = std::move(*impl_->pending);
                impl_->pending.reset();
                generation = impl_->generation;
            }
            LoadResult* result = nullptr;
            try {
                result = build(request, generation);
            } catch (const std::exception& ex) {
                result = new LoadResult{};
                result->generation = generation;
                result->title = L"无法打开";
                ComposeOptions options;
                options.dark = request.dark;
                const auto page = compose_page(std::string("<h1>无法打开</h1>\n<p>") + escape_html(ex.what()) + "</p>\n",
                                                "无法打开", options);
                const auto file = cache_directory() / "page-error.html";
                std::ofstream output(file, std::ios::binary | std::ios::trunc);
                output << page.html;
                result->html_file = file;
            }
            if (result != nullptr && !PostMessageW(impl_->hwnd, WM_APP_PARSED, 0, reinterpret_cast<LPARAM>(result))) {
                delete result;
            }
        }
        CoUninitialize();
    });
}

Loader::~Loader() { shutdown(); }

uint64_t Loader::request(LoadRequest request) {
    uint64_t generation = 0;
    {
        std::lock_guard lock(impl_->mutex);
        ++impl_->generation;
        generation = impl_->generation;
        impl_->pending = std::move(request);
    }
    impl_->cv.notify_one();
    return generation;
}

uint64_t Loader::generation() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->generation;
}

void Loader::shutdown() {
    if (!impl_) {
        return;
    }
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stop = true;
        impl_->pending.reset();
    }
    impl_->cv.notify_one();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

}  // namespace mdv
