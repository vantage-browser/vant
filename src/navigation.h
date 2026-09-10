#ifndef VANTAGE_NAVIGATION_H
#define VANTAGE_NAVIGATION_H

#include <string>
#include <string_view>

namespace vantage {

enum class NavigationKind { web, internal, external, rejected };

struct NavigationDecision {
    NavigationKind kind{NavigationKind::rejected};
    std::string uri;
    std::string reason;
};

class NavigationPolicy {
public:
    explicit NavigationPolicy(std::string search_template = "https://duckduckgo.com/?q={query}");
    NavigationDecision resolve(std::string_view input) const;
    bool allow_tls_error_bypass() const noexcept { return false; }
    bool open_popup_as_tab() const noexcept { return true; }

private:
    std::string search_template_;
};

} // namespace vantage
#endif
