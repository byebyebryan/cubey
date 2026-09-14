#include "forward_pbr_renderer_3d_internal.h"

#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace cubey {
namespace {

[[nodiscard]] std::size_t route_index(ForwardPbrDrawRoute route) noexcept {
    return static_cast<std::size_t>(route);
}

[[nodiscard]] bool has_supported_cull_mode(const scene::RenderDrawPacket3D& packet) noexcept {
    return packet.material_info.cull_mode == VK_CULL_MODE_BACK_BIT ||
           packet.material_info.cull_mode == VK_CULL_MODE_NONE;
}

[[nodiscard]] ForwardPbrDrawRoute route_for_cull(ForwardPbrDrawRoute back,
                                                 ForwardPbrDrawRoute no_cull,
                                                 VkCullModeFlags cull_mode) noexcept {
    return cull_mode == VK_CULL_MODE_BACK_BIT ? back : no_cull;
}

[[nodiscard]] std::optional<ForwardPbrDrawRoute>
shadow_route(const scene::RenderDrawPacket3D& packet) noexcept {
    if (!packet.cast_shadows ||
        !render::material_supports_pass(packet.material_info,
                                        render::MaterialPassKind::DepthOnly) ||
        packet.material_info.optical_mode != render::MaterialOpticalMode::Opaque ||
        !has_supported_cull_mode(packet)) {
        return std::nullopt;
    }

    switch (packet.material_info.alpha_mode) {
    case render::MaterialAlphaMode::Opaque:
        return route_for_cull(ForwardPbrDrawRoute::ShadowOpaqueBack,
                              ForwardPbrDrawRoute::ShadowOpaqueNoCull,
                              packet.material_info.cull_mode);
    case render::MaterialAlphaMode::Mask:
        return route_for_cull(ForwardPbrDrawRoute::ShadowMaskedBack,
                              ForwardPbrDrawRoute::ShadowMaskedNoCull,
                              packet.material_info.cull_mode);
    case render::MaterialAlphaMode::Blend:
        return std::nullopt;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ForwardPbrDrawRoute>
scene_route(const scene::RenderDrawPacket3D& packet) noexcept {
    if (!render::material_supports_pass(packet.material_info,
                                        render::MaterialPassKind::ForwardColor) ||
        !has_supported_cull_mode(packet)) {
        return std::nullopt;
    }

    const bool back_culled = packet.material_info.cull_mode == VK_CULL_MODE_BACK_BIT;
    switch (packet.material_info.optical_mode) {
    case render::MaterialOpticalMode::Opaque:
        switch (packet.material_info.blend) {
        case render::MaterialBlendMode::Opaque:
            return back_culled ? ForwardPbrDrawRoute::SceneOpaqueBack
                               : ForwardPbrDrawRoute::SceneOpaqueNoCull;
        case render::MaterialBlendMode::AlphaBlend:
            return back_culled ? ForwardPbrDrawRoute::SceneAlphaBack
                               : ForwardPbrDrawRoute::SceneAlphaNoCull;
        }
        break;
    case render::MaterialOpticalMode::Transmission:
        switch (packet.material_info.blend) {
        case render::MaterialBlendMode::Opaque:
            return back_culled ? ForwardPbrDrawRoute::TransmissionOpaqueBack
                               : ForwardPbrDrawRoute::TransmissionOpaqueNoCull;
        case render::MaterialBlendMode::AlphaBlend:
            return back_culled ? ForwardPbrDrawRoute::TransmissionAlphaBack
                               : ForwardPbrDrawRoute::TransmissionAlphaNoCull;
        }
        break;
    }
    return std::nullopt;
}

void validate_packet_indexable(std::size_t packet_count, const char* plan_name) {
    if (packet_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string{"forward PBR "} + plan_name +
                                 " plan exceeds 32-bit packet indices");
    }
}

} // namespace

std::span<const std::uint32_t> ForwardPbrDrawPlan::indices(ForwardPbrDrawRoute route) const {
    return route_indices_.at(route_index(route));
}

ForwardPbrDrawPlan build_forward_pbr_draw_plan(const ForwardPbrRenderer3DFramePlans& frame_plans,
                                               ForwardPbrRenderer3DFrameDrawMetrics* metrics) {
    if (frame_plans.shadow == nullptr || frame_plans.scene == nullptr) {
        throw std::runtime_error("forward PBR draw plan requires shadow and scene frame plans");
    }

    if (metrics != nullptr) {
        *metrics = {};
    }

    ForwardPbrDrawPlan result;
    const scene::RenderFramePlan3D& shadow_plan = *frame_plans.shadow;
    const scene::RenderFramePlan3D& scene_plan = *frame_plans.scene;
    validate_packet_indexable(shadow_plan.draw_packets.size(), "shadow");
    validate_packet_indexable(scene_plan.draw_packets.size(), "scene");
    if (metrics != nullptr) {
        metrics->shadow_source_packet_count = shadow_plan.draw_packets.size();
        metrics->scene_source_packet_count = scene_plan.draw_packets.size();
    }

    for (std::uint32_t packet_index = 0;
         packet_index < static_cast<std::uint32_t>(shadow_plan.draw_packets.size());
         ++packet_index) {
        if (metrics != nullptr) {
            ++metrics->shadow_classification_count;
        }
        const std::optional<ForwardPbrDrawRoute> route =
            shadow_route(shadow_plan.draw_packets[packet_index]);
        if (!route.has_value()) {
            continue;
        }
        result.route_indices_[route_index(route.value())].push_back(packet_index);
        if (metrics != nullptr) {
            ++metrics->route_packet_reference_count;
        }
    }

    std::optional<std::unordered_set<render::MaterialHandle, render::MaterialHandleHash>>
        visible_materials;
    if (metrics != nullptr) {
        visible_materials.emplace();
    }
    for (std::uint32_t packet_index = 0;
         packet_index < static_cast<std::uint32_t>(scene_plan.draw_packets.size());
         ++packet_index) {
        if (metrics != nullptr) {
            ++metrics->scene_classification_count;
        }
        const scene::RenderDrawPacket3D& packet = scene_plan.draw_packets[packet_index];
        result.has_transmission_ =
            result.has_transmission_ || render::material_uses_transmission(packet.material_info);
        const std::optional<ForwardPbrDrawRoute> route = scene_route(packet);
        if (!route.has_value()) {
            continue;
        }
        result.route_indices_[route_index(route.value())].push_back(packet_index);
        if (metrics != nullptr) {
            ++metrics->route_packet_reference_count;
            visible_materials->insert(packet.material);
        }
    }
    if (metrics != nullptr) {
        metrics->visible_scene_unique_material_count = visible_materials->size();
        metrics->has_transmission = result.has_transmission_;
    }
    return result;
}

} // namespace cubey
