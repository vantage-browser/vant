#ifndef VANTAGE_PREFERENCES_H
#define VANTAGE_PREFERENCES_H

#include <filesystem>
#include <string>
#include <vector>

namespace vantage {

std::filesystem::path preferences_path();
bool compatibility_video_rendering();
void set_compatibility_video_rendering(bool enabled);
void apply_video_rendering_environment(bool enabled);
std::vector<std::string> dark_mode_enabled_domains();
bool dark_mode_enabled_for_domain(const std::string &domain);
void set_dark_mode_enabled_for_domain(const std::string &domain, bool enabled);

} // namespace vantage

#endif
