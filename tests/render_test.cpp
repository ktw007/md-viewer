#include "document/compose.hpp"
#include "document/render.hpp"
#include "document/rewrite.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

int g_failed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++g_failed;
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
}

}  // namespace

int main() {
    const char* sample =
        "# 标题\n\n正文 **粗**\n\n<script>alert(1)</script>\n\n"
        "- [ ] 待办\n- [x] 完成\n\n"
        "| 甲 | 乙 |\n| --- | --- |\n| 1 | 2 |\n\n"
        "```cpp\nint x = 1;\n```\n\n"
        "# 标题\n";
    const mdv::Rendered rendered = mdv::render_markdown(sample);
    expect(rendered.title == "标题", "title is first h1, got: " + rendered.title);
    expect(rendered.html.find("<script>") == std::string::npos, "raw script tag leaked");
    expect(rendered.html.find("alert") != std::string::npos, "script text missing");
    expect(rendered.html.find("&lt;script&gt;") != std::string::npos, "script was not escaped");
    expect(rendered.html.find("<table>") != std::string::npos, "table missing");
    expect(rendered.html.find("task-list-item-checkbox") != std::string::npos, "task list missing");
    expect(rendered.html.find("language-cpp") != std::string::npos, "code language missing");
    expect(rendered.html.find("id=\"标题\"") != std::string::npos, "heading id missing");
    expect(rendered.html.find("id=\"标题-1\"") != std::string::npos, "duplicate heading id missing");

    const mdv::Rendered english = mdv::render_markdown("# Hello World\n");
    expect(english.html.find("id=\"hello-world\"") != std::string::npos, "ascii slug missing: " + english.html);

    const auto root = std::filesystem::temp_directory_path() / "mdv-rewrite";
    const auto doc = root / "guide" / "a.md";
    const mdv::Rewritten rewritten = mdv::rewrite_local_urls(
        "<p><img src=\"pic.png\" alt=\"a\"><img src=\"my pic.png\">"
        "<img src=\"图.png\"><a href=\"../b.md\">b</a>"
        "<a href=\"https://example.com/a\">e</a><a href=\"#sec\">s</a></p>",
        doc);
    expect(rewritten.root == root.lexically_normal(),
           "root should rise to the common parent: " + rewritten.root.string() + " vs " +
               root.lexically_normal().string());
    expect(rewritten.html.find("https://doc.local/guide/pic.png") != std::string::npos, "image url");
    expect(rewritten.html.find("https://doc.local/guide/my%20pic.png") != std::string::npos, "space encoding");
    expect(rewritten.html.find("https://doc.local/guide/%E5%9B%BE.png") != std::string::npos,
           "chinese encoding: " + rewritten.html);
    expect(rewritten.html.find("https://doc.local/b.md") != std::string::npos, "parent link");
    expect(rewritten.html.find("href=\"https://example.com/a\"") != std::string::npos, "external link changed");
    expect(rewritten.html.find("href=\"#sec\"") != std::string::npos, "fragment rewritten");

    mdv::ComposeOptions options;
    options.dark = true;
    options.scroll = 0.25;
    options.anchor = "标题";
    const mdv::Page page = mdv::compose_page(rendered.html, rendered.title, options);
    expect(page.html.find("data-theme=\"dark\"") != std::string::npos, "theme");
    expect(page.html.find("class=\"topbar\"") != std::string::npos, "topbar");
    expect(page.html.find("viewer.css?v=7") != std::string::npos, "css version");
    expect(page.html.find("Content-Security-Policy") != std::string::npos, "csp");
    expect(page.html.find("data-scroll=\"0.250000\"") != std::string::npos, "scroll");
    expect(page.html.find("data-anchor=\"标题\"") != std::string::npos, "anchor");
    expect(page.html.find("<script src=") != std::string::npos && page.html.find("viewer.js") != std::string::npos,
           "page script missing");
    expect(page.html.find("alert(1)") == std::string::npos || page.html.find("&lt;script&gt;") != std::string::npos,
           "composed page lost escaping");

    const std::filesystem::path fixtures = MDV_FIXTURE_DIR;
    const std::filesystem::path preview = MDV_PREVIEW_DIR;
    const std::filesystem::path page_dir = MDV_PAGE_DIR;
    std::error_code error;
    const auto asset = std::filesystem::relative(page_dir, preview, error).generic_string();
    const char* names[] = {"essay.md", "mixed.md", "readme.md", "code.md", "gallery.md", "design.md"};
    for (const char* name : names) {
        const auto markdown = read_file(fixtures / name);
        expect(!markdown.empty(), std::string("fixture empty: ") + name);
        const auto body = mdv::render_markdown(markdown);
        const auto linked = mdv::rewrite_local_urls(body.html, fixtures / name);
        mdv::ComposeOptions preview_options;
        preview_options.asset_base = asset;
        preview_options.content_security_policy = false;
        const auto html = mdv::compose_page(body.html, body.title, preview_options);
        auto out_name = std::string(name);
        out_name.replace(out_name.size() - 3, 3, ".html");
        write_file(preview / out_name, html.html);
        expect(html.html.find("<article") != std::string::npos, std::string("preview article: ") + name);
        (void)linked;
    }
    if (std::filesystem::exists(fixtures / "picture.png")) {
        std::filesystem::copy_file(fixtures / "picture.png", preview / "picture.png",
                                   std::filesystem::copy_options::overwrite_existing, error);
    }

    if (g_failed == 0) {
        std::cout << "ok\n";
    }
    return g_failed == 0 ? 0 : 1;
}
