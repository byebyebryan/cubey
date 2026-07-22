#pragma once

#include <cubey/asset/terrain_height_source.h>
#include <cubey/render/terrain_backdrop_density.h>
#include <cubey/render/mesh.h>
#include <cubey/render/primitive_mesh.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cubey::render {

using cubey::asset::TerrainHeightSource;
using cubey::asset::TerrainHeightSourceMetadata;

enum class TerrainBackdropCenterMode : std::uint8_t {
    Cutout,
    Continuous,
};

enum class TerrainBackdropCenterSampling : std::uint8_t {
    SplitLinearLog,
    Uniform,
    SeamMatched,
};

struct TerrainBackdropDensityProfile {
    std::uint32_t angular_intervals = 0U;
    std::uint32_t center_radial_intervals = 0U;
    std::uint32_t hidden_radial_intervals = 0U;
    std::uint32_t visible_radial_intervals = 0U;
    std::uint32_t sector_count = 0U;
};

[[nodiscard]] TerrainBackdropDensityProfile
terrain_backdrop_density_profile(TerrainBackdropMeshDensity density) noexcept;

struct TerrainBackdropProductRequest {
    cubey::math::Vec2 source_focus_xz{0.0F, 0.0F};
    TerrainBackdropMeshDensity density = TerrainBackdropMeshDensity::High;
    TerrainBackdropCenterMode center_mode = TerrainBackdropCenterMode::Cutout;
    TerrainBackdropCenterSampling center_sampling = TerrainBackdropCenterSampling::SplitLinearLog;
    std::uint32_t render_stride = 0U;
    float consumer_radius_m = 300.0F;
    float visible_inner_radius_m = 3'200.0F;
    float outer_radius_m = 16'384.0F;
    float vertical_scale = 1.0F;
    float vertical_offset_m = 0.0F;
};

struct TerrainBackdropSurfaceQuery {
    cubey::math::Vec2 source_xz{0.0F, 0.0F};
    float normalized_height = 0.0F;
    float slope = 0.0F;
    float normal_y = 1.0F;
    float concavity_m = 0.0F;
    float relief_scale_m = 0.0F;
};

struct TerrainBackdropSurfaceChannels {
    float rock = 0.0F;
    float snow = 0.0F;
    float ambient_visibility = 1.0F;
    float vegetation = 0.0F;
    float moisture = 0.0F;
};

class TerrainBackdropSurfaceClassifier {
  public:
    virtual ~TerrainBackdropSurfaceClassifier() = default;

    [[nodiscard]] virtual TerrainBackdropSurfaceChannels
    classify(const TerrainBackdropSurfaceQuery& query) const = 0;
};

class TerrainBackdropMineralSurfaceClassifier final : public TerrainBackdropSurfaceClassifier {
  public:
    [[nodiscard]] TerrainBackdropSurfaceChannels
    classify(const TerrainBackdropSurfaceQuery& query) const override;
};

struct TerrainBackdropSectorBounds {
    cubey::math::Vec3 minimum{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 maximum{0.0F, 0.0F, 0.0F};
    cubey::math::Vec3 center{0.0F, 0.0F, 0.0F};
    float radius_m = 0.0F;
};

struct TerrainBackdropSectorMesh {
    std::vector<cubey::render::VertexPositionColorNormalUv> vertices{};
    std::vector<std::uint32_t> indices{};
    std::vector<std::uint32_t> render_indices{};
    TerrainBackdropSectorBounds bounds{};
    float begin_azimuth_radians = 0.0F;
    float end_azimuth_radians = 0.0F;

    [[nodiscard]] cubey::render::MeshConfig mesh_config() const;
    [[nodiscard]] std::uint32_t triangle_count() const noexcept;
};

struct TerrainBackdropProductDiagnostics {
    TerrainBackdropDensityProfile density{};
    std::uint64_t source_sample_count = 0U;
    std::uint64_t center_vertex_count = 0U;
    std::uint64_t center_index_count = 0U;
    std::uint64_t center_triangle_count = 0U;
    std::uint64_t center_render_triangle_count = 0U;
    std::uint64_t visible_vertex_count = 0U;
    std::uint64_t visible_index_count = 0U;
    std::uint64_t visible_triangle_count = 0U;
    std::uint64_t render_triangle_count = 0U;
    float minimum_height_m = 0.0F;
    float maximum_height_m = 0.0F;
    float maximum_sector_boundary_delta_m = 0.0F;
    std::uint64_t content_hash = 0U;
    std::uint64_t geometry_hash = 0U;
    float mean_rock = 0.0F;
    float mean_snow = 0.0F;
    float mean_vegetation = 0.0F;
    float mean_moisture = 0.0F;
};

struct TerrainBackdropProduct {
    TerrainBackdropProductRequest request{};
    TerrainHeightSourceMetadata source{};
    std::optional<TerrainBackdropSectorMesh> center{};
    std::vector<TerrainBackdropSectorMesh> sectors{};
    TerrainBackdropProductDiagnostics diagnostics{};
};

[[nodiscard]] TerrainBackdropProduct
make_terrain_backdrop_product(const TerrainBackdropProductRequest& request,
                              const TerrainHeightSource& source,
                              const TerrainBackdropSurfaceClassifier& classifier);

[[nodiscard]] TerrainBackdropProduct
make_terrain_backdrop_product(const TerrainBackdropProductRequest& request,
                              const TerrainHeightSource& source);

} // namespace cubey::render
