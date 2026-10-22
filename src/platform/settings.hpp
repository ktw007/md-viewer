#pragma once

#include <filesystem>
#include <map>
#include <string>

namespace mdv {

struct WindowPlacement {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool maximized = false;
    bool valid = false;
};

class Settings {
public:
    double zoom = 1.0;
    WindowPlacement window;

    void load();
    void save() const;

    double scroll_for(const std::filesystem::path& path) const;
    void set_scroll(const std::filesystem::path& path, double y);

private:
    std::map<std::string, double> scroll_;
    std::filesystem::path file_;
};

}  // namespace mdv
