#ifndef VANTAGE_NATIVE_APP_H
#define VANTAGE_NATIVE_APP_H

#include "launch_options.h"

namespace vantage {
int run_native(const NativeLaunchOptions &options);
std::string native_versions();
}

#endif
