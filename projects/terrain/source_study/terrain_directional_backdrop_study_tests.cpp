#include "terrain_directional_backdrop_study.h"

#include "terrain_backdrop_product.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

template <typename Function> void require_throws(Function&& function, std::string_view message) {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(std::string(message));
}

class DirectionalStageSource final : public cubey::projects::terrain::TerrainHeightSource {
  public:
    [[nodiscard]] cubey::projects::terrain::TerrainHeightSourceMetadata
    metadata() const noexcept override {
        return {.id = "directional-stage-test", .seed = 11U, .relief_scale_m = 2'000.0F};
    }

    [[nodiscard]] float
    sample_height(const cubey::projects::terrain::TerrainQuery& query) const override {
        const float broad_rise = std::clamp((query.world_xz.x - 2'000.0F) / 8'000.0F, 0.0F, 1.0F);
        return 120.0F + 1'500.0F * broad_rise;
    }
};

void require_same_vertex(const cubey::render::VertexPositionColorNormal& lhs,
                         const cubey::render::VertexPositionColorNormal& rhs,
                         std::string_view message) {
    require(lhs.position == rhs.position && lhs.color == rhs.color && lhs.normal == rhs.normal,
            message);
}

void require_same_baked_mesh(const cubey::projects::terrain::TerrainBackdropSectorMesh& lhs,
                             const cubey::projects::terrain::TerrainBackdropSectorMesh& rhs,
                             std::string_view message) {
    require(lhs.vertices.size() == rhs.vertices.size() && lhs.indices == rhs.indices, message);
    require(lhs.begin_azimuth_radians == rhs.begin_azimuth_radians &&
                lhs.end_azimuth_radians == rhs.end_azimuth_radians,
            message);
    for (std::size_t index = 0U; index < lhs.vertices.size(); ++index) {
        require_same_vertex(lhs.vertices[index], rhs.vertices[index], message);
    }
}

void test_lane_names_round_trip() {
    using namespace cubey::projects::terrain;
    for (const TerrainDirectionalBackdropLane lane : {
             TerrainDirectionalBackdropLane::HardCut,
             TerrainDirectionalBackdropLane::ContinuousCurrent,
             TerrainDirectionalBackdropLane::Placement,
             TerrainDirectionalBackdropLane::Shaped,
             TerrainDirectionalBackdropLane::ExpandedShaped,
             TerrainDirectionalBackdropLane::ExpandedRadial,
             TerrainDirectionalBackdropLane::CachedRadial,
         }) {
        require(terrain_directional_backdrop_lane_from_name(
                    terrain_directional_backdrop_lane_name(lane)) == lane,
                "directional backdrop lane names should round trip");
    }
}

void test_radial_fidelity_contract() {
    using namespace cubey::projects::terrain;
    for (const TerrainRadialFidelity fidelity : {
             TerrainRadialFidelity::Control,
             TerrainRadialFidelity::Source,
             TerrainRadialFidelity::Material,
             TerrainRadialFidelity::Combined,
         }) {
        require(terrain_radial_fidelity_from_name(terrain_radial_fidelity_name(fidelity)) ==
                    fidelity,
                "radial fidelity names should round trip");
    }
    require(!terrain_radial_fidelity_uses_source_detail(TerrainRadialFidelity::Control) &&
                !terrain_radial_fidelity_uses_material_detail(TerrainRadialFidelity::Control) &&
                terrain_radial_fidelity_uses_source_detail(TerrainRadialFidelity::Source) &&
                !terrain_radial_fidelity_uses_material_detail(TerrainRadialFidelity::Source) &&
                !terrain_radial_fidelity_uses_source_detail(TerrainRadialFidelity::Material) &&
                terrain_radial_fidelity_uses_material_detail(TerrainRadialFidelity::Material) &&
                terrain_radial_fidelity_uses_source_detail(TerrainRadialFidelity::Combined) &&
                terrain_radial_fidelity_uses_material_detail(TerrainRadialFidelity::Combined),
            "radial fidelity variants should form the requested two by two ablation");
}

void test_cached_radial_stride_contract() {
    using namespace cubey::projects::terrain;
    require(cached_radial_backdrop_render_stride() == 3U,
            "cached radial should default to production-style stride three");
    require(cached_radial_backdrop_render_stride(1U) == 1U &&
                cached_radial_backdrop_render_stride(2U) == 2U &&
                cached_radial_backdrop_render_stride(3U) == 3U,
            "cached radial should accept the fixed comparison strides");
    require_throws([] { (void)cached_radial_backdrop_render_stride(4U); },
                   "cached radial should reject unreviewed coarser strides");

    constexpr TerrainRadialBackdropProfile profile = terrain_radial_backdrop_profile();
    require(profile.outer_radius_m == 32'768.0F && profile.visible_inner_radius_m == 6'000.0F &&
                profile.render_stride == 3U && profile.stage.focus_height_m == 500.0F &&
                profile.stage.orbit_min_radius_m == 100.0F &&
                profile.stage.orbit_default_radius_m == 400.0F &&
                profile.stage.orbit_max_radius_m == 1'000.0F,
            "production radial profile should freeze the accepted study contract");
}

void test_stage_is_grounded_and_camera_clear() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainBackdropStagePlan stage = make_directional_backdrop_stage_plan(source, placement);
    require(stage.mode == TerrainBackdropStageMode::Grounded,
            "directional stage should use grounded ownership");
    require(stage.source_focus_xz.x == 0.0F && stage.source_focus_xz.y == 0.0F,
            "directional stage should preserve the measured source focus");
    require(stage.minimum_camera_clearance_m >= 10.0F,
            "directional stage should keep the tested orbit envelope above terrain");
    require(stage.orbit_min_radius_m == 50.0F && stage.orbit_default_radius_m == 100.0F &&
                stage.orbit_max_radius_m == 250.0F,
            "directional stage should publish the fixed comparison orbit envelope");
    require(std::abs(stage.showcase_yaw_radians - std::numbers::pi_v<float> * 0.5F) < 0.35F,
            "directional stage should face the measured mountain arc");
}

void test_expanded_stage_uses_far_field_scale() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainDirectionalReliefParameters relief =
        expanded_directional_backdrop_relief_parameters(placement);
    require(relief.broad_start_m == 6'000.0F && relief.broad_full_m == 18'000.0F &&
                relief.detail_start_m == 10'000.0F && relief.detail_full_m == 26'000.0F,
            "expanded relief should restore structure and detail over far-field distances");
    require(expanded_backdrop_outer_radius_m() == 32'768.0F,
            "expanded backdrop should provide room beyond the relief transition");
    const TerrainRadialReliefParameters radial =
        expanded_radial_backdrop_relief_parameters(placement);
    require(radial.floor_footprint_m == 6'000.0F && radial.broad_start_m == 1'000.0F &&
                radial.broad_full_m == 24'000.0F && radial.detail_start_m == 5'000.0F &&
                radial.detail_full_m == 30'000.0F && radial.detail_footprint_m == 0.0F,
            "radial relief should use a broad far-field transition band");
    require(radial.focus_xz.x == placement.source_focus_xz.x &&
                radial.focus_xz.y == placement.source_focus_xz.y,
            "radial relief should remain centered on the selected stage focus");

