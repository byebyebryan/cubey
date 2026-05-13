#pragma once

#include <cubey/core/math.h>
#include <cubey/render/material.h>
#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>

namespace cubey::examples::shadow_cube::detail {

inline constexpr std::uint32_t kShadowMapSize = 1024;

struct ShadowPushConstants {
    cubey::math::Mat4 light_mvp;
};

struct ScenePushConstants {
    cubey::math::Mat4 mvp;
    cubey::math::Mat4 light_mvp;
};

static_assert(sizeof(ShadowPushConstants) == sizeof(cubey::math::Mat4));
static_assert(sizeof(ScenePushConstants) == sizeof(cubey::math::Mat4) * 2U);

[[nodiscard]] const cubey::math::Vec3& light_direction();
[[nodiscard]] std::filesystem::path shader_path(const char* filename);

[[nodiscard]] cubey::render::MaterialPassInfo shadow_scene_pass_info();
[[nodiscard]] cubey::render::MaterialPassInfo shadow_present_pass_info();

[[nodiscard]] cubey::render::RenderGraphTextureState undefined_texture_state();
[[nodiscard]] cubey::render::RenderGraphTextureState sampled_depth_texture_state();
[[nodiscard]] cubey::render::RenderGraphTextureState present_texture_state();

} // namespace cubey::examples::shadow_cube::detail
