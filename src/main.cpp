#include "application.h"
#include "agent_rpc.h"
#include "launch_options.h"
#include "native_app.h"
#include "preferences.h"
#include "version.h"

#include <exception>
#include <glib.h>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
void usage(std::ostream &out) {
    out << "usage: vant [URL]\n"
        << "       vant [--fullscreen] [--compatibility-video-rendering|--accelerated-video-rendering] [URL]\n"
        << "       vant --private [URL]\n"
        << "       vant --app URL [--fullscreen]\n"
        << "       vant --app=URL [--fullscreen]\n"
        << "       vant settings video-rendering [compatibility|accelerated]\n"
        << "       vant agent <status|version|capabilities|call> ...\n"
        << "       vant [--headless-smoke|--native-probe|--native-smoke|--tab-sizing-probe|--version|--help]\n";
}
}

int main(int argc, char **argv) {
    // WebKitGTK derives fallback website-data/cache locations from GLib's
    // program name.  The executable is intentionally called `vant`, but the
    // browser profile namespace is `vantage-browser`; set that identity before
    // any WebKit/GTK object can initialise its process-wide default paths.
    // Our normal NetworkSession still uses explicit .../vantage-browser/webkit
    // directories, while any WebKit subsystem that requires a fallback now
    // stays inside the same Vantage namespace instead of recreating
    // ~/.local/share/vant (and ~/.cache/vant).
    g_set_prgname("vantage-browser");

    if (argc >= 2 && std::string_view(argv[1]) == "agent") {
        std::vector<std::string_view> args; for (int i=2;i<argc;++i) args.emplace_back(argv[i]);
        return vantage::run_agent_cli(args);
    }
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
        return vantage::run_native({.smoke = true});
    if (argc == 2 && std::string_view(argv[1]) == "--tab-sizing-probe")
        return vantage::run_native({.tab_probe = true});
    if (argc == 2 && std::string_view(argv[1]) == "--native-probe") {
        std::cout << vantage::native_versions() << '\n';
        return 0;
    }
    std::vector<std::string_view> arguments;
    for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    const auto launch = vantage::parse_launch_options(arguments);
    if (!launch.valid) {
        if (!launch.error.empty()) std::cerr << "vant: " << launch.error << '\n';
        usage(std::cerr);
        return 2;
    }
    try {
        vantage::apply_video_rendering_environment(
            launch.rendering_override.value_or(vantage::compatibility_video_rendering()));
    } catch (const std::exception &error) {
        std::cerr << "vant: " << error.what() << '\n';
        return 1;
    }
    return vantage::run_native(launch.options);
}
