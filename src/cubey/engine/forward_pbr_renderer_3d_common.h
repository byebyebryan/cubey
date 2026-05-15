#pragma once

#include <cubey/core/math.h>
#include <cubey/engine/forward_pbr_renderer_3d.h>
#include <cubey/scene/scene.h>

#include <vulkan/vulkan.h>

#include <cmath>
#include <cstdint>

namespace cubey {

constexpr float kForwardPbrRenderer3DDegreesToRadians = 0.017453292519943295769F;

struct ForwardPbrRenderer3DShadowPushConstants {
    math::Mat4 light_mvp{1.0F};
};

static_assert(sizeof(ForwardPbrRenderer3DShadowPushConstants) == sizeof(math::Mat4));

[[nodiscard]] inline math::Vec3
forward_pbr_renderer_3d_camera_world_position(const SceneReadView& view, Entity camera) {
    const TransformInstance3D instance = view.transforms3d().instance(camera);
    const math::Mat4& world = view.transforms3d().world_affine_matrix(instance);
    return {world[3].x, world[3].y, world[3].z};
}

[[nodiscard]] inline float forward_pbr_renderer_3d_rotation_radians(float degrees) {
    return degrees * kForwardPbrRenderer3DDegreesToRadians;
}

[[nodiscard]] inline std::uint32_t forward_pbr_renderer_3d_binding(render::PbrSceneBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] inline std::uint32_t forward_pbr_renderer_3d_binding(render::PbrSkyboxBinding value) {
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] inline std::uint32_t forward_pbr_renderer_3d_binding(render::PbrPostBinding value) {
    return static_cast<std::uint32_t>(value);
}

} // namespace cubey
