#include "navigation.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {
std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string encode_query(std::string_view text) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out << c;
        else if (c == ' ') out << '+';
        else out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
    }
    return out.str();
}
}

namespace vantage {

NavigationPolicy::NavigationPolicy(std::string search_template)
    : search_template_(std::move(search_template)) {
    if (search_template_.find("{query}") == std::string::npos)
        throw std::invalid_argument("search template must contain {query}");
}

NavigationDecision NavigationPolicy::resolve(std::string_view raw) const {
    const auto input = trim(raw);
    if (input.empty()) return {NavigationKind::rejected, {}, "empty input"};
    if (input.find('\0') != std::string::npos || input.size() > 8192)
        return {NavigationKind::rejected, {}, "invalid input"};

    const auto colon = input.find(':');
    const bool explicit_host_port = colon != std::string::npos &&
        (input.substr(0, colon) == "localhost" || input.substr(0, colon).find('.') != std::string::npos);
    if (input.find(' ') == std::string::npos && explicit_host_port)
        return {NavigationKind::web, "https://" + input, {}};

    if (colon != std::string::npos) {
        const auto scheme = lower(std::string_view(input).substr(0, colon));
        if (scheme == "http" || scheme == "https")
            return {NavigationKind::web, input, {}};
        if (scheme == "about" || scheme == "vantage")
            return {NavigationKind::internal, input, {}};
        if (scheme == "mailto" || scheme == "tel")
            return {NavigationKind::external, input, "requires user confirmation"};
        return {NavigationKind::rejected, {}, "scheme is not allowed"};
    }

    const bool looks_like_host = input.find(' ') == std::string::npos &&
        (input.find('.') != std::string::npos || input == "localhost" || input.starts_with("localhost:"));
    if (looks_like_host) return {NavigationKind::web, "https://" + input, {}};

    auto uri = search_template_;
    uri.replace(uri.find("{query}"), 7, encode_query(input));
    return {NavigationKind::web, std::move(uri), {}};
}

} // namespace vantage
