#include "application.h"

#include <iostream>
#include <string_view>

namespace {
constexpr std::string_view version = "0.0.0-dev";

void usage(std::ostream &out) {
    out << "usage: vant [--headless-smoke|--version|--help]\n";
}
}

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "Vantage Browser " << version << '\n';
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        usage(std::cout);
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--headless-smoke") {
        vantage::Application app;
        app.start();
        app.stop();
        return vantage::Application::live_instances() == 1 ? 0 : 1;
    }
    usage(std::cerr);
    return 2;
}
