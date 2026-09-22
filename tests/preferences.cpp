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

    // Legacy profiles used compatibility mode by default.  They must migrate
    // to accelerated compositing rather than retaining the crash-prone mode.
    std::filesystem::create_directories(root / "vantage-browser");
    {
        std::ofstream legacy(root / "vantage-browser" / "preferences.conf");
        legacy << "compatibility_video_rendering=1\n";
    }
    assert(!vantage::compatibility_video_rendering());

    vantage::set_compatibility_video_rendering(false);
    assert(!vantage::compatibility_video_rendering());
    vantage::apply_video_rendering_environment(false);
    assert(std::getenv("WEBKIT_DISABLE_COMPOSITING_MODE") == nullptr);

    vantage::set_compatibility_video_rendering(true);
    assert(vantage::compatibility_video_rendering());
    vantage::apply_video_rendering_environment(true);
    assert(std::string(std::getenv("WEBKIT_DISABLE_COMPOSITING_MODE")) == "1");

    // Dark Mode is opt-in globally. Per-domain exceptions are only persisted
    // when users explicitly toggle a domain while it is enabled.
    assert(!vantage::dark_mode_enabled());
    assert(vantage::dark_mode_disabled_domains().empty());
    vantage::set_dark_mode_enabled(true);
    assert(vantage::dark_mode_enabled());
    assert(vantage::compatibility_video_rendering()); // setters preserve unrelated preferences
    vantage::set_dark_mode_disabled_for_domain("example.com", true);
    vantage::set_dark_mode_disabled_for_domain("www.example.org", true);
    assert(vantage::dark_mode_disabled_for_domain("example.com"));
    assert(vantage::dark_mode_disabled_for_domain("www.example.org"));
    assert(vantage::dark_mode_disabled_domains().size() == 2);
    vantage::set_dark_mode_disabled_for_domain("example.com", false);
    assert(!vantage::dark_mode_disabled_for_domain("example.com"));
    assert(vantage::dark_mode_disabled_for_domain("www.example.org"));

    std::filesystem::remove_all(root);
}
