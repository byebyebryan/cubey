#include "terrain_product.h"

#include <algorithm>

namespace cubey::projects::terrain {

TerrainRegionProduct make_empty_terrain_region_product(const TerrainRegionConfig& config) {
    const cubey::procedural::Grid2DDesc desc = terrain_region_grid_desc(config);
    return TerrainRegionProduct{
        .config = config,
        .fields = cubey::procedural::FieldSet2D(desc),
        .summary = {},
    };
}

bool terrain_product_has_field(const TerrainRegionProduct& product, std::string_view name) {
    return product.fields.has_field(name);
}

const cubey::procedural::ScalarField2D&
terrain_product_field(const TerrainRegionProduct& product, std::string_view name) {
    return product.fields.field(name);
}

TerrainRegionSummary summarize_terrain_region_product(
    const TerrainRegionConfig& config, const cubey::procedural::FieldSet2D& fields) {
    TerrainRegionSummary summary{};
    cubey::procedural::ProceduralHashBuilder hash;
    hash.append_string(config.recipe_id);
    hash.append_u64(config.seed);
    hash.append_u32(config.generator_revision);
    hash.append_u32(fields.desc().width);
    hash.append_u32(fields.desc().height);

    if (fields.has_field(kTerrainFieldHeightM)) {
        summary.height = fields.summarize_field(kTerrainFieldHeightM);
    }
    if (fields.has_field(kTerrainFieldSlope)) {
        summary.slope = fields.summarize_field(kTerrainFieldSlope);
    }
    if (fields.has_field(kTerrainFieldWetness)) {
        summary.wetness = fields.summarize_field(kTerrainFieldWetness);
    }
    if (const cubey::procedural::ScalarField2D* river = fields.try_field(kTerrainFieldRiverMask)) {
        const auto values = river->values();
        const auto river_samples =
            std::count_if(values.begin(), values.end(), [](float value) { return value > 0.5F; });
        summary.river_coverage =
            static_cast<float>(river_samples) / static_cast<float>(values.size());
    }
    if (const cubey::procedural::ScalarField2D* channel =
            fields.try_field(kTerrainFieldChannelWidth)) {
        summary.max_channel_width_m = channel->summarize().max;
    }

    for (const std::string& field_name : fields.field_names()) {
        const cubey::procedural::ScalarField2D& field = fields.field(field_name);
        hash.append_string(field_name);
        for (const float value : field.values()) {
            hash.append_float32(value);
        }
    }
    summary.content_hash = hash.value();
    return summary;
}

} // namespace cubey::projects::terrain
