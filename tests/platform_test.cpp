#include "platform/encoding.hpp"
#include "platform/paths.hpp"
#include "platform/text.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failed = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++g_failed;
    }
}

void write_bytes(const std::filesystem::path& path, const std::vector<char>& data) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!data.empty()) {
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    }
}

}  // namespace

int main() {
    // 1. Text conversion tests
    {
        const std::string text = "你好，世界！Hello, 123.";
        const std::wstring wide = mdv::utf8_to_wide(text);
        expect(!wide.empty(), "utf8_to_wide non-empty");
        const std::string back = mdv::wide_to_utf8(wide);
        expect(back == text, "wide_to_utf8 round-trip");
        expect(mdv::utf8_to_wide("").empty(), "empty utf8_to_wide");
        expect(mdv::wide_to_utf8(L"").empty(), "empty wide_to_utf8");
    }

    const auto temp_dir = std::filesystem::temp_directory_path() / "mdv_platform_test";
    std::filesystem::create_directories(temp_dir);

    // 2. UTF-8 plain and UTF-8 BOM
    {
        const auto p1 = temp_dir / "utf8_plain.md";
        write_bytes(p1, {'#', ' ', 'H', 'i', '\n'});
        const auto res1 = mdv::read_markdown_file(p1);
        expect(res1.ok, "read utf8 plain");
        expect(res1.utf8 == "# Hi\n", "utf8 content mismatch");

        const auto p2 = temp_dir / "utf8_bom.md";
        const std::vector<char> bom_data = {
            static_cast<char>(0xEF), static_cast<char>(0xBB), static_cast<char>(0xBF),
            '#', ' ', 'B', 'O', 'M'
        };
        write_bytes(p2, bom_data);
        const auto res2 = mdv::read_markdown_file(p2);
        expect(res2.ok, "read utf8 with bom");
        expect(res2.utf8 == "# BOM", "bom should be stripped");
    }

    // 3. UTF-16 LE with BOM (0xFF, 0xFE)
    {
        const auto p_le = temp_dir / "utf16_le.md";
        // BOM + 'A' (0x0041)
        const std::vector<char> le_data = {
            static_cast<char>(0xFF), static_cast<char>(0xFE),
            0x41, 0x00
        };
        write_bytes(p_le, le_data);
        const auto res_le = mdv::read_markdown_file(p_le);
        expect(res_le.ok, "read utf16 le with bom");
        expect(res_le.utf8 == "A", "utf16 le content");
    }

    // 4. UTF-16 BE with BOM (0xFE, 0xFF)
    {
        const auto p_be = temp_dir / "utf16_be.md";
        // BOM + 'B' (0x0042)
        const std::vector<char> be_data = {
            static_cast<char>(0xFE), static_cast<char>(0xFF),
            0x00, 0x42
        };
        write_bytes(p_be, be_data);
        const auto res_be = mdv::read_markdown_file(p_be);
        expect(res_be.ok, "read utf16 be with bom");
        expect(res_be.utf8 == "B", "utf16 be content");

        // UTF-16 with embedded NUL wchar
        const auto p_le_nul = temp_dir / "utf16_le_nul.md";
        const std::vector<char> le_nul_data = {
            static_cast<char>(0xFF), static_cast<char>(0xFE),
            0x41, 0x00, 0x00, 0x00, 0x42, 0x00
        };
        write_bytes(p_le_nul, le_nul_data);
        const auto res_le_nul = mdv::read_markdown_file(p_le_nul);
        expect(!res_le_nul.ok, "utf16 with embedded nul must fail");
    }

    // 5. Binary file containing NUL byte
    {
        const auto p_bin = temp_dir / "binary.md";
        const std::vector<char> bin_data = {'a', '\0', 'b'};
        write_bytes(p_bin, bin_data);
        const auto res_bin = mdv::read_markdown_file(p_bin);
        expect(!res_bin.ok, "binary with nul must fail");
        expect(res_bin.error.find(L"文本") != std::wstring::npos, "error message for binary");
    }

    // 6. Non-existent file
    {
        const auto p_missing = temp_dir / "does_not_exist_12345.md";
        const auto res_missing = mdv::read_markdown_file(p_missing);
        expect(!res_missing.ok, "non-existent file must fail");
    }

    // 7. Path key normalization
    {
        const auto key1 = mdv::path_key("C:\\Users\\Test\\Doc.md");
        const auto key2 = mdv::path_key("c:/users/test/doc.md");
        expect(key1 == key2, "path_key case-folding and slash unification: " + key1 + " vs " + key2);
    }

    // Cleanup
    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);

    if (g_failed == 0) {
        std::cout << "platform tests ok\n";
    }
    return g_failed == 0 ? 0 : 1;
}
