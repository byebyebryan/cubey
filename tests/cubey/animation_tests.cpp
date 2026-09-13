#include <cubey/animation/gltf_animation.h>

#include <cubey/asset/gltf_asset.h>
#include <cubey/core/math.h>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float value, float expected, const char* message) {
    constexpr float kTolerance = 0.0001F;
    if (value < expected - kTolerance || value > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_vec3_close(cubey::math::Vec3 value, cubey::math::Vec3 expected, const char* message) {
    require_close(value.x, expected.x, message);
    require_close(value.y, expected.y, message);
    require_close(value.z, expected.z, message);
}

cubey::asset::GltfAnimationSampler
linear_sampler(std::vector<float> times, std::vector<float> values, std::uint32_t components) {
    return {
        .interpolation = cubey::asset::GltfAnimationInterpolation::Linear,
        .input_times = std::move(times),
        .output_values = std::move(values),
        .component_count = components,
    };
}

template <typename Data>
concept RuntimeDataHasImages = requires(Data value) { value.images; };

template <typename Data>
concept RuntimeDataHasMaterials = requires(Data value) { value.materials; };

template <typename Data>
concept RuntimeDataHasSamplers = requires(Data value) { value.samplers; };

template <typename Data>
concept RuntimeDataHasSourcePath = requires(Data value) { value.source_path; };

template <typename Mesh>
concept RuntimeMeshHasPrimitives = requires(Mesh value) { value.primitives; };

template <typename Data>
concept RuntimeDataHasTextures = requires(Data value) { value.textures; };

static_assert(!RuntimeDataHasImages<cubey::asset::GltfRuntimeSceneData>);
static_assert(!RuntimeDataHasMaterials<cubey::asset::GltfRuntimeSceneData>);
static_assert(!RuntimeDataHasSamplers<cubey::asset::GltfRuntimeSceneData>);
static_assert(!RuntimeDataHasSourcePath<cubey::asset::GltfRuntimeSceneData>);
static_assert(!RuntimeDataHasTextures<cubey::asset::GltfRuntimeSceneData>);
static_assert(!RuntimeMeshHasPrimitives<cubey::asset::GltfRuntimeMesh>);

} // namespace

void test_gltf_animation_wraps_looping_playback_time() {
    cubey::animation::GltfAnimationPlayback playback{
        .time_seconds = 0.75F,
        .speed = 2.0F,
        .loop = true,
    };

    cubey::animation::advance_gltf_animation_playback(playback, 0.5F, 1.0F);

    require_close(playback.time_seconds, 0.75F, "looping playback should wrap by clip duration");
}

void test_gltf_animation_samples_linear_translation() {
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(1);
    asset.animations.emplace_back();
    cubey::asset::GltfAnimation& animation = asset.animations.back();
    animation.samplers.push_back(
        linear_sampler({0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 2.0F, 4.0F, 6.0F}, 3));
    animation.channels.push_back({
        .sampler_index = 0,
        .node_index = 0,
        .target_path = cubey::asset::GltfAnimationTargetPath::Translation,
    });

    const cubey::asset::GltfRuntimeSceneData runtime =
        cubey::asset::consume_gltf_runtime_scene_data(std::move(asset));
    const cubey::animation::GltfAnimationSample sample =
        cubey::animation::sample_gltf_animation(runtime, runtime.animations.front(), 0.25F);

    require(sample.nodes.size() == 1, "sample should include one node slot");
    require(sample.nodes[0].has_translation, "sample should mark translation as present");
    require_vec3_close(sample.nodes[0].translation, {0.5F, 1.0F, 1.5F},
                       "linear translation should interpolate");
}

void test_gltf_animation_samples_step_scale() {
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(1);
    asset.animations.emplace_back();
    cubey::asset::GltfAnimation& animation = asset.animations.back();
    animation.samplers.push_back({
        .interpolation = cubey::asset::GltfAnimationInterpolation::Step,
        .input_times = {0.0F, 1.0F},
        .output_values = {1.0F, 1.0F, 1.0F, 3.0F, 3.0F, 3.0F},
        .component_count = 3,
    });
    animation.channels.push_back({
        .sampler_index = 0,
        .node_index = 0,
        .target_path = cubey::asset::GltfAnimationTargetPath::Scale,
    });

    const cubey::asset::GltfRuntimeSceneData runtime =
        cubey::asset::consume_gltf_runtime_scene_data(std::move(asset));
    const cubey::animation::GltfAnimationSample sample =
        cubey::animation::sample_gltf_animation(runtime, runtime.animations.front(), 0.75F);

    require(sample.nodes[0].has_scale, "sample should mark scale as present");
    require_vec3_close(sample.nodes[0].scale, {1.0F, 1.0F, 1.0F},
                       "step scale should hold the previous key");
}

