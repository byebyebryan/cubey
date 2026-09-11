#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

namespace cubey {

void prepare_gltf_materials(GltfPreparedScene& prepared, const asset::GltfAsset& asset,
                            const GltfSceneImportConfig& config,
                            GltfSceneImportCapabilities capabilities);

void prepare_gltf_deformation_primitives(GltfPreparedScene& prepared,
                                         const asset::GltfAsset& asset);

void rebuild_gltf_deformation_frame_meshes(GltfSceneImportResources& resources,
                                           const GltfSceneImportConfig& config);

} // namespace cubey
