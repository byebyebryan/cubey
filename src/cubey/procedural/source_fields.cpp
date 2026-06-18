#include <cubey/procedural/source_fields.h>

#include <cubey/procedural/operators.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::procedural {
namespace {

[[nodiscard]] std::int32_t coherent_seed(std::uint64_t seed) {
    return static_cast<std::int32_t>(seed & 0x7fff'ffffULL);
}

[[nodiscard]] float to_output_range(float value, NoiseSource2DBackend backend,
                                    NoiseSource2DOutput output) {
    if (output == NoiseSource2DOutput::Signed || backend == NoiseSource2DBackend::LegacyRidgedFbm) {
        return value;
    }
    return (value * 0.5F) + 0.5F;
}

[[nodiscard]] bool contains_name(const std::vector<std::string>& names, std::string_view name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}

void validate_source_recipe(const SourceRecipe2D& recipe) {
    if (recipe.layers.empty()) {
        throw std::runtime_error("procedural source recipe must contain at least one layer");
    }

    std::vector<std::string> names;
    names.reserve(recipe.layers.size());
    std::size_t enabled_count = 0;
    for (const SourceRecipeLayer2D& layer : recipe.layers) {
        if (layer.name.empty()) {
            throw std::runtime_error("procedural source recipe layer names must be non-empty");
        }
        if (layer.name == "output") {
            throw std::runtime_error("procedural source recipe layer name is reserved");
        }
        if (contains_name(names, layer.name)) {
            throw std::runtime_error("procedural source recipe layer names must be unique");
        }
        if (!std::isfinite(layer.weight)) {
            throw std::runtime_error("procedural source recipe layer weights must be finite");
        }
        names.push_back(layer.name);
        if (layer.enabled) {
            ++enabled_count;
        }
    }
    if (enabled_count == 0U) {
        throw std::runtime_error("procedural source recipe must contain at least one enabled layer");
    }
}

[[nodiscard]] float blend_recipe_sample(float current, float layer_value, float strength,
                                        SourceRecipeBlendMode2D mode) {
    switch (mode) {
    case SourceRecipeBlendMode2D::Add:
        return current + (layer_value * strength);
    case SourceRecipeBlendMode2D::Multiply:
        return current * lerp(1.0F, layer_value, saturate(strength));
    case SourceRecipeBlendMode2D::Min:
        return lerp(current, std::min(current, layer_value), saturate(strength));
    case SourceRecipeBlendMode2D::Max:
        return lerp(current, std::max(current, layer_value), saturate(strength));
    case SourceRecipeBlendMode2D::Blend:
        return lerp(current, layer_value, saturate(strength));
    }
    return current;
}

} // namespace

float sample_noise_source_2d(float x, float y, const NoiseSource2D& config) {
    float sx = (x * config.domain.x_scale) + config.domain.x_offset;
    float sy = (y * config.domain.y_scale) + config.domain.y_offset;

    if (config.warp.enabled) {
        CoherentDomainWarpConfig warp = config.warp.coherent;
        warp.seed = coherent_seed(config.seed + config.warp.seed_offset);
        const CoherentWarp2D warped = domain_warp_2d(sx, sy, warp);
        sx = warped.x;
        sy = warped.y;
    }

    float value = 0.0F;
    switch (config.backend) {
    case NoiseSource2DBackend::LegacyFbm:
        value = fbm_2d(sx, sy, config.seed, config.legacy_fbm);
        break;
    case NoiseSource2DBackend::LegacyRidgedFbm:
        value = ridged_fbm_2d(sx, sy, config.seed, config.legacy_fbm);
        break;
    case NoiseSource2DBackend::CoherentNoise: {
        CoherentNoiseConfig coherent = config.coherent;
        coherent.seed = coherent_seed(config.seed);
        value = sample_coherent_noise_2d(sx, sy, coherent);
        break;
    }
    }

    return to_output_range(value, config.backend, config.output);
}

ScalarField2D sample_noise_source_field_2d(Grid2DDesc desc, const NoiseSource2D& config) {
    ScalarField2D result(desc, 0.0F);
    for (std::uint32_t y = 0; y < desc.height; ++y) {
        for (std::uint32_t x = 0; x < desc.width; ++x) {
            result.at(x, y) =
                sample_noise_source_2d(grid_sample_x(desc, x), grid_sample_y(desc, y), config);
        }
    }
    return result;
}

SourceRecipe2DResult sample_source_recipe_2d(Grid2DDesc desc, const SourceRecipe2D& recipe) {
    validate_source_recipe(recipe);

    ScalarField2D output(desc, 0.0F);
    ScalarField2D first_layer(desc, 0.0F);
    FieldSet2D debug_fields(desc);
    bool initialized = false;

    for (const SourceRecipeLayer2D& layer : recipe.layers) {
        if (!layer.enabled) {
            continue;
        }

        ScalarField2D layer_field = sample_noise_source_field_2d(desc, layer.source);
        debug_fields.add_field(layer.name, layer_field);
        const std::span<const float> layer_values = layer_field.values();

        if (!initialized) {
            output = ScalarField2D(desc, 0.0F);
            std::span<float> output_values = output.values();
            for (std::size_t index = 0; index < output_values.size(); ++index) {
                output_values[index] = layer_values[index] * layer.weight;
            }
            first_layer = layer_field;
            initialized = true;
            continue;
        }

        const std::span<const float> first_values = first_layer.values();
        std::span<float> output_values = output.values();
        for (std::size_t index = 0; index < output_values.size(); ++index) {
            const float mask = layer.use_first_layer_as_mask ? saturate(first_values[index]) : 1.0F;
            const float strength = layer.weight * mask;
            output_values[index] = blend_recipe_sample(output_values[index], layer_values[index],
                                                       strength, layer.blend_mode);
        }
    }

    if (recipe.normalize_output_to_unit) {
        normalize_to_unit(output);
    }

    debug_fields.add_field("output", output);
    return SourceRecipe2DResult{
        .output = output,
        .debug_fields = debug_fields,
        .output_stats = output.summarize(),
    };
}

} // namespace cubey::procedural
