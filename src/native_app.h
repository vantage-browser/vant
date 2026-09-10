#ifndef VANTAGE_NATIVE_APP_H
#define VANTAGE_NATIVE_APP_H

#include <string>

namespace vantage {
int run_native(bool smoke, const std::string &initial_uri);
std::string native_versions();
}

#endif
