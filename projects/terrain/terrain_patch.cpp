#include "terrain_patch.h"

#include "upland_mountain_source.h"

#include <cubey/procedural/field_metadata.h>
#include <cubey/procedural/operators.h>

#include <cmath>
#include <stdexcept>
#include <utility>

namespace cubey::projects::terrain {
namespace {

[[nodiscard]] cubey::procedural::ScalarField2D
crop_to_interior(const cubey::procedural::ScalarField2D& expanded,
                 const cubey::procedural::PatchDomain2D& domain) {
    cubey::procedural::ScalarField2D result(domain.interior_grid, 0.0F);
    for (std::uint32_t y = 0; y < domain.interior_grid.height; ++y) {
        for (std::uint32_t x = 0; x < domain.interior_grid.width; ++x) {
            result.at(x, y) = expanded.at(x + domain.border_samples, y + domain.border_samples);
        }
    }
    return result;
}

[[nodiscard]] TerrainPatchSummary summarize_patch(const cubey::procedural::FieldSet2D& fields) {
    TerrainPatchSummary result;
    result.fields.reserve(fields.field_count());
    for (const std::string& name : fields.field_names()) {
        result.fields.push_back({.name = name, .stats = fields.summarize_field(name)});
    }
    result.content_hash = cubey::procedural::field_set_content_hash(fields);
    return result;
}

} // namespace

TerrainPatchRequest default_terrain_patch_request() {
    return {
        .domain =
            {
                .address = {},
                .interior_grid =
                    {
                        .width = kTerrainDefaultGridSize,
                        .height = kTerrainDefaultGridSize,
                        .cell_size = kTerrainDefaultCellSizeM,
                        .origin_x = 0.0F,
                        .origin_y = 0.0F,
                    },
                .border_samples = kTerrainProcessHaloSamples,
                .seed = kTerrainDefaultSeed,
                .space = cubey::procedural::ProceduralDomainSpace::World,
            },
        .recipe_id = std::string(kTerrainRecipeUplandCatchmentV1),
        .generator_revision = kTerrainGeneratorRevisionV1,
    };
}

void validate_terrain_patch_request(const TerrainPatchRequest& request) {
    cubey::procedural::validate_patch_domain(request.domain);
    const cubey::procedural::Grid2DDesc& grid = request.domain.interior_grid;
    if (grid.width < 17U || grid.height < 17U || (grid.width % 2U) == 0U ||
        (grid.height % 2U) == 0U) {
        throw std::runtime_error("terrain patch dimensions must be odd and at least 17");
    }
    if (!std::isfinite(grid.cell_size) || grid.cell_size <= 0.0F) {
        throw std::runtime_error("terrain patch cell size must be finite and positive");
    }
    if (!std::isfinite(grid.origin_x) || !std::isfinite(grid.origin_y)) {
        throw std::runtime_error("terrain patch origin must be finite");
    }
    if (request.domain.border_samples != kTerrainProcessHaloSamples) {
        throw std::runtime_error("terrain v1 requires a 32-sample process halo");
    }
    if (request.domain.space != cubey::procedural::ProceduralDomainSpace::World) {
        throw std::runtime_error("terrain v1 requires a world-space patch domain");
    }
    if (request.recipe_id != kTerrainRecipeUplandCatchmentV1) {
        throw std::runtime_error("terrain v1 recipe must be upland-catchment-v1");
    }
    if (request.generator_revision != kTerrainGeneratorRevisionV1) {
        throw std::runtime_error("terrain v1 generator revision must be 1");
    }
}

TerrainPatchProduct generate_terrain_patch(const TerrainPatchRequest& request) {
    validate_terrain_patch_request(request);
    const cubey::procedural::Grid2DDesc expanded_grid =
        cubey::procedural::patch_sample_grid(request.domain);
    cubey::procedural::FieldSet2D source =
        sample_upland_mountain_fields_v1(expanded_grid, request.domain.seed);
    const cubey::procedural::ScalarField2D& source_height =
        source.field(kTerrainFieldSourceHeightM);
    const cubey::procedural::SlopeCurvature2D derivatives =
        cubey::procedural::compute_slope_curvature(source_height);
    const cubey::procedural::LocalRelief2D relief =
        cubey::procedural::compute_local_relief(source_height, 4U);

    cubey::procedural::FieldSet2D fields(request.domain.interior_grid);
    fields.add_field(std::string(kTerrainFieldSourceHeightM),
                     crop_to_interior(source_height, request.domain));
    fields.add_field(std::string(kTerrainFieldMountainSupport),
                     crop_to_interior(source.field(kTerrainFieldMountainSupport), request.domain));
    fields.add_field(std::string(kTerrainFieldHeightM),
                     crop_to_interior(source_height, request.domain));
    fields.add_field(std::string(kTerrainFieldSlope),
                     crop_to_interior(derivatives.slope, request.domain));
    fields.add_field(std::string(kTerrainFieldCurvature),
                     crop_to_interior(derivatives.curvature, request.domain));
    fields.add_field(std::string(kTerrainFieldLocalReliefM),
                     crop_to_interior(relief.local_span, request.domain));

    TerrainPatchProduct result{
        .request = request,
        .fields = std::move(fields),
        .summary = {},
    };
    result.summary = summarize_patch(result.fields);
    return result;
}

} // namespace cubey::projects::terrain
