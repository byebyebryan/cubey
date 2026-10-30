#include "sdf_render_modes.h"
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
// Sample settings remain independent; box recipes retain their Cached route.
constexpr std::string_view authored_default = "balanced";
template <typename T> void apply(const std::optional<T>& request, T& value) {
    if (request)
        value = *request;
}
template <typename T>
void agrees(const std::optional<T>& request, const T& value, std::string_view name) {
    if (request && *request != value)
        throw std::invalid_argument("--render-mode conflicts with --" + std::string(name));
}
bool private_request(const SdfConfig& c) {
    return c.comparison_stage != "off" || c.comparison_tight_root ||
           c.comparison_residency != "host" || c.comparison_donor || !c.comparison_input.empty() ||
           !c.query_workload_output.empty() || c.validate || c.validate_batches ||
           c.validate_subtractions || c.validate_history;
}
} // namespace
void normalize_render_request(SdfConfig& c, bool mode_explicit) {
    const auto& r = c.route_overrides;
    apply(r.surface_trace, c.surface_trace);
    apply(r.csg_evaluation, c.csg_evaluation);
    apply(r.cached_quality, c.cached_quality);
    apply(r.acceleration_variant, c.acceleration_variant);
    apply(r.source_shaders, c.source_shaders);
    apply(r.source_optimization, c.source_optimization);
    apply(r.query_stepping, c.query_stepping);
    apply(r.frame_profile, c.frame_profile);
    apply(r.region_axis, c.region_axis);
    const bool overrides = r.surface_trace || r.csg_evaluation || r.cached_quality ||
                           r.acceleration_variant || r.source_shaders || r.source_optimization ||
                           r.query_stepping || r.frame_profile || r.region_axis;
    c.named_render_mode = mode_explicit || !(overrides || private_request(c));
}
std::string_view render_mode_label(std::string_view mode) {
    if (mode == "auto")
        return "Default";
    if (mode == "cached")
        return "Cached";
    if (mode == "cached-refined")
        return "Cached refined";
    if (mode == "balanced")
        return "Balanced";
    if (mode == "analytic")
        return "Analytical";
    if (mode == "hybrid")
        return "Hybrid";
    return "Advanced controls";
}
std::string render_mode_unavailable(const SdfConfig& c, std::string_view mode, bool authored) {
    if (std::find(render_modes.begin(), render_modes.end(), mode) == render_modes.end())
        return "Unknown rendering mode";
    if (!authored && mode != "auto" && mode != "cached")
        return "Requires an authored scene; box recipes use Cached";
    if ((mode == "analytic" || mode == "hybrid") && c.debug == 4)
        return "Brick debug requires a cached rendering mode";
    if (private_request(c))
        return "Explicit tracing studies and field oracles use their advanced controls";
    return {};
}
void resolve_render_mode(SdfConfig& c, bool authored) {
    if (!c.named_render_mode) {
        c.effective_render_mode = "custom";
        return;
    }
    if (const auto reason = render_mode_unavailable(c, c.render_mode, authored); !reason.empty())
        throw std::invalid_argument("--render-mode " + c.render_mode + ": " + reason);
    auto resolved = c;
    resolved.effective_render_mode = c.render_mode == "auto"
                                         ? std::string(authored ? authored_default : "cached")
                                         : c.render_mode;
    resolved.surface_trace = "sampled";
    resolved.csg_evaluation = "full";
    resolved.cached_quality = "ordinary";
    resolved.region_axis = 16;
    resolved.acceleration_variant = "off";
    resolved.source_shaders = "automatic";
    resolved.source_optimization = "automatic";
    resolved.query_stepping = "ordinary";
    resolved.frame_profile = "off";
    if (resolved.effective_render_mode == "cached-refined" ||
        resolved.effective_render_mode == "balanced") {
        resolved.region_axis = 64;
        resolved.cached_quality =
            resolved.effective_render_mode == "balanced" ? "refine-normal" : "refine-0.03125";
    } else if (resolved.effective_render_mode == "analytic" ||
               resolved.effective_render_mode == "hybrid") {
        resolved.surface_trace = resolved.effective_render_mode;
        resolved.csg_evaluation = "pruned";
        if (resolved.effective_render_mode == "analytic" && resolved.lighting == "atmosphere") {
            resolved.region_axis = 64;
            resolved.acceleration_variant = "root-far";
        }
    }
    const auto& r = c.route_overrides;
    agrees(r.surface_trace, resolved.surface_trace, "surface-trace");
    agrees(r.csg_evaluation, resolved.csg_evaluation, "csg-evaluation");
    agrees(r.cached_quality, resolved.cached_quality, "cached-quality");
    agrees(r.region_axis, resolved.region_axis, "region-axis");
    agrees(r.acceleration_variant, resolved.acceleration_variant, "acceleration-variant");
    agrees(r.source_shaders, resolved.source_shaders, "source-shaders");
    agrees(r.source_optimization, resolved.source_optimization, "source-optimization");
    agrees(r.query_stepping, resolved.query_stepping, "query-stepping");
    agrees(r.frame_profile, resolved.frame_profile, "frame-profile");
    c = std::move(resolved);
}
SdfConfig config_for_render_mode(const SdfConfig& current, std::string_view mode, bool authored) {
    auto next = current;
    next.render_mode = mode;
    next.named_render_mode = true;
    next.route_overrides = {};
    resolve_render_mode(next, authored);
    return next;
}
bool uses_public_cached_quality(const SdfConfig& c) {
    return c.named_render_mode &&
           (c.effective_render_mode == "cached-refined" || c.effective_render_mode == "balanced");
}
bool uses_public_source_acceleration(const SdfConfig& c) {
    return c.named_render_mode && c.effective_render_mode == "analytic" &&
           c.surface_trace == "analytic" && c.csg_evaluation == "pruned" && c.region_axis == 64 &&
           c.acceleration_variant == "root-far" && c.source_optimization == "automatic" &&
           c.query_stepping == "ordinary" && c.lighting == "atmosphere";
}
} // namespace cubey::projects::sdf
