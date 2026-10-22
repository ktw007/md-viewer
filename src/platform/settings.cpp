#include "platform/settings.hpp"

#include "platform/paths.hpp"
#include "platform/text.hpp"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string_view>

namespace mdv {
namespace {

constexpr double kMinZoom = 0.5;
constexpr double kMaxZoom = 3.0;

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buffer[8];
                    snprintf(buffer, sizeof buffer, "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

struct Stored {
    double zoom = 1.0;
    WindowPlacement window;
    std::map<std::string, double> scroll;
};

class Json {
public:
    explicit Json(std::string text) : text_(std::move(text)) {}

    bool parse(Stored& settings) {
        skip();
        if (!consume('{')) {
            return false;
        }
        skip();
        if (peek() == '}') {
            return true;
        }
        while (true) {
            const auto key = parse_string();
            if (!key) {
                return false;
            }
            skip();
            if (!consume(':')) {
                return false;
            }
            skip();
            if (*key == "zoom") {
                const auto value = parse_number();
                if (!value) {
                    return false;
                }
                settings.zoom = *value;
            } else if (*key == "maximized") {
                const auto value = parse_bool();
                if (!value) {
                    return false;
                }
                settings.window.maximized = *value;
            } else if (*key == "x" || *key == "y" || *key == "width" || *key == "height") {
                const auto value = parse_number();
                if (!value) {
                    return false;
                }
                const int n = static_cast<int>(*value);
                if (*key == "x") {
                    settings.window.x = n;
                } else if (*key == "y") {
                    settings.window.y = n;
                } else if (*key == "width") {
                    settings.window.width = n;
                } else {
                    settings.window.height = n;
                }
            } else if (*key == "valid") {
                const auto value = parse_bool();
                if (!value) {
                    return false;
                }
                settings.window.valid = *value;
            } else if (*key == "scroll") {
                if (!parse_scroll(settings)) {
                    return false;
                }
            } else if (!skip_value()) {
                return false;
            }
            skip();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip();
        }
    }

private:
    char peek() const { return index_ < text_.size() ? text_[index_] : '\0'; }

    void skip() {
        while (index_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[index_])) != 0) {
            ++index_;
        }
    }

    bool consume(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++index_;
        return true;
    }

    std::optional<std::string> parse_string() {
        if (!consume('"')) {
            return std::nullopt;
        }
        std::string out;
        while (index_ < text_.size()) {
            const char c = text_[index_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (index_ >= text_.size()) {
                return std::nullopt;
            }
            const char esc = text_[index_++];
            switch (esc) {
                case '\\':
                case '"':
                case '/':
                    out.push_back(esc);
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'u':
                    if (index_ + 4 > text_.size()) {
                        return std::nullopt;
                    }
                    index_ += 4;
                    out.push_back('?');
                    break;
                default:
                    return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<double> parse_number() {
        const size_t begin = index_;
        if (peek() == '-') {
            ++index_;
        }
        if (!std::isdigit(static_cast<unsigned char>(peek()))) {
            return std::nullopt;
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            ++index_;
        }
        if (peek() == '.') {
            ++index_;
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                ++index_;
            }
        }
        try {
            return std::stod(text_.substr(begin, index_ - begin));
        } catch (...) {
            return std::nullopt;
        }
    }

    std::optional<bool> parse_bool() {
        if (text_.compare(index_, 4, "true") == 0) {
            index_ += 4;
            return true;
        }
        if (text_.compare(index_, 5, "false") == 0) {
            index_ += 5;
            return false;
        }
        return std::nullopt;
    }

    bool skip_value() {
        if (peek() == '"') {
            return parse_string().has_value();
        }
        if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            return parse_number().has_value();
        }
        if (peek() == 't' || peek() == 'f') {
            return parse_bool().has_value();
        }
        if (peek() == '{') {
            ++index_;
            skip();
            if (consume('}')) {
                return true;
            }
            while (true) {
                if (!parse_string() || (skip(), !consume(':')) || (skip(), !skip_value())) {
                    return false;
                }
                skip();
                if (consume('}')) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
                skip();
            }
        }
        return false;
    }

    bool parse_scroll(Stored& settings) {
        if (!consume('{')) {
            return false;
        }
        skip();
        if (consume('}')) {
            return true;
        }
        while (true) {
            const auto key = parse_string();
            skip();
            if (!key || !consume(':')) {
                return false;
            }
            skip();
            const auto value = parse_number();
            if (!value) {
                return false;
            }
            if (*value >= 0.0 && *value <= 1.0) {
                settings.scroll[*key] = *value;
            }
            skip();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return false;
            }
            skip();
        }
    }

    std::string text_;
    size_t index_ = 0;
};

void clamp_zoom(double& zoom) {
    if (!std::isfinite(zoom)) {
        zoom = 1.0;
    }
    if (zoom < kMinZoom) {
        zoom = kMinZoom;
    }
    if (zoom > kMaxZoom) {
        zoom = kMaxZoom;
    }
}

}  // namespace

void Settings::load() {
    file_ = roaming_directory() / "session.json";
    std::ifstream input(file_, std::ios::binary);
    if (!input) {
        return;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    Stored parsed;
    Json json(buffer.str());
    if (!json.parse(parsed)) {
        return;
    }
    clamp_zoom(parsed.zoom);
    if (parsed.window.width < 480 || parsed.window.height < 320) {
        parsed.window.valid = false;
    }
    zoom = parsed.zoom;
    window = parsed.window;
    scroll_ = std::move(parsed.scroll);
}

void Settings::save() const {
    if (file_.empty()) {
        return;
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.setf(std::ios::fixed);
    out.precision(6);
    out << "{\n  \"zoom\": " << zoom << ",\n  \"maximized\": " << (window.maximized ? "true" : "false")
        << ",\n  \"x\": " << window.x << ",\n  \"y\": " << window.y << ",\n  \"width\": " << window.width
        << ",\n  \"height\": " << window.height << ",\n  \"valid\": " << (window.valid ? "true" : "false")
        << ",\n  \"scroll\": {\n";
    bool first = true;
    for (const auto& [key, value] : scroll_) {
        if (!first) {
            out << ",\n";
        }
        first = false;
        out << "    \"" << json_escape(key) << "\": " << value;
    }
    out << "\n  }\n}\n";

    const auto temporary = std::filesystem::path(file_.wstring() + L".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return;
        }
        const auto body = out.str();
        output.write(body.data(), static_cast<std::streamsize>(body.size()));
        if (!output) {
            return;
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, file_, error);
    if (error) {
        std::filesystem::remove(file_, error);
        std::filesystem::rename(temporary, file_, error);
    }
}

double Settings::scroll_for(const std::filesystem::path& path) const {
    const auto found = scroll_.find(path_key(path));
    if (found == scroll_.end()) {
        return 0;
    }
    return found->second;
}

void Settings::set_scroll(const std::filesystem::path& path, double y) {
    if (!(y >= 0.0)) {
        y = 0;
    }
    if (y > 1.0) {
        y = 1.0;
    }
    scroll_[path_key(path)] = y;
}

}  // namespace mdv
