#include <cubey/asset/gltf_runtime_scene.h>

#include <utility>

namespace cubey::asset {

GltfRuntimeSceneData consume_gltf_runtime_scene_data(GltfAsset&& asset) {
    GltfRuntimeSceneData runtime;
    runtime.nodes.reserve(asset.nodes.size());
    for (GltfNode& node : asset.nodes) {
        runtime.nodes.push_back({
            .translation = node.translation,
            .rotation = node.rotation,
            .scale = node.scale,
            .weights = std::move(node.weights),
        });
    }

    runtime.meshes.reserve(asset.meshes.size());
    for (GltfMesh& mesh : asset.meshes) {
        runtime.meshes.push_back({.weights = std::move(mesh.weights)});
    }

    runtime.skins.reserve(asset.skins.size());
    for (GltfSkin& skin : asset.skins) {
        runtime.skins.push_back({
            .joints = std::move(skin.joints),
            .inverse_bind_matrices = std::move(skin.inverse_bind_matrices),
        });
    }

    runtime.animations.reserve(asset.animations.size());
    for (GltfAnimation& animation : asset.animations) {
        runtime.animations.push_back({
            .samplers = std::move(animation.samplers),
            .channels = std::move(animation.channels),
            .duration_seconds = animation.duration_seconds,
        });
    }

    asset = {};
    return runtime;
}

} // namespace cubey::asset
