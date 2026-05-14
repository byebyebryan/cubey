#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/engine/gltf_scene_importer.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/gpu_runtime.h>

namespace cubey {

void create_default_textures(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                             GltfSceneImportResources& resources);

void create_material_resources(Engine& engine, const vulkan::Device& device,
                               vulkan::GpuRuntime& gpu, GltfSceneImportResources& resources,
                               GltfSceneImportResult& result, const asset::GltfAsset& asset,
                               const GltfSceneImportConfig& config);

void create_deformation_resources(const vulkan::Device& device, vulkan::GpuRuntime& gpu,
                                  GltfSceneImportResources& resources,
                                  const asset::GltfAsset& asset,
                                  const GltfSceneImportConfig& config);

} // namespace cubey
