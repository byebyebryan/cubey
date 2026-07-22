#pragma once

#include <cubey/asset/terrain_height_source.h>
#include <cubey/terrain/terrain_backdrop_density.h>
#include <cubey/terrain/terrain_backdrop_stage.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::terrain {

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

[[nodiscard]] TerrainBackdropProductRequest
terrain_backdrop_v1_product_request(const TerrainBackdropStagePlan& stage,
                                    std::uint32_t render_stride = 3U) noexcept;

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
};

struct TerrainBackdropVertex {
    std::array<float, 3> position{};
    std::array<float, 3> material{};
    std::array<float, 3> normal{};
    std::array<float, 2> surface{};
};

struct TerrainBackdropSectorMesh {
    std::vector<TerrainBackdropVertex> vertices{};
    std::vector<std::uint32_t> indices{};
    TerrainBackdropSectorBounds bounds{};
    float begin_azimuth_radians = 0.0F;
    float end_azimuth_radians = 0.0F;

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

struct TerrainBackdropSourceInfo {
    std::string id{};
    std::uint64_t seed = 0U;
    float base_height_m = 0.0F;
    float relief_scale_m = 1.0F;
    float gradient_step_m = 2.0F;
};

struct TerrainBackdropProduct {
    TerrainBackdropProductRequest request{};
    TerrainBackdropSourceInfo source{};
    std::optional<TerrainBackdropSectorMesh> center{};
    std::vector<TerrainBackdropSectorMesh> sectors{};
    TerrainBackdropProductDiagnostics diagnostics{};
};

struct TerrainBackdropProductInfo {
    TerrainBackdropProductRequest request{};
    TerrainBackdropSourceInfo source{};
    TerrainBackdropProductDiagnostics diagnostics{};
};

[[nodiscard]] TerrainBackdropProductInfo
terrain_backdrop_product_info(const TerrainBackdropProduct& product);

[[nodiscard]] TerrainBackdropProduct
make_terrain_backdrop_product(const TerrainBackdropProductRequest& request,
                              const TerrainHeightSource& source,
                              const TerrainBackdropSurfaceClassifier& classifier);

[[nodiscard]] TerrainBackdropProduct
make_terrain_backdrop_product(const TerrainBackdropProductRequest& request,
                              const TerrainHeightSource& source);

} // namespace cubey::terrain
