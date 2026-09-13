#include "launch_options.h"

#include <cassert>
#include <string_view>
#include <vector>

int main() {
    using namespace std::literals;
    {
        const auto parsed = vantage::parse_launch_options({});
        assert(parsed.valid && parsed.options.initial_uri == "vantage:new");
        assert(!parsed.options.fullscreen && !parsed.options.app_mode && !parsed.options.private_mode);
    }
    {
        const auto parsed = vantage::parse_launch_options({"--fullscreen"sv, "https://nift.dev"sv});
        assert(parsed.valid && parsed.options.fullscreen);
        assert(parsed.options.initial_uri == "https://nift.dev");
    }
    assert(vantage::parse_launch_options({"nift.dev"sv}).valid);
    assert(vantage::parse_launch_options({"localhost:8080"sv}).valid);
    assert(vantage::parse_launch_options({"file:///tmp/page.html"sv}).valid);
    assert(vantage::parse_launch_options({"./page.html"sv}).valid);
    {
        const auto parsed = vantage::parse_launch_options({"--app"sv, "https://nift.dev"sv});
        assert(parsed.valid && parsed.options.app_mode);
        assert(parsed.options.initial_uri == "https://nift.dev");
    }
    {
        const auto parsed = vantage::parse_launch_options({"--app=https://nift.dev"sv, "--fullscreen"sv});
        assert(parsed.valid && parsed.options.app_mode && parsed.options.fullscreen);
    }
    {
        const auto parsed = vantage::parse_launch_options({"--private"sv});
        assert(parsed.valid && parsed.options.private_mode);
    }
    assert(!vantage::parse_launch_options({"--app"sv}).valid);
    assert(!vantage::parse_launch_options({"--app="sv}).valid);
    assert(!vantage::parse_launch_options({"--app"sv, "--fullscreen"sv}).valid);
    assert(!vantage::parse_launch_options({"one"sv, "two"sv}).valid);
    assert(!vantage::parse_launch_options({"--private"sv, "--app=https://nift.dev"sv}).valid);
    {
        const auto parsed = vantage::parse_launch_options({"unknown-command"sv});
        assert(!parsed.valid && parsed.error == "unknown command or address: unknown-command");
    }
    {
        const auto parsed = vantage::parse_launch_options({"--unknown"sv});
        assert(!parsed.valid && parsed.error == "unknown option: --unknown");
    }
    assert(!vantage::parse_launch_options({"--app"sv, "unknown-command"sv}).valid);
}
