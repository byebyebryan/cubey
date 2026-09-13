#pragma once

#include <cubey/asset/gltf_asset.h>

#include <cstdint>
#include <vector>

namespace cubey::asset {

// The semantic state needed after a glTF asset has been prepared and made
// resident. It intentionally excludes loader and upload payloads such as
// images, material definitions, static geometry, and morph deltas.
struct GltfRuntimeNode {
    math::Vec3 translation{0.0F, 0.0F, 0.0F};
    math::Quat rotation{1.0F, 0.0F, 0.0F, 0.0F};
    math::Vec3 scale{1.0F, 1.0F, 1.0F};
    std::vector<float> weights{};
};

struct GltfRuntimeMesh {
    std::vector<float> weights{};
};

struct GltfRuntimeSkin {
    std::vector<std::uint32_t> joints{};
    std::vector<math::Mat4> inverse_bind_matrices{};
};

struct GltfRuntimeAnimation {
    std::vector<GltfAnimationSampler> samplers{};
    std::vector<GltfAnimationChannel> channels{};
    float duration_seconds = 0.0F;
};

struct GltfRuntimeSceneData {
    std::vector<GltfRuntimeNode> nodes{};
    std::vector<GltfRuntimeMesh> meshes{};
    std::vector<GltfRuntimeSkin> skins{};
    std::vector<GltfRuntimeAnimation> animations{};
};

// Moves the complete asset through the activation boundary, retaining only
// semantic animation and deformation state. The source asset is reset as a
// unit before return rather than being retained with selected fields cleared.
[[nodiscard]] GltfRuntimeSceneData consume_gltf_runtime_scene_data(GltfAsset&& asset);

} // namespace cubey::asset
