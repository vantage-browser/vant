#include "launch_options.h"

namespace vantage {

LaunchOptionsParse parse_launch_options(const std::vector<std::string_view> &arguments) {
    LaunchOptionsParse result;
    bool have_uri = false;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const auto argument = arguments[index];
        if (argument == "--compatibility-video-rendering" || argument == "--fix-broken-video") {
            result.rendering_override = true;
        } else if (argument == "--accelerated-video-rendering") {
            result.rendering_override = false;
        } else if (argument == "--fullscreen") {
            result.options.fullscreen = true;
        } else if (argument == "--private") {
            result.options.private_mode = true;
        } else if (argument == "--app") {
            if (have_uri || ++index >= arguments.size() || arguments[index].empty() ||
                arguments[index].starts_with('-')) {
                result.valid = false;
                return result;
            }
            result.options.app_mode = true;
            result.options.initial_uri = arguments[index];
            have_uri = true;
        } else if (argument.starts_with("--app=")) {
            const auto uri = argument.substr(6);
            if (have_uri || uri.empty()) {
                result.valid = false;
                return result;
            }
            result.options.app_mode = true;
            result.options.initial_uri = uri;
            have_uri = true;
        } else if (!argument.starts_with('-') && !have_uri) {
            result.options.initial_uri = argument;
            have_uri = true;
        } else {
            result.valid = false;
            return result;
        }
    }
    if (result.options.app_mode && result.options.private_mode) result.valid = false;
    return result;
}

} // namespace vantage
