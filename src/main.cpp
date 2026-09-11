#include "application.h"
#include "native_app.h"
#include "preferences.h"
#include "version.h"

#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {
void usage(std::ostream &out) {
    out << "usage: vant [URL]\n"
        << "       vant [--compatibility-video-rendering|--accelerated-video-rendering] [URL]\n"
        << "       vant settings video-rendering [compatibility|accelerated]\n"
        << "       vant [--headless-smoke|--native-probe|--native-smoke|--version|--help]\n";
}
}

int main(int argc, char **argv) {
    if (argc >= 2 && std::string_view(argv[1]) == "settings") {
        if (argc != 4 || std::string_view(argv[2]) != "video-rendering" ||
            (std::string_view(argv[3]) != "compatibility" && std::string_view(argv[3]) != "accelerated")) {
            usage(std::cerr);
            return 2;
        }
        try {
            const bool enabled = std::string_view(argv[3]) == "compatibility";
            vantage::set_compatibility_video_rendering(enabled);
            std::cout << "Video rendering set to " << (enabled ? "compatibility" : "accelerated")
                      << ". The setting applies the next time Vantage opens.\n";
            return 0;
        } catch (const std::exception &error) {
            std::cerr << "vant: " << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "Vantage Browser " << vantage::version << '\n';
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--headless-smoke") {
        vantage::Application app;
        app.start();
        app.stop();
        return vantage::Application::live_instances() == 1 ? 0 : 1;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--native-smoke")
        return vantage::run_native(true, {});
    if (argc == 2 && std::string_view(argv[1]) == "--native-probe") {
        std::cout << vantage::native_versions() << '\n';
        return 0;
    }
    std::optional<bool> rendering_override;
    std::string initial_uri = "vantage:new";
    bool have_uri = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--compatibility-video-rendering" || argument == "--fix-broken-video")
            rendering_override = true;
        else if (argument == "--accelerated-video-rendering") rendering_override = false;
        else if (!argument.starts_with('-') && !have_uri) {
            initial_uri = argument;
            have_uri = true;
        } else {
            usage(std::cerr);
            return 2;
        }
    }
    try {
        vantage::apply_video_rendering_environment(
            rendering_override.value_or(vantage::compatibility_video_rendering()));
    } catch (const std::exception &error) {
        std::cerr << "vant: " << error.what() << '\n';
        return 1;
    }
    return vantage::run_native(false, initial_uri);
}
