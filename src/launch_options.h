#ifndef VANTAGE_LAUNCH_OPTIONS_H
#define VANTAGE_LAUNCH_OPTIONS_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vantage {

struct NativeLaunchOptions {
    bool smoke{};
    bool fullscreen{};
    bool app_mode{};
    bool private_mode{};
    std::string initial_uri{"vantage:new"};
};

struct LaunchOptionsParse {
    NativeLaunchOptions options;
    std::optional<bool> rendering_override;
    bool valid{true};
};

LaunchOptionsParse parse_launch_options(const std::vector<std::string_view> &arguments);

} // namespace vantage

#endif
