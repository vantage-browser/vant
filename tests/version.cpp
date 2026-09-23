#include "version.h"

#include <cassert>
#include <string_view>

int main() {
    static_assert(vantage::version == std::string_view{"0.1.11"});
    assert(!vantage::version.empty());
}
