#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/core/math.h>

#include <cstdint>
#include <span>
#include <vector>

namespace cubey::animation {

struct GltfNodeAnimationSample {
    bool has_translation = false;
    math::Vec3 translation{0.0F, 0.0F, 0.0F};

    bool has_rotation = false;
    math::Quat rotation{math::identity_quat()};

    bool has_scale = false;
    math::Vec3 scale{1.0F, 1.0F, 1.0F};

    bool has_weights = false;
    std::vector<float> weights{};
};

struct GltfAnimationSample {
    std::vector<GltfNodeAnimationSample> nodes{};
};

struct GltfAnimationPlayback {
    std::uint32_t animation_index = 0;
    float time_seconds = 0.0F;
    float speed = 1.0F;
    bool loop = true;
};

void advance_gltf_animation_playback(GltfAnimationPlayback& playback, float delta_seconds,
                                     float duration_seconds);

[[nodiscard]] GltfAnimationSample sample_gltf_animation(const asset::GltfAsset& asset,
                                                        const asset::GltfAnimation& animation,
                                                        float time_seconds);

[[nodiscard]] std::vector<math::Mat4>
compute_gltf_joint_palette(const asset::GltfSkin& skin, std::span<const math::Mat4> node_world,
                           std::uint32_t mesh_node_index);

} // namespace cubey::animation
