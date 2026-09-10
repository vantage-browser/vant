#include "application.h"

#include <atomic>
#include <stdexcept>
#include <utility>

namespace {
std::atomic_size_t live_instance_count{0};
}

namespace vantage {

Application::Application(std::string profile_name)
    : profile_name_(std::move(profile_name)) {
    if (profile_name_.empty()) {
        throw std::invalid_argument("profile name must not be empty");
    }
    ++live_instance_count;
}

Application::~Application() {
    stop();
    --live_instance_count;
}

const std::string &Application::profile_name() const noexcept { return profile_name_; }
bool Application::running() const noexcept { return running_; }
void Application::start() { running_ = true; }
void Application::stop() noexcept { running_ = false; }
std::size_t Application::live_instances() noexcept { return ::live_instance_count.load(); }

} // namespace vantage