void test_gltf_animation_slerps_rotation_and_normalizes() {
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(1);
    const cubey::math::Quat start =
        cubey::math::angle_axis_quat(glm::half_pi<float>(), {1.0F, 0.0F, 0.0F});
    const cubey::math::Quat end =
        cubey::math::angle_axis_quat(glm::half_pi<float>(), {0.0F, 1.0F, 0.0F});
    asset.animations.emplace_back();
    cubey::asset::GltfAnimation& animation = asset.animations.back();
    animation.samplers.push_back(linear_sampler(
        {0.0F, 1.0F}, {start.x, start.y, start.z, start.w, end.x, end.y, end.z, end.w}, 4));
    animation.channels.push_back({
        .sampler_index = 0,
        .node_index = 0,
        .target_path = cubey::asset::GltfAnimationTargetPath::Rotation,
    });

    const cubey::asset::GltfRuntimeSceneData runtime =
        cubey::asset::consume_gltf_runtime_scene_data(std::move(asset));
    const cubey::animation::GltfAnimationSample sample =
        cubey::animation::sample_gltf_animation(runtime, runtime.animations.front(), 0.5F);

    require(sample.nodes[0].has_rotation, "sample should mark rotation as present");
    require_close(glm::length(sample.nodes[0].rotation), 1.0F,
                  "sampled rotation should be normalized");
    const cubey::math::Quat expected = glm::slerp(start, end, 0.5F);
    require_close(sample.nodes[0].rotation.x, expected.x,
                  "linear rotation animation should use spherical interpolation x");
    require_close(sample.nodes[0].rotation.y, expected.y,
                  "linear rotation animation should use spherical interpolation y");
    require_close(sample.nodes[0].rotation.z, expected.z,
                  "linear rotation animation should use spherical interpolation z");
    require_close(sample.nodes[0].rotation.w, expected.w,
                  "linear rotation animation should use spherical interpolation w");
}

void test_gltf_animation_samples_cubic_translation() {
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(1);
    asset.animations.emplace_back();
    cubey::asset::GltfAnimation& animation = asset.animations.back();
    animation.samplers.push_back({
        .interpolation = cubey::asset::GltfAnimationInterpolation::CubicSpline,
        .input_times = {0.0F, 1.0F},
        .output_values =
            {
                0.0F,
                0.0F,
                0.0F, // key 0 in tangent
                0.0F,
                0.0F,
                0.0F, // key 0 value
                0.0F,
                0.0F,
                0.0F, // key 0 out tangent
                0.0F,
                0.0F,
                0.0F, // key 1 in tangent
                2.0F,
                0.0F,
                0.0F, // key 1 value
                0.0F,
                0.0F,
                0.0F, // key 1 out tangent
            },
        .component_count = 3,
    });
    animation.channels.push_back({
        .sampler_index = 0,
        .node_index = 0,
        .target_path = cubey::asset::GltfAnimationTargetPath::Translation,
    });

    const cubey::asset::GltfRuntimeSceneData runtime =
        cubey::asset::consume_gltf_runtime_scene_data(std::move(asset));
    const cubey::animation::GltfAnimationSample sample =
        cubey::animation::sample_gltf_animation(runtime, runtime.animations.front(), 0.5F);

    require_vec3_close(sample.nodes[0].translation, {1.0F, 0.0F, 0.0F},
                       "cubic spline translation should interpolate Hermite values");
}