    const TerrainBackdropStagePlan stage =
        make_directional_backdrop_stage_plan(source, placement, 1.0F,
                                             {
                                                 .focus_height_m = 500.0F,
                                                 .orbit_min_radius_m = 100.0F,
                                                 .orbit_default_radius_m = 400.0F,
                                                 .orbit_max_radius_m = 1'000.0F,
                                             });
    require(stage.target_height_m - stage.source_center_height_m >= 500.0F,
            "expanded stage focus should remain at least 500 m above the valley floor");
    require(stage.orbit_min_radius_m == 100.0F && stage.orbit_default_radius_m == 400.0F &&
                stage.orbit_max_radius_m == 1'000.0F,
            "expanded stage should allow a one-kilometer orbit");
    require(stage.minimum_camera_clearance_m >= 10.0F,
            "expanded orbit envelope should remain above terrain");
}

void test_radial_fidelity_source_parameters_preserve_macro_boundary() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainRadialReliefParameters control =
        radial_fidelity_backdrop_relief_parameters(placement, TerrainRadialFidelity::Control);
    const TerrainRadialReliefParameters material =
        radial_fidelity_backdrop_relief_parameters(placement, TerrainRadialFidelity::Material);
    const TerrainRadialReliefParameters candidate =
        radial_fidelity_backdrop_relief_parameters(placement, TerrainRadialFidelity::Combined);
    require(control.structure_footprint_m == 2'500.0F && control.detail_footprint_m == 0.0F &&
                control.detail_full_m == 30'000.0F,
            "control should retain radial-v1 source parameters");
    require(material.structure_footprint_m == control.structure_footprint_m &&
                material.detail_footprint_m == control.detail_footprint_m &&
                material.detail_full_m == control.detail_full_m,
            "material-only should retain the control source parameters");
    require(candidate.floor_footprint_m == 6'000.0F && candidate.floor_relief_fraction == 0.08F &&
                candidate.structure_footprint_m == 900.0F &&
                candidate.detail_footprint_m == 180.0F && candidate.broad_start_m == 1'000.0F &&
                candidate.broad_full_m == 24'000.0F && candidate.detail_start_m == 5'000.0F &&
                candidate.detail_full_m == 24'000.0F,
            "source candidate should change only the reviewed coherent scale separation");
}

