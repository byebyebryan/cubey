#pragma once

#include <cubey/asset/gltf_asset.h>
#include <cubey/render/pbr.h>

#include <cstdint>
#include <vector>

namespace cubey::projects::gltf_viewer {

struct GltfViewerLoadingCageMesh {
    std::vector<cubey::render::PbrVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    cubey::asset::GltfBounds3D render_bounds{};
    float thickness = 0.0F;
};

// Builds a portable indexed triangle cage from the authored scene bounds. The
// cage is twelve thin rectangular prisms, one for each axis-aligned box edge;
// it deliberately does not depend on line rasterization support.
[[nodiscard]] GltfViewerLoadingCageMesh
make_gltf_viewer_loading_cage(cubey::asset::GltfBounds3D bounds);

} // namespace cubey::projects::gltf_viewer
