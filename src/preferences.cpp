#include "preferences.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

namespace {

std::filesystem::path config_root() {
    if (const char *override_root = std::getenv("VANT_CONFIG_HOME"); override_root && *override_root)
        return override_root;
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) return xdg;
    if (const char *home = std::getenv("HOME"); home && *home)
        return std::filesystem::path(home) / ".config";
    throw std::runtime_error("cannot determine Vantage configuration directory");
}

} // namespace

namespace vantage {

std::filesystem::path preferences_path() {
    return config_root() / "vantage-browser" / "preferences.conf";
}

bool compatibility_video_rendering() {
    std::ifstream input(preferences_path());
    std::string line;
    while (std::getline(input, line)) {
        if (line == "compatibility_video_rendering=0") return false;
        if (line == "compatibility_video_rendering=1") return true;
    }
    return true;
}

void set_compatibility_video_rendering(bool enabled) {
    const auto path = preferences_path();
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".new";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write Vantage preferences");
        output << "compatibility_video_rendering=" << (enabled ? '1' : '0') << '\n';
        output.flush();
        if (!output) throw std::runtime_error("cannot write Vantage preferences");
    }
    std::filesystem::rename(temporary, path);
}

void apply_video_rendering_environment(bool enabled) {
    if (enabled) {
        if (setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1) != 0)
            throw std::runtime_error("cannot enable compatibility video rendering");
    } else {
        if (unsetenv("WEBKIT_DISABLE_COMPOSITING_MODE") != 0)
            throw std::runtime_error("cannot enable accelerated video rendering");
    }
}

} // namespace vantage
