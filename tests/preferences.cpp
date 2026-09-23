#include "preferences.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "vant-preferences-test";
    std::filesystem::remove_all(root);
    assert(setenv("VANT_CONFIG_HOME", root.c_str(), 1) == 0);

    assert(!vantage::compatibility_video_rendering());

    // Legacy compatibility preferences are intentionally ignored: disabling
    // compositing can crash modern WebKitGTK pages.
    std::filesystem::create_directories(root / "vantage-browser");
    {
        std::ofstream legacy(root / "vantage-browser" / "preferences.conf");
        legacy << "compatibility_video_rendering=1\n";
    }
    assert(!vantage::compatibility_video_rendering());
    {
        std::ofstream legacy_v2(root / "vantage-browser" / "preferences.conf");
        legacy_v2 << "compatibility_video_rendering_v2=1\n";
    }
    assert(!vantage::compatibility_video_rendering());

    vantage::set_compatibility_video_rendering(false);
    assert(!vantage::compatibility_video_rendering());
    vantage::apply_video_rendering_environment(false);
    assert(std::getenv("WEBKIT_DISABLE_COMPOSITING_MODE") == nullptr);
    assert(std::string(std::getenv("WEBKIT_DISABLE_DMABUF_RENDERER")) == "1");

    vantage::set_compatibility_video_rendering(true);
    assert(vantage::compatibility_video_rendering());
    vantage::apply_video_rendering_environment(true);
    assert(std::string(std::getenv("WEBKIT_DISABLE_COMPOSITING_MODE")) == "1");

    // Dark Mode is off for every site by default and becomes enabled only
    // for domains the user explicitly toggles.
    assert(vantage::dark_mode_enabled_domains().empty());
    assert(!vantage::dark_mode_enabled_for_domain("example.com"));
    vantage::set_dark_mode_enabled_for_domain("example.com", true);
    vantage::set_dark_mode_enabled_for_domain("www.example.org", true);
    assert(vantage::dark_mode_enabled_for_domain("example.com"));
    assert(vantage::dark_mode_enabled_for_domain("www.example.org"));
    assert(vantage::dark_mode_enabled_domains().size() == 2);
    assert(vantage::compatibility_video_rendering()); // setters preserve unrelated preferences
    vantage::set_dark_mode_enabled_for_domain("example.com", false);
    assert(!vantage::dark_mode_enabled_for_domain("example.com"));
    assert(vantage::dark_mode_enabled_for_domain("www.example.org"));

    std::filesystem::remove_all(root);
}
