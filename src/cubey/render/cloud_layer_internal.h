#pragma once

#include <cubey/render/cloud_layer.h>

namespace cubey::render::detail {

struct CloudLayerShadowPushConstants {
    math::Vec4 receiver_center_half_extent;
    math::Vec4 receiver_axis_u_texel_size;
    math::Vec4 receiver_axis_v_steps;
    math::Vec4 light_direction_intensity;
};

static_assert(sizeof(CloudLayerShadowPushConstants) == sizeof(float) * 16U);
[[nodiscard]] inline bool cloud_layer_composite_mode_has_background(CloudLayerCompositeMode mode) {
    return mode == CloudLayerCompositeMode::ExternalBackground ||
           mode == CloudLayerCompositeMode::ExternalBackgroundSceneDepth;
}

[[nodiscard]] inline bool cloud_layer_composite_mode_has_scene_depth(CloudLayerCompositeMode mode) {
    return mode == CloudLayerCompositeMode::ExternalBackgroundSceneDepth;
}


} // namespace cubey::render::detail
