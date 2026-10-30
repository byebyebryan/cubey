#pragma once
#include "sdf_config.h"
#include <array>
#include <string>
#include <string_view>

namespace cubey::projects::sdf {
inline constexpr std::array<std::string_view, 6> render_modes{
    "auto", "cached", "cached-refined", "balanced", "analytic", "hybrid"};
[[nodiscard]] std::string_view render_mode_label(std::string_view mode);
[[nodiscard]] std::string render_mode_unavailable(const SdfConfig& config, std::string_view mode,
                                                  bool authored);
void resolve_render_mode(SdfConfig& config, bool authored);
[[nodiscard]] SdfConfig config_for_render_mode(const SdfConfig& current, std::string_view mode,
                                               bool authored);
[[nodiscard]] bool uses_public_cached_quality(const SdfConfig& config);
} // namespace cubey::projects::sdf
