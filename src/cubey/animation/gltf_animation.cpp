#include <cubey/animation/gltf_animation.h>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace cubey::animation {
namespace {

[[nodiscard]] std::size_t spline_factor(const asset::GltfAnimationSampler& sampler) {
    return sampler.interpolation == asset::GltfAnimationInterpolation::CubicSpline ? 3U : 1U;
}

[[nodiscard]] std::size_t expected_width(asset::GltfAnimationTargetPath path) {
    switch (path) {
    case asset::GltfAnimationTargetPath::Translation:
    case asset::GltfAnimationTargetPath::Scale:
        return 3;
    case asset::GltfAnimationTargetPath::Rotation:
        return 4;
    case asset::GltfAnimationTargetPath::Weights:
        return 0;
    }
    throw std::runtime_error("animation target path is invalid");
}

[[nodiscard]] std::size_t sample_width(const asset::GltfAnimationSampler& sampler,
                                       asset::GltfAnimationTargetPath path) {
    if (sampler.input_times.empty()) {
        throw std::runtime_error("animation sampler must have input times");
    }
    const std::size_t fixed_width = expected_width(path);
    if (fixed_width != 0) {
        if (sampler.component_count != fixed_width) {
            throw std::runtime_error("animation sampler component count does not match target");
        }
        return fixed_width;
    }

    const std::size_t divisor = sampler.input_times.size() * spline_factor(sampler);
    if (divisor == 0 || sampler.output_values.size() % divisor != 0) {
        throw std::runtime_error("animation weight sampler output count is invalid");
    }
    const std::size_t width = sampler.output_values.size() / divisor;
    if (width == 0) {
        throw std::runtime_error("animation weight sampler must contain at least one weight");
    }
    return width;
}

void validate_sampler_values(const asset::GltfAnimationSampler& sampler, std::size_t width) {
    const std::size_t expected = sampler.input_times.size() * spline_factor(sampler) * width;
    if (sampler.output_values.size() != expected) {
        throw std::runtime_error("animation sampler output count does not match input count");
    }
}

[[nodiscard]] std::size_t find_key_index(std::span<const float> times, float time_seconds) {
    if (times.size() < 2 || time_seconds <= times.front()) {
        return 0;
    }
    for (std::size_t index = 0; index + 1 < times.size(); ++index) {
        if (time_seconds < times[index + 1]) {
            return index;
        }
    }
    return times.size() - 1;
}

[[nodiscard]] float normalized_time(std::span<const float> times, std::size_t key,
                                    float time_seconds) {
    if (key + 1 >= times.size()) {
        return 0.0F;
    }
    const float start = times[key];
    const float end = times[key + 1];
    const float duration = end - start;
    if (duration <= 0.0F) {
        return 0.0F;
    }
    return std::clamp((time_seconds - start) / duration, 0.0F, 1.0F);
}

[[nodiscard]] const float* key_value(const asset::GltfAnimationSampler& sampler, std::size_t key,
                                     std::size_t width) {
    const std::size_t factor = spline_factor(sampler);
    const std::size_t value_offset =
        sampler.interpolation == asset::GltfAnimationInterpolation::CubicSpline ? 1U : 0U;
    return sampler.output_values.data() + (((key * factor) + value_offset) * width);
}

[[nodiscard]] const float* cubic_in_tangent(const asset::GltfAnimationSampler& sampler,
                                            std::size_t key, std::size_t width) {
    return sampler.output_values.data() + ((key * 3U) * width);
}

[[nodiscard]] const float* cubic_out_tangent(const asset::GltfAnimationSampler& sampler,
                                             std::size_t key, std::size_t width) {
    return sampler.output_values.data() + (((key * 3U) + 2U) * width);
}

[[nodiscard]] math::Quat quat_from_gltf_sample(std::span<const float> values);
[[nodiscard]] std::vector<float> slerp_gltf_rotation(const float* first, const float* second,
                                                     float t);

[[nodiscard]] std::vector<float> sample_vector(const asset::GltfAnimationSampler& sampler,
                                               asset::GltfAnimationTargetPath path,
                                               float time_seconds) {
    const std::size_t width = sample_width(sampler, path);
    validate_sampler_values(sampler, width);

    const std::size_t key = find_key_index(sampler.input_times, time_seconds);
    std::vector<float> result(width, 0.0F);
    const float* first = key_value(sampler, key, width);
    if (key + 1 >= sampler.input_times.size() ||
        sampler.interpolation == asset::GltfAnimationInterpolation::Step) {
        std::copy(first, first + width, result.begin());
        return result;
    }

    const float t = normalized_time(sampler.input_times, key, time_seconds);
    const float* second = key_value(sampler, key + 1U, width);
    if (path == asset::GltfAnimationTargetPath::Rotation &&
        sampler.interpolation == asset::GltfAnimationInterpolation::Linear) {
        return slerp_gltf_rotation(first, second, t);
    }
    if (sampler.interpolation != asset::GltfAnimationInterpolation::CubicSpline) {
        for (std::size_t component = 0; component < width; ++component) {
            result[component] = glm::mix(first[component], second[component], t);
        }
        return result;
    }

    const float dt = sampler.input_times[key + 1U] - sampler.input_times[key];
    const float t2 = t * t;
    const float t3 = t2 * t;
    const float h00 = (2.0F * t3) - (3.0F * t2) + 1.0F;
    const float h10 = t3 - (2.0F * t2) + t;
    const float h01 = (-2.0F * t3) + (3.0F * t2);
    const float h11 = t3 - t2;
    const float* out_tangent = cubic_out_tangent(sampler, key, width);
    const float* in_tangent = cubic_in_tangent(sampler, key + 1U, width);
    for (std::size_t component = 0; component < width; ++component) {
        result[component] = (h00 * first[component]) + (h10 * out_tangent[component] * dt) +
                            (h01 * second[component]) + (h11 * in_tangent[component] * dt);
    }
    return result;
}

[[nodiscard]] math::Vec3 vec3_from_sample(std::span<const float> values) {
    if (values.size() != 3) {
        throw std::runtime_error("animation sample is not a vec3");
    }
    return {values[0], values[1], values[2]};
}

[[nodiscard]] math::Quat quat_from_gltf_sample(std::span<const float> values) {
    if (values.size() != 4) {
        throw std::runtime_error("animation sample is not a quaternion");
    }
    return glm::normalize(math::Quat{values[3], values[0], values[1], values[2]});
}

[[nodiscard]] std::vector<float> gltf_sample_from_quat(math::Quat value) {
    const math::Quat normalized = glm::normalize(value);
    return {normalized.x, normalized.y, normalized.z, normalized.w};
}

[[nodiscard]] std::vector<float> slerp_gltf_rotation(const float* first, const float* second,
                                                     float t) {
    math::Quat start = quat_from_gltf_sample(std::span<const float>{first, 4});
    math::Quat end = quat_from_gltf_sample(std::span<const float>{second, 4});
    if (glm::dot(start, end) < 0.0F) {
        end = -end;
    }
    return gltf_sample_from_quat(glm::slerp(start, end, t));
}

void apply_channel_sample(GltfNodeAnimationSample& node_sample, asset::GltfAnimationTargetPath path,
                          std::vector<float> values) {
    switch (path) {
    case asset::GltfAnimationTargetPath::Translation:
        node_sample.has_translation = true;
        node_sample.translation = vec3_from_sample(values);
        return;
    case asset::GltfAnimationTargetPath::Rotation:
        node_sample.has_rotation = true;
        node_sample.rotation = quat_from_gltf_sample(values);
        return;
    case asset::GltfAnimationTargetPath::Scale:
        node_sample.has_scale = true;
        node_sample.scale = vec3_from_sample(values);
        return;
    case asset::GltfAnimationTargetPath::Weights:
        node_sample.has_weights = true;
        node_sample.weights = std::move(values);
        return;
    }
    throw std::runtime_error("animation target path is invalid");
}

} // namespace

