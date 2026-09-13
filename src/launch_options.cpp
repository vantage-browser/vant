#include "launch_options.h"

#include <cctype>
#include <utility>

namespace {
bool is_launch_target(const std::string_view argument) {
    if (argument.empty()) return false;
    if (argument.front() == '/' || argument.starts_with("./") || argument.starts_with("../")) return true;
    if (argument == "localhost" || argument.starts_with("localhost:")) return true;
    if (argument.find('.') != std::string_view::npos && argument.find(' ') == std::string_view::npos)
        return true;

    const auto colon = argument.find(':');
    if (colon == std::string_view::npos || colon == 0) return false;
    if (!std::isalpha(static_cast<unsigned char>(argument.front()))) return false;
    for (std::size_t index = 1; index < colon; ++index) {
        const auto character = static_cast<unsigned char>(argument[index]);
        if (!std::isalnum(character) && character != '+' && character != '-' && character != '.') return false;
    }
    return true;
}

void invalidate(vantage::LaunchOptionsParse &result, std::string message) {
    result.valid = false;
    result.error = std::move(message);
}
} // namespace

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
                invalidate(result, "--app requires a URL");
                return result;
            }
            if (!is_launch_target(arguments[index])) {
                invalidate(result, "unknown command or address: " + std::string(arguments[index]));
                return result;
            }
            result.options.app_mode = true;
            result.options.initial_uri = arguments[index];
            have_uri = true;
        } else if (argument.starts_with("--app=")) {
            const auto uri = argument.substr(6);
            if (have_uri || uri.empty()) {
                invalidate(result, "--app requires a URL");
                return result;
            }
            if (!is_launch_target(uri)) {
                invalidate(result, "unknown command or address: " + std::string(uri));
                return result;
            }
            result.options.app_mode = true;
            result.options.initial_uri = uri;
            have_uri = true;
        } else if (!argument.starts_with('-') && !have_uri && is_launch_target(argument)) {
            result.options.initial_uri = argument;
            have_uri = true;
        } else {
            if (argument.starts_with('-'))
                invalidate(result, "unknown option: " + std::string(argument));
            else if (have_uri)
                invalidate(result, "unexpected argument: " + std::string(argument));
            else
                invalidate(result, "unknown command or address: " + std::string(argument));
            return result;
        }
    }
    if (result.options.app_mode && result.options.private_mode)
        invalidate(result, "--app and --private cannot be used together");
    return result;
}

} // namespace vantage