void test_gltf_animation_samples_morph_weights() {
    cubey::asset::GltfAsset asset;
    asset.nodes.resize(1);
    asset.animations.emplace_back();
    cubey::asset::GltfAnimation& animation = asset.animations.back();
    animation.samplers.push_back(linear_sampler({0.0F, 1.0F}, {0.0F, 1.0F, 1.0F, 0.0F}, 1));
    animation.channels.push_back({
        .sampler_index = 0,
        .node_index = 0,
        .target_path = cubey::asset::GltfAnimationTargetPath::Weights,
    });

    const cubey::asset::GltfRuntimeSceneData runtime =
        cubey::asset::consume_gltf_runtime_scene_data(std::move(asset));
    const cubey::animation::GltfAnimationSample sample =
        cubey::animation::sample_gltf_animation(runtime, runtime.animations.front(), 0.5F);

    require(sample.nodes[0].has_weights, "sample should mark weights as present");
    require(sample.nodes[0].weights.size() == 2, "weights sample should infer target count");
    require_close(sample.nodes[0].weights[0], 0.5F, "first morph weight should interpolate");
    require_close(sample.nodes[0].weights[1], 0.5F, "second morph weight should interpolate");
}

void test_gltf_animation_computes_joint_palette_from_world_matrices() {
    cubey::asset::GltfRuntimeSkin skin;
    skin.joints = {1};
    skin.inverse_bind_matrices = {cubey::math::translation(-1.0F, 0.0F, 0.0F)};
    const std::vector<cubey::math::Mat4> node_world{
        cubey::math::translation(0.0F, 2.0F, 0.0F),
        cubey::math::translation(3.0F, 2.0F, 0.0F),
    };

    const std::vector<cubey::math::Mat4> palette =
        cubey::animation::compute_gltf_joint_palette(skin, node_world, 0);

    require(palette.size() == 1, "joint palette should include one matrix per joint");
    const cubey::math::Vec4 transformed = palette[0] * cubey::math::Vec4{1.0F, 0.0F, 0.0F, 1.0F};
    require_close(transformed.x, 3.0F, "joint palette should move into mesh space");
    require_close(transformed.y, 0.0F, "joint palette should remove mesh node transform");
}

void test_gltf_runtime_scene_consumes_load_only_payload() {
    cubey::asset::GltfAsset asset;
    asset.source_path = "runtime-conversion.gltf";
    asset.nodes = {{
        .translation = {1.0F, 2.0F, 3.0F},
        .weights = {0.25F, 0.75F},
    }};
    asset.meshes = {{.weights = {0.5F, 0.5F}}};
    asset.skins = {{
        .joints = {0},
        .inverse_bind_matrices = {cubey::math::translation(1.0F, 0.0F, 0.0F)},
    }};
    asset.animations = {{
        .label = "load-only label",
        .samplers = {linear_sampler({0.0F, 1.0F}, {0.0F, 0.0F, 0.0F, 1.0F, 2.0F, 3.0F}, 3)},
        .channels = {{
            .sampler_index = 0,
            .node_index = 0,
            .target_path = cubey::asset::GltfAnimationTargetPath::Translation,
        }},
        .duration_seconds = 1.0F,
    }};
    asset.materials.emplace_back();
    asset.textures.emplace_back();
    asset.samplers.emplace_back();
    asset.images.push_back({.rgba8 = {1, 2, 3, 4}});
    asset.meshes[0].primitives.push_back({
        .vertices = {{.position = {1.0F, 0.0F, 0.0F}}},
        .indices = {0},
        .morph_targets = {{.position_deltas = {{1.0F, 0.0F, 0.0F}}}},
    });

    const cubey::asset::GltfRuntimeSceneData runtime =
        cubey::asset::consume_gltf_runtime_scene_data(std::move(asset));

    require(runtime.nodes.size() == 1 && runtime.nodes[0].weights.size() == 2,
            "runtime conversion should retain node rest state and default morph weights");
    require(runtime.meshes.size() == 1 && runtime.meshes[0].weights.size() == 2,
            "runtime conversion should retain mesh default morph weights");
    require(runtime.skins.size() == 1 && runtime.skins[0].joints == std::vector<std::uint32_t>{0} &&
                runtime.skins[0].inverse_bind_matrices.size() == 1,
            "runtime conversion should retain skin joints and inverse bind matrices");
    require(runtime.animations.size() == 1 && runtime.animations[0].channels.size() == 1 &&
                runtime.animations[0].duration_seconds == 1.0F,
            "runtime conversion should retain animation sampling data");
    require(asset.source_path.empty() && asset.nodes.empty() && asset.meshes.empty() &&
                asset.skins.empty() && asset.animations.empty() && asset.materials.empty() &&
                asset.textures.empty() && asset.samplers.empty() && asset.images.empty(),
            "runtime conversion should consume the complete load asset");
}