void advance_gltf_animation_playback(GltfAnimationPlayback& playback, float delta_seconds,
                                     float duration_seconds) {
    playback.time_seconds += delta_seconds * playback.speed;
    if (duration_seconds <= 0.0F) {
        playback.time_seconds = 0.0F;
        return;
    }
    if (playback.loop) {
        playback.time_seconds = std::fmod(playback.time_seconds, duration_seconds);
        if (playback.time_seconds < 0.0F) {
            playback.time_seconds += duration_seconds;
        }
        return;
    }
    playback.time_seconds = std::clamp(playback.time_seconds, 0.0F, duration_seconds);
}

GltfAnimationSample sample_gltf_animation(const asset::GltfAsset& asset,
                                          const asset::GltfAnimation& animation,
                                          float time_seconds) {
    GltfAnimationSample result;
    result.nodes.resize(asset.nodes.size());

    for (const asset::GltfAnimationChannel& channel : animation.channels) {
        if (channel.node_index >= result.nodes.size()) {
            throw std::runtime_error("animation channel node index is out of range");
        }
        if (channel.sampler_index >= animation.samplers.size()) {
            throw std::runtime_error("animation channel sampler index is out of range");
        }
        const asset::GltfAnimationSampler& sampler = animation.samplers[channel.sampler_index];
        apply_channel_sample(result.nodes[channel.node_index], channel.target_path,
                             sample_vector(sampler, channel.target_path, time_seconds));
    }

    return result;
}

std::vector<math::Mat4> compute_gltf_joint_palette(const asset::GltfSkin& skin,
                                                   std::span<const math::Mat4> node_world,
                                                   std::uint32_t mesh_node_index) {
    if (mesh_node_index >= node_world.size()) {
        throw std::runtime_error("mesh node index is out of range for joint palette");
    }
    if (!skin.inverse_bind_matrices.empty() &&
        skin.inverse_bind_matrices.size() != skin.joints.size()) {
        throw std::runtime_error("skin inverse bind matrix count must match joint count");
    }

    const math::Mat4 inverse_mesh_world = glm::inverse(node_world[mesh_node_index]);
    std::vector<math::Mat4> palette;
    palette.reserve(skin.joints.size());
    for (std::size_t index = 0; index < skin.joints.size(); ++index) {
        const std::uint32_t joint = skin.joints[index];
        if (joint >= node_world.size()) {
            throw std::runtime_error("skin joint index is out of range for joint palette");
        }
        const math::Mat4 inverse_bind = skin.inverse_bind_matrices.empty()
                                            ? math::Mat4{1.0F}
                                            : skin.inverse_bind_matrices[index];
        palette.push_back(inverse_mesh_world * node_world[joint] * inverse_bind);
    }
    return palette;
}

} // namespace cubey::animation