void test_cached_radial_stride_only_changes_render_topology() {
    using namespace cubey::projects::terrain;
    const DirectionalStageSource source;
    const TerrainDirectionalPlacementPlan placement = evaluate_terrain_directional_placement(
        source, directional_backdrop_placement_request(), {0.0F, 0.0F});
    const TerrainRadialReliefSource radial_source(
        source, expanded_radial_backdrop_relief_parameters(placement));
    const TerrainBackdropStagePlan stage =
        make_directional_backdrop_stage_plan(radial_source, placement, 1.0F,
                                             {
                                                 .focus_height_m = 500.0F,
                                                 .orbit_min_radius_m = 100.0F,
                                                 .orbit_default_radius_m = 400.0F,
                                                 .orbit_max_radius_m = 1'000.0F,
                                             });
    TerrainBackdropProductRequest request{
        .source_focus_xz = stage.source_focus_xz,
        .density = TerrainBackdropMeshDensity::Low,
        .center_mode = TerrainBackdropCenterMode::Continuous,
        .render_stride = 1U,
        .consumer_radius_m = stage.stage_radius_m,
        .visible_inner_radius_m = 6'000.0F,
        .outer_radius_m = expanded_backdrop_outer_radius_m(),
        .vertical_offset_m = stage.terrain_vertical_offset_m,
    };
    const TerrainBackdropProduct full = make_terrain_backdrop_product(request, radial_source);
    request.render_stride = cached_radial_backdrop_render_stride(2U);
    const TerrainBackdropProduct stride_two = make_terrain_backdrop_product(request, radial_source);
    request.render_stride = cached_radial_backdrop_render_stride(3U);
    const TerrainBackdropProduct stride_three =
        make_terrain_backdrop_product(request, radial_source);

    require(full.source.id == stride_two.source.id && full.source.id == stride_three.source.id &&
                full.source.seed == stride_two.source.seed &&
                full.source.seed == stride_three.source.seed,
            "cached radial strides should preserve source metadata");
    require(full.diagnostics.source_sample_count == stride_two.diagnostics.source_sample_count &&
                full.diagnostics.source_sample_count ==
                    stride_three.diagnostics.source_sample_count,
            "cached radial strides should sample the same source field");
    require(full.diagnostics.visible_vertex_count == stride_two.diagnostics.visible_vertex_count &&
                full.diagnostics.visible_vertex_count ==
                    stride_three.diagnostics.visible_vertex_count,
            "cached radial strides should retain the full baked vertex product");
    require(full.diagnostics.maximum_sector_boundary_delta_m == 0.0F &&
                stride_two.diagnostics.maximum_sector_boundary_delta_m == 0.0F &&
                stride_three.diagnostics.maximum_sector_boundary_delta_m == 0.0F,
            "cached radial strides should preserve exact sector boundaries");
    require(full.diagnostics.render_triangle_count > stride_two.diagnostics.render_triangle_count &&
                stride_two.diagnostics.render_triangle_count >
                    stride_three.diagnostics.render_triangle_count,
            "cached radial strides should progressively reduce submitted topology");
    require(full.center.has_value() && stride_two.center.has_value() &&
                stride_three.center.has_value(),
            "cached radial comparison should preserve the continuous center");
    require_same_baked_mesh(full.center.value(), stride_two.center.value(),
                            "stride two should preserve the baked center mesh");
    require_same_baked_mesh(full.center.value(), stride_three.center.value(),
                            "stride three should preserve the baked center mesh");
    require(full.sectors.size() == stride_two.sectors.size() &&
                full.sectors.size() == stride_three.sectors.size(),
            "cached radial strides should preserve the sector partition");
    for (std::size_t index = 0U; index < full.sectors.size(); ++index) {
        require_same_baked_mesh(full.sectors[index], stride_two.sectors[index],
                                "stride two should preserve every baked sector");
        require_same_baked_mesh(full.sectors[index], stride_three.sectors[index],
                                "stride three should preserve every baked sector");
    }
}

} // namespace

int main() {
    try {
        test_lane_names_round_trip();
        test_radial_fidelity_contract();
        test_cached_radial_stride_contract();
        test_stage_is_grounded_and_camera_clear();
        test_expanded_stage_uses_far_field_scale();
        test_radial_fidelity_source_parameters_preserve_macro_boundary();
        test_cached_radial_stride_only_changes_render_topology();
        std::cout << "terrain_directional_backdrop_study_tests: ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "terrain_directional_backdrop_study_tests: " << error.what() << '\n';
        return 1;
    }
}
