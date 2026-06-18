#pragma once

#include <cubey/procedural/field_set_2d.h>
#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/noise.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cubey::procedural {

struct FieldDomain2D {
    float x_scale = 1.0F;
    float y_scale = 1.0F;
    float x_offset = 0.0F;
    float y_offset = 0.0F;
};

struct NoiseSource2DWarp {
    bool enabled = false;
    std::uint64_t seed_offset = 7001U;
    CoherentDomainWarpConfig coherent{};
};

enum class NoiseSource2DBackend {
    LegacyFbm,
    LegacyRidgedFbm,
    CoherentNoise,
};

enum class NoiseSource2DOutput {
    Signed,
    Unit,
};

struct NoiseSource2D {
    NoiseSource2DBackend backend = NoiseSource2DBackend::LegacyFbm;
    NoiseSource2DOutput output = NoiseSource2DOutput::Signed;
    std::uint64_t seed = 0;
    Fbm2DConfig legacy_fbm{};
    CoherentNoiseConfig coherent{};
    FieldDomain2D domain{};
    NoiseSource2DWarp warp{};
};

enum class SourceRecipeBlendMode2D {
    Add,
    Multiply,
    Min,
    Max,
    Blend,
};

struct SourceRecipeLayer2D {
    std::string name{};
    bool enabled = true;
    NoiseSource2D source{};
    float weight = 1.0F;
    SourceRecipeBlendMode2D blend_mode = SourceRecipeBlendMode2D::Add;
    bool use_first_layer_as_mask = false;
};

struct SourceRecipe2D {
    std::string name{};
    std::vector<SourceRecipeLayer2D> layers{};
    bool normalize_output_to_unit = false;
};

struct SourceRecipe2DResult {
    ScalarField2D output{};
    FieldSet2D debug_fields{};
    ScalarFieldStats output_stats{};
};

[[nodiscard]] float sample_noise_source_2d(float x, float y, const NoiseSource2D& config);
[[nodiscard]] ScalarField2D sample_noise_source_field_2d(Grid2DDesc desc,
                                                         const NoiseSource2D& config);
[[nodiscard]] SourceRecipe2DResult sample_source_recipe_2d(Grid2DDesc desc,
                                                           const SourceRecipe2D& recipe);

} // namespace cubey::procedural
