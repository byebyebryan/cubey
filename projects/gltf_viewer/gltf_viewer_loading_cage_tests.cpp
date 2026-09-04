#include "gltf_viewer_loading_cage.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_finite(float value, const char* message) {
    require(std::isfinite(value), message);
}

void test_loading_cage_has_twelve_prism_edges() {
    const cubey::projects::gltf_viewer::GltfViewerLoadingCageMesh mesh =
        cubey::projects::gltf_viewer::make_gltf_viewer_loading_cage({
            .center = {10.0F, -2.0F, 4.0F},
            .half_extent = {2.0F, 3.0F, 5.0F},
        });

    constexpr std::size_t kEdgeCount = 12U;
    constexpr std::size_t kVerticesPerPrism = 24U;
    constexpr std::size_t kIndicesPerPrism = 36U;
    require(mesh.vertices.size() == kEdgeCount * kVerticesPerPrism,
            "loading cage should contain one six-face prism per box edge");
    require(mesh.indices.size() == kEdgeCount * kIndicesPerPrism,
            "loading cage should contain twelve triangles per box edge");

    for (const cubey::render::PbrVertex& vertex : mesh.vertices) {
        for (const float value :
             {vertex.position.x, vertex.position.y, vertex.position.z, vertex.normal.x,
              vertex.normal.y, vertex.normal.z, vertex.tangent.x, vertex.tangent.y,
              vertex.tangent.z, vertex.tangent.w, vertex.uv0.x, vertex.uv0.y}) {
            require_finite(value, "loading cage vertex data should be finite");
        }
    }
    for (std::size_t prism = 0; prism < kEdgeCount; ++prism) {
        const std::uint32_t first_vertex = static_cast<std::uint32_t>(prism * kVerticesPerPrism);
        const std::uint32_t last_vertex = first_vertex + kVerticesPerPrism;
        const std::size_t first_index = prism * kIndicesPerPrism;
        const std::size_t last_index = first_index + kIndicesPerPrism;
        for (std::size_t index = first_index; index < last_index; ++index) {
            require(mesh.indices[index] >= first_vertex && mesh.indices[index] < last_vertex,
                    "each loading-cage prism should own its indexed topology");
        }
    }
    for (const std::uint32_t index : mesh.indices) {
        require(index < mesh.vertices.size(), "loading cage index should be in vertex range");
    }
    require(mesh.thickness > 0.0F, "loading cage thickness should be positive");
    require(mesh.render_bounds.half_extent.x > 2.0F && mesh.render_bounds.half_extent.y > 3.0F &&
                mesh.render_bounds.half_extent.z > 5.0F,
            "loading cage bounds should include prism thickness");
}

void test_loading_cage_handles_flat_bounds() {
    const cubey::projects::gltf_viewer::GltfViewerLoadingCageMesh mesh =
        cubey::projects::gltf_viewer::make_gltf_viewer_loading_cage({
            .center = {1.0F, 2.0F, 3.0F},
            .half_extent = {0.0F, 2.0F, 0.0F},
        });

    require(mesh.thickness > 0.0F, "flat loading cage should retain a visible thickness");
    require(mesh.render_bounds.half_extent.x > 0.0F && mesh.render_bounds.half_extent.z > 0.0F,
            "flat loading cage should expand degenerate axes");
    for (const cubey::render::PbrVertex& vertex : mesh.vertices) {
        require_finite(vertex.position.x, "flat loading cage x positions should be finite");
        require_finite(vertex.position.y, "flat loading cage y positions should be finite");
        require_finite(vertex.position.z, "flat loading cage z positions should be finite");
    }
}

} // namespace

int main() {
    test_loading_cage_has_twelve_prism_edges();
    test_loading_cage_handles_flat_bounds();
    return 0;
}
