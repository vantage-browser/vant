#include "application.h"

#include <cassert>
#include <stdexcept>

int main() {
    assert(vantage::Application::live_instances() == 0);
    {
        vantage::Application app("test");
        assert(vantage::Application::live_instances() == 1);
        assert(app.profile_name() == "test");
        assert(!app.running());
        app.start();
        assert(app.running());
        app.stop();
        assert(!app.running());
    }
    assert(vantage::Application::live_instances() == 0);
    try {
        vantage::Application invalid("");
        assert(false);
    } catch (const std::invalid_argument &) {
    }
}
