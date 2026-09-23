#include "preferences.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
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

std::map<std::string, std::string> read_preferences() {
    std::map<std::string, std::string> values;
    std::ifstream input(vantage::preferences_path());
    std::string line;
    while (std::getline(input, line)) {
        const auto split = line.find('=');
        if (split != std::string::npos) values[line.substr(0, split)] = line.substr(split + 1);
    }
    return values;
}

void write_preferences(const std::map<std::string, std::string> &values) {
    const auto path = vantage::preferences_path();
    std::filesystem::create_directories(path.parent_path());
    const auto temporary = path.string() + ".new";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw std::runtime_error("cannot write Vantage preferences");
        for (const auto &[key, value] : values) output << key << '=' << value << '\n';
        output.flush();
        if (!output) throw std::runtime_error("cannot write Vantage preferences");
    }
    std::filesystem::rename(temporary, path);
}

std::vector<std::string> split_domains(const std::string &value) {
    std::vector<std::string> domains;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find('|', start);
        auto domain = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (!domain.empty()) domains.push_back(std::move(domain));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    std::sort(domains.begin(), domains.end());
    domains.erase(std::unique(domains.begin(), domains.end()), domains.end());
    return domains;
}

std::string join_domains(const std::vector<std::string> &domains) {
    std::string result;
    for (const auto &domain : domains) {
        if (!result.empty()) result += '|';
        result += domain;
    }
    return result;
}

} // namespace

namespace vantage {

std::filesystem::path preferences_path() { return config_root() / "vantage-browser" / "preferences.conf"; }

bool compatibility_video_rendering() {
    const auto values = read_preferences();
    const auto found = values.find("compatibility_video_rendering_v3");
    // v1/v2 compatibility rendering disabled WebKit compositing. That fixed
    // media on some Wayland systems, but it also crashes complex pages such as
    // Google authentication and YouTube History. Ignore the old keys so an
    // existing profile cannot silently re-enable that crash path after an
    // upgrade. Only an explicit v3 opt-in enables the legacy workaround.
    return found != values.end() && found->second == "1";
}

void set_compatibility_video_rendering(bool enabled) {
    auto values = read_preferences();
    values["compatibility_video_rendering_v3"] = enabled ? "1" : "0";
    write_preferences(values);
}

void apply_video_rendering_environment(bool enabled) {
    if (enabled) {
        if (unsetenv("WEBKIT_DISABLE_DMABUF_RENDERER") != 0 ||
            unsetenv("WEBKIT_GST_DMABUF_SINK_DISABLED") != 0)
            throw std::runtime_error("cannot reset compatibility video rendering");
        if (setenv("WEBKIT_DISABLE_COMPOSITING_MODE", "1", 1) != 0)
            throw std::runtime_error("cannot enable compatibility video rendering");
    } else {
        // Keep WebKit's normal accelerated compositor and GTK DMA-BUF renderer.
        // Disabling the compositor is a known WebKitGTK crash path on complex
        // pages (Google auth/ChatGPT), while disabling the *renderer* has also
        // caused page/video crashes in WebKitGTK.  The media regression is in
        // the GStreamer DMA-BUF video-sink path, so isolate the workaround to
        // that path instead of changing page compositing.
        if (unsetenv("WEBKIT_DISABLE_COMPOSITING_MODE") != 0)
            throw std::runtime_error("cannot enable accelerated video rendering");
        if (unsetenv("WEBKIT_DISABLE_DMABUF_RENDERER") != 0)
            throw std::runtime_error("cannot enable the normal WebKit renderer");
        if (setenv("WEBKIT_GST_DMABUF_SINK_DISABLED", "1", 1) != 0)
            throw std::runtime_error("cannot enable safe media rendering");
    }
}

std::vector<std::string> dark_mode_enabled_domains() {
    const auto values = read_preferences();
    const auto found = values.find("dark_mode_enabled_domains");
    return found == values.end() ? std::vector<std::string>{} : split_domains(found->second);
}

bool dark_mode_enabled_for_domain(const std::string &domain) {
    const auto domains = dark_mode_enabled_domains();
    return std::find(domains.begin(), domains.end(), domain) != domains.end();
}

void set_dark_mode_enabled_for_domain(const std::string &domain, bool enabled) {
    if (domain.empty() || domain.find('|') != std::string::npos) return;
    auto values = read_preferences();
    auto domains = dark_mode_enabled_domains();
    const auto found = std::find(domains.begin(), domains.end(), domain);
    if (enabled && found == domains.end()) domains.push_back(domain);
    if (!enabled && found != domains.end()) domains.erase(found);
    std::sort(domains.begin(), domains.end());
    values["dark_mode_enabled_domains"] = join_domains(domains);
    // Remove the experimental global/exception model so upgrades start from
    // the explicit per-site allow-list only.
    values.erase("dark_mode");
    values.erase("dark_mode_disabled_domains");
    write_preferences(values);
}

} // namespace vantage
