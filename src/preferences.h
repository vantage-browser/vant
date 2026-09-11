#ifndef VANTAGE_PREFERENCES_H
#define VANTAGE_PREFERENCES_H

#include <filesystem>

namespace vantage {

std::filesystem::path preferences_path();
bool compatibility_video_rendering();
void set_compatibility_video_rendering(bool enabled);
void apply_video_rendering_environment(bool enabled);

} // namespace vantage

#endif
