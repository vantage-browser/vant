#ifndef VANTAGE_APPLICATION_H
#define VANTAGE_APPLICATION_H

#include <cstddef>
#include <string>

namespace vantage {

class Application {
public:
    explicit Application(std::string profile_name = "default");
    ~Application();

    Application(const Application &) = delete;
    Application &operator=(const Application &) = delete;

    const std::string &profile_name() const noexcept;
    bool running() const noexcept;
    void start();
    void stop() noexcept;

    static std::size_t live_instances() noexcept;

private:
    std::string profile_name_;
    bool running_{false};
};

} // namespace vantage

#endif
