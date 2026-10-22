#include "platform/encoding.hpp"

#include "platform/text.hpp"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace mdv {
namespace {

constexpr std::uintmax_t kMaxBytes = 32ull * 1024ull * 1024ull;

bool convert_wide(std::wstring_view wide, std::string& utf8, std::wstring& error) {
    if (wide.empty()) {
        utf8.clear();
        return true;
    }
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (bytes <= 0) {
        error = L"无法把文本转成 UTF-8。";
        return false;
    }
    utf8.resize(static_cast<size_t>(bytes));
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), bytes, nullptr,
                        nullptr);
    return true;
}

bool convert_codepage(const char* data, int size, UINT codepage, std::string& utf8) {
    if (size == 0) {
        utf8.clear();
        return true;
    }
    const int chars = MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, data, size, nullptr, 0);
    if (chars <= 0) {
        return false;
    }
    std::wstring wide(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(codepage, MB_ERR_INVALID_CHARS, data, size, wide.data(), chars);
    std::wstring error;
    return convert_wide(wide, utf8, error);
}

}  // namespace

std::wstring utf8_to_wide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (chars <= 0) {
        return {};
    }
    std::wstring wide(static_cast<size_t>(chars), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), chars);
    return wide;
}

std::string wide_to_utf8(std::wstring_view wide) {
    std::string utf8;
    std::wstring error;
    convert_wide(wide, utf8, error);
    return utf8;
}

FileText read_markdown_file(const std::filesystem::path& path) {
    FileText result;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        result.error = L"找不到这份文件。";
        return result;
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        result.error = L"无法读取这份文件。";
        return result;
    }
    if (size > kMaxBytes) {
        result.error = L"这份文件超过 32 MB，阅读器不予打开。";
        return result;
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = L"无法读取这份文件。";
        return result;
    }
    std::vector<char> bytes(static_cast<size_t>(size));
    if (size > 0) {
        input.read(bytes.data(), static_cast<std::streamsize>(size));
        if (!input) {
            result.error = L"读文件中途失败。";
            return result;
        }
    }
    if (std::find(bytes.begin(), bytes.end(), '\0') != bytes.end()) {
        result.error = L"这不是一份文本。";
        return result;
    }

    const char* data = bytes.data();
    int length = static_cast<int>(bytes.size());
    if (length >= 3 && static_cast<unsigned char>(data[0]) == 0xEF &&
        static_cast<unsigned char>(data[1]) == 0xBB && static_cast<unsigned char>(data[2]) == 0xBF) {
        data += 3;
        length -= 3;
    } else if (length >= 2 && static_cast<unsigned char>(data[0]) == 0xFF &&
               static_cast<unsigned char>(data[1]) == 0xFE) {
        const auto* wide = reinterpret_cast<const wchar_t*>(data + 2);
        const int chars = (length - 2) / static_cast<int>(sizeof(wchar_t));
        std::wstring text(wide, wide + chars);
        if (!convert_wide(text, result.utf8, result.error)) {
            return result;
        }
        result.ok = true;
        return result;
    } else if (length >= 2 && static_cast<unsigned char>(data[0]) == 0xFE &&
               static_cast<unsigned char>(data[1]) == 0xFF) {
        std::wstring text;
        text.resize(static_cast<size_t>((length - 2) / 2));
        for (size_t i = 0; i < text.size(); ++i) {
            const auto hi = static_cast<unsigned char>(data[2 + i * 2]);
            const auto lo = static_cast<unsigned char>(data[3 + i * 2]);
            text[i] = static_cast<wchar_t>((hi << 8) | lo);
        }
        if (!convert_wide(text, result.utf8, result.error)) {
            return result;
        }
        result.ok = true;
        return result;
    }

    if (convert_codepage(data, length, CP_UTF8, result.utf8) ||
        convert_codepage(data, length, 54936, result.utf8) ||
        convert_codepage(data, length, CP_ACP, result.utf8)) {
        result.ok = true;
        return result;
    }
    result.error = L"无法识别这份文件的编码。请另存为 UTF-8。";
    return result;
}

}  // namespace mdv
