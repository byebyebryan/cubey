#include <cubey/asset/gltf_asset.h>

#include "gltf_asset_internal.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include <cgltf.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey::asset::gltf_internal {
namespace {

[[nodiscard]] std::uint32_t checked_animation_index(std::ptrdiff_t index, const char* label) {
    if (index < 0 || static_cast<std::uint64_t>(index) >
                         static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
        throw gltf_error(std::string(label) + " index is out of range");
    }
    return static_cast<std::uint32_t>(index);
}

template <typename T>
[[nodiscard]] std::uint32_t animation_pointer_index(const T* pointer, const T* base,
                                                    cgltf_size count, const char* label) {
    if (pointer == nullptr) {
        return kInvalidAssetIndex;
    }
    if (base == nullptr || pointer < base || pointer >= base + count) {
        throw gltf_error(std::string(label) + " pointer is outside the glTF data");
    }
    return checked_animation_index(pointer - base, label);
}

void require_float_accessor(const cgltf_accessor* accessor, cgltf_type type, const char* label) {
    if (accessor == nullptr) {
        throw gltf_error(std::string(label) + " accessor is missing");
    }
    if (accessor->type != type) {
        throw gltf_error(std::string(label) + " accessor has unsupported type");
    }
    if (accessor->component_type != cgltf_component_type_r_32f) {
        throw gltf_error(std::string(label) + " accessor must use FLOAT components");
    }
}

[[nodiscard]] GltfAnimationInterpolation
load_animation_interpolation(cgltf_interpolation_type interpolation) {
    switch (interpolation) {
    case cgltf_interpolation_type_step:
        return GltfAnimationInterpolation::Step;
    case cgltf_interpolation_type_cubic_spline:
        return GltfAnimationInterpolation::CubicSpline;
    case cgltf_interpolation_type_linear:
    default:
        return GltfAnimationInterpolation::Linear;
    }
}

[[nodiscard]] GltfAnimationTargetPath load_animation_target_path(cgltf_animation_path_type path) {
    switch (path) {
    case cgltf_animation_path_type_translation:
        return GltfAnimationTargetPath::Translation;
    case cgltf_animation_path_type_rotation:
        return GltfAnimationTargetPath::Rotation;
    case cgltf_animation_path_type_scale:
        return GltfAnimationTargetPath::Scale;
    case cgltf_animation_path_type_weights:
        return GltfAnimationTargetPath::Weights;
    case cgltf_animation_path_type_invalid:
    default:
        throw gltf_error("unsupported animation target path");
    }
}

[[nodiscard]] bool normalized_signed_animation_accessor(const cgltf_accessor* accessor) noexcept {
    return accessor != nullptr && accessor->normalized != 0 &&
           (accessor->component_type == cgltf_component_type_r_8 ||
            accessor->component_type == cgltf_component_type_r_16);
}

[[nodiscard]] bool normalized_unsigned_animation_accessor(const cgltf_accessor* accessor) noexcept {
    return accessor != nullptr && accessor->normalized != 0 &&
           (accessor->component_type == cgltf_component_type_r_8u ||
            accessor->component_type == cgltf_component_type_r_16u);
}

void require_animation_output_component_type(const cgltf_accessor* accessor,
                                             GltfAnimationTargetPath path) {
    if (accessor->component_type == cgltf_component_type_r_32f) {
        return;
    }
    if (path == GltfAnimationTargetPath::Rotation &&
        normalized_signed_animation_accessor(accessor)) {
        return;
    }
    if (path == GltfAnimationTargetPath::Weights &&
        normalized_unsigned_animation_accessor(accessor)) {
        return;
    }
    throw gltf_error("animation output accessor has unsupported component type for target path");
}

[[nodiscard]] cgltf_size animation_output_factor(GltfAnimationInterpolation interpolation) {
    return interpolation == GltfAnimationInterpolation::CubicSpline ? 3U : 1U;
}

void require_animation_output_shape(const cgltf_animation_sampler& source,
                                    GltfAnimationInterpolation interpolation,
                                    GltfAnimationTargetPath path) {
    if (source.output == nullptr) {
        throw gltf_error("animation output accessor is missing");
    }
    require_animation_output_component_type(source.output, path);
    const cgltf_size input_count = source.input != nullptr ? source.input->count : 0U;
    const cgltf_size factor = animation_output_factor(interpolation);
    const cgltf_size output_count = source.output->count;
    if (input_count == 0U) {
        throw gltf_error("animation input accessor must contain at least one key");
    }
    switch (path) {
    case GltfAnimationTargetPath::Translation:
    case GltfAnimationTargetPath::Scale:
        if (source.output->type != cgltf_type_vec3 || output_count != input_count * factor) {
            throw gltf_error("animation translation/scale output must be a matching VEC3 accessor");
        }
        return;
    case GltfAnimationTargetPath::Rotation:
        if (source.output->type != cgltf_type_vec4 || output_count != input_count * factor) {
            throw gltf_error("animation rotation output must be a matching VEC4 accessor");
        }
        return;
    case GltfAnimationTargetPath::Weights:
        if (source.output->type != cgltf_type_scalar || output_count == 0U ||
            output_count % (input_count * factor) != 0U) {
            throw gltf_error("animation weights output must be matching scalar samples");
        }
        return;
    }
    throw gltf_error("unsupported animation target path");
}

[[nodiscard]] GltfAnimationSampler load_animation_sampler(const cgltf_animation_sampler& sampler,
                                                          GltfAnimationTargetPath target_path) {
    require_float_accessor(sampler.input, cgltf_type_scalar, "animation input");
    const GltfAnimationInterpolation interpolation =
        load_animation_interpolation(sampler.interpolation);
    require_animation_output_shape(sampler, interpolation, target_path);
    const cgltf_size component_count = cgltf_num_components(sampler.output->type);
    if (component_count == 0) {
        throw gltf_error("animation output accessor has unsupported type");
    }
    if (component_count > std::numeric_limits<std::uint32_t>::max()) {
        throw gltf_error("animation output component count is out of range");
    }

    return {
        .interpolation = interpolation,
        .input_times = read_float_accessor_values(sampler.input, 1, "animation input"),
        .output_values =
            read_float_accessor_values(sampler.output, component_count, "animation output"),
        .component_count = static_cast<std::uint32_t>(component_count),
    };
}

[[nodiscard]] GltfAnimation load_animation(const cgltf_animation& animation,
                                           const cgltf_node* node_base, cgltf_size node_count) {
    GltfAnimation result{
        .label = label_or_empty(animation.name),
    };

    std::vector<std::optional<GltfAnimationTargetPath>> sampler_targets(animation.samplers_count);
    for (cgltf_size i = 0; i < animation.channels_count; ++i) {
        const cgltf_animation_channel& channel = animation.channels[i];
        const std::uint32_t sampler_index = animation_pointer_index(
            channel.sampler, animation.samplers, animation.samplers_count, "animation sampler");
        const GltfAnimationTargetPath target_path = load_animation_target_path(channel.target_path);
        std::optional<GltfAnimationTargetPath>& prior = sampler_targets[sampler_index];
        if (prior.has_value()) {
            const bool compatible = prior.value() == target_path ||
                                    ((prior.value() == GltfAnimationTargetPath::Translation ||
                                      prior.value() == GltfAnimationTargetPath::Scale) &&
                                     (target_path == GltfAnimationTargetPath::Translation ||
                                      target_path == GltfAnimationTargetPath::Scale));
            if (!compatible) {
                throw gltf_error("animation sampler is shared across incompatible target paths");
            }
            continue;
        }
        prior = target_path;
    }

    result.samplers.reserve(animation.samplers_count);
    for (cgltf_size i = 0; i < animation.samplers_count; ++i) {
        if (!sampler_targets[i].has_value()) {
            throw gltf_error("animation sampler is not referenced by any channel");
        }
        GltfAnimationSampler sampler =
            load_animation_sampler(animation.samplers[i], sampler_targets[i].value());
        if (!sampler.input_times.empty()) {
            result.duration_seconds =
                std::max(result.duration_seconds,
                         *std::max_element(sampler.input_times.begin(), sampler.input_times.end()));
        }
        result.samplers.push_back(std::move(sampler));
    }

    result.channels.reserve(animation.channels_count);
    for (cgltf_size i = 0; i < animation.channels_count; ++i) {
        const cgltf_animation_channel& channel = animation.channels[i];
        result.channels.push_back(GltfAnimationChannel{
            .sampler_index = animation_pointer_index(channel.sampler, animation.samplers,
                                                     animation.samplers_count, "animation sampler"),
            .node_index =
                animation_pointer_index(channel.target_node, node_base, node_count, "node"),
            .target_path = load_animation_target_path(channel.target_path),
        });
    }
    return result;
}
} // namespace

void assemble_gltf_animation_data(GltfAsset& asset, const cgltf_data& data) {
    asset.animations.reserve(data.animations_count);
    for (cgltf_size i = 0; i < data.animations_count; ++i) {
        asset.animations.push_back(
            load_animation(data.animations[i], data.nodes, data.nodes_count));
    }
}

} // namespace cubey::asset::gltf_internal
