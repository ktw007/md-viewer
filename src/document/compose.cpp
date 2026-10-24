#include "document/compose.hpp"

#include "document/escape.hpp"

#include <locale>
#include <sstream>

#ifndef MDV_ASSET_VERSION
#define MDV_ASSET_VERSION "7"
#endif

namespace mdv {
namespace {

std::string format_scroll(double scroll) {
    if (!(scroll >= 0.0) || scroll > 1.0) {
        scroll = 0;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream.setf(std::ios::fixed);
    stream.precision(6);
    stream << scroll;
    return stream.str();
}

void append_attr(std::string& html, std::string_view name, std::string_view value) {
    if (value.empty()) {
        return;
    }
    html += " data-";
    html += name;
    html += "=\"";
    html += escape_html(value);
    html += "\"";
}

}  // namespace

Page compose_page(std::string_view fragment, std::string_view title, const ComposeOptions& options) {
    const auto safe_title = escape_html(title);
    const auto asset = std::string(options.asset_base);
    const char* theme = options.dark ? "dark" : "light";
    Page page;
    page.html.reserve(fragment.size() + 3072);
    page.html += "<!DOCTYPE html>\n<html lang=\"zh-CN\" data-theme=\"";
    page.html += theme;
    page.html += "\" data-system=\"";
    page.html += theme;
    page.html += "\">\n<head>\n<meta charset=\"utf-8\">\n";
    page.html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    if (options.content_security_policy) {
        page.html +=
            "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
            "script-src https://asset.local; style-src https://asset.local; font-src https://asset.local; "
            "img-src https: http: data:; media-src https: http: data:; connect-src 'none'; "
            "base-uri 'none'; form-action 'none'; frame-src 'none'\">\n";
    }
    page.html += "<title>";
    page.html += safe_title;
    page.html += "</title>\n<link rel=\"stylesheet\" href=\"";
    page.html += escape_html(asset);
    page.html += "/viewer.css?v=" MDV_ASSET_VERSION "\">\n</head>\n<body data-scroll=\"";
    page.html += format_scroll(options.scroll);
    page.html += "\" data-anchor=\"";
    page.html += escape_html(options.anchor);
    page.html += "\"";
    if (options.force_scroll) {
        page.html += " data-resume=\"force\"";
    }
    if (options.welcome) {
        page.html += " data-welcome=\"1\"";
    }
    append_attr(page.html, "file", options.file_name);
    append_attr(page.html, "modified", options.modified);
    append_attr(page.html, "path", options.source);
    page.html += ">\n";
    page.html +=
        "<a class=\"skip\" href=\"#doc\">跳到正文</a>\n"
        "<header class=\"topbar\">\n"
        "<button type=\"button\" class=\"icon-button\" id=\"nav-toggle\" aria-controls=\"outline\" "
        "aria-expanded=\"true\" aria-label=\"大纲\">"
        "<svg width=\"16\" height=\"16\" viewBox=\"0 0 16 16\" aria-hidden=\"true\">"
        "<rect x=\"1.4\" y=\"2\" width=\"4.2\" height=\"12\" fill=\"none\" stroke=\"currentColor\" "
        "stroke-width=\"1.2\"/>"
        "<path d=\"M8 4.5h6.5M8 8h6.5M8 11.5h4.5\" fill=\"none\" stroke=\"currentColor\" stroke-width=\"1.2\"/>"
        "</svg></button>\n"
        "<div class=\"crumb\" id=\"crumb\"><span class=\"crumb-file\" id=\"crumb-file\"></span>"
        "<span class=\"crumb-title\" id=\"crumb-title\"></span></div>\n"
        "<button type=\"button\" class=\"search-open\" id=\"cmdk\"><span>搜索</span><kbd>Ctrl K</kbd></button>\n"
        "<button type=\"button\" class=\"text-button\" id=\"read-settings\" aria-expanded=\"false\" "
        "aria-haspopup=\"dialog\">阅读</button>\n"
        "<button type=\"button\" class=\"text-button\" id=\"theme-toggle\" aria-label=\"切换主题\">主题</button>\n"
        "<div class=\"progress\" aria-hidden=\"true\"><span id=\"progress\"></span></div>\n"
        "</header>\n"
        "<div class=\"frame\">\n"
        "<nav id=\"outline\" aria-label=\"大纲\">\n"
        "<div class=\"side-tabs\" role=\"tablist\">"
        "<button type=\"button\" role=\"tab\" id=\"tab-outline\" aria-selected=\"true\" "
        "aria-controls=\"panel-outline\">大纲</button>"
        "<button type=\"button\" role=\"tab\" id=\"tab-files\" aria-selected=\"false\" "
        "aria-controls=\"panel-files\">文件</button></div>\n"
        "<div class=\"side-panels\">"
        "<div id=\"panel-outline\" role=\"tabpanel\"></div>"
        "<div id=\"panel-files\" role=\"tabpanel\" hidden></div></div>\n"
        "<div class=\"side-resize\" id=\"side-resize\" role=\"separator\" aria-orientation=\"vertical\" "
        "aria-label=\"调整侧栏宽度\" tabindex=\"0\"></div>\n"
        "</nav>\n"
        "<main id=\"main\">\n<article id=\"doc\" tabindex=\"-1\">\n";
    page.html.append(fragment);
    page.html +=
        "\n</article>\n</main>\n"
        "<aside id=\"rail\" aria-label=\"旁注\" hidden>\n"
        "<div class=\"rail-head\"><span id=\"rail-title\">旁注</span>"
        "<button type=\"button\" id=\"rail-close\">关闭</button></div>\n"
        "<div id=\"rail-body\"></div>\n"
        "</aside>\n"
        "</div>\n<script src=\"";
    page.html += escape_html(asset);
    page.html += "/prism.min.js?v=" MDV_ASSET_VERSION "\"></script>\n<script src=\"";
    page.html += escape_html(asset);
    page.html += "/viewer.js?v=" MDV_ASSET_VERSION "\"></script>\n</body>\n</html>\n";
    return page;
}

}  // namespace mdv
