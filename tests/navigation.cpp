#include "navigation.h"

#include <cassert>
#include <stdexcept>

using vantage::NavigationKind;

int main() {
    const vantage::NavigationPolicy policy;
    assert(policy.resolve(" example.com ").uri == "https://example.com");
    assert(policy.resolve("localhost:8080").uri == "https://localhost:8080");
    assert(policy.resolve("https://example.com/a").kind == NavigationKind::web);
    assert(policy.resolve("about:blank").kind == NavigationKind::internal);
    assert(policy.resolve("vantage:new").kind == NavigationKind::internal);
    assert(policy.resolve("mailto:test@example.com").kind == NavigationKind::external);
    assert(policy.resolve("javascript:alert(1)").kind == NavigationKind::rejected);
    assert(policy.resolve("file:///tmp/vantage-local-page.html").kind == NavigationKind::web);
    assert(policy.resolve("file:///tmp/vantage-local-page.html").uri == "file:///tmp/vantage-local-page.html");
    assert(policy.resolve("data:text/html,test").kind == NavigationKind::rejected);
    assert(policy.resolve("two words").uri == "https://search.brave.com/search?q=two+words");
    assert(policy.resolve("c++ browser").uri == "https://search.brave.com/search?q=c%2B%2B+browser");
    assert(policy.resolve("").kind == NavigationKind::rejected);
    assert(!policy.allow_tls_error_bypass());
    assert(policy.open_popup_as_tab());
    try { vantage::NavigationPolicy broken("https://invalid/"); assert(false); }
    catch (const std::invalid_argument &) {}
}
