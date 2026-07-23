#include "terrain_product_adapter.h"

#include <cubey/procedural/artifact_metadata.h>
#include <cubey/terrain/terrain_backdrop_product_cache.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::terrain {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::array<std::uint8_t, 8U> kClimateDiagnosticsMagic{
    'C', 'U', 'B', 'E', 'Y', 'C', 'D', '1',
};
constexpr std::uint32_t kClimateDiagnosticsVersion = 1U;
constexpr std::size_t kClimateDiagnosticsFloatCount = 14U;
constexpr std::size_t kClimateDiagnosticsPayloadBytes =
    kClimateDiagnosticsMagic.size() + sizeof(std::uint32_t) + sizeof(std::uint64_t) +
    kClimateDiagnosticsFloatCount * sizeof(float);

[[nodiscard]] double elapsed_milliseconds(Clock::time_point started) {
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

void append_diagnostic(std::string& destination, std::string value) {
    if (value.empty()) {
        return;
    }
    if (!destination.empty()) {
        destination += "; ";
    }
    destination += std::move(value);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
    }
}

void append_u64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
        bytes.push_back(static_cast<std::uint8_t>((value >> (byte * 8U)) & 0xffU));
    }
}

void append_float(std::vector<std::uint8_t>& bytes, float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::uint8_t> payload, std::size_t& offset) {
    if (offset > payload.size() || payload.size() - offset < sizeof(std::uint32_t)) {
        throw std::runtime_error("terrain climate diagnostics payload is truncated");
    }
    std::uint32_t value = 0U;
    for (std::uint32_t byte = 0U; byte < 4U; ++byte) {
        value |= static_cast<std::uint32_t>(payload[offset + byte]) << (byte * 8U);
    }
    offset += sizeof(std::uint32_t);
    return value;
}

[[nodiscard]] std::uint64_t read_u64(std::span<const std::uint8_t> payload, std::size_t& offset) {
    if (offset > payload.size() || payload.size() - offset < sizeof(std::uint64_t)) {
        throw std::runtime_error("terrain climate diagnostics payload is truncated");
    }
    std::uint64_t value = 0U;
    for (std::uint32_t byte = 0U; byte < 8U; ++byte) {
        value |= static_cast<std::uint64_t>(payload[offset + byte]) << (byte * 8U);
    }
    offset += sizeof(std::uint64_t);
    return value;
}

[[nodiscard]] float read_float(std::span<const std::uint8_t> payload, std::size_t& offset) {
    return std::bit_cast<float>(read_u32(payload, offset));
}

[[nodiscard]] cubey::procedural::ProceduralArtifactMetadata
product_metadata(const cubey::procedural::ProceduralArtifactRecipe& recipe,
                 std::uint64_t content_hash) {
    return cubey::procedural::make_procedural_artifact_metadata(
        cubey::procedural::make_procedural_artifact_identity(recipe.name, recipe.generator,
                                                             recipe.formula_version, recipe.domain,
                                                             recipe.seed, recipe.space),
        recipe.kind, recipe.format, recipe.extent, content_hash);
}

[[nodiscard]] cubey::terrain::TerrainBackdropProductRecipeContext
product_recipe_context(const TerrainRasterHeightSource& source, TerrainSurfaceModel surface_model,
                       const TerrainRasterClimateSource* climate_source,
                       std::uint64_t placement_parameter_hash) {
    return {
        .source_content_sha256 = source.provenance().elevation_sha256,
        .climate_content_sha256 =
            climate_source != nullptr ? climate_source->metadata().climate_sha256 : std::string{},
        .surface_formula_version = std::string{kTerrainSurfaceModelFormulaVersion},
        .surface_parameter_hash = terrain_surface_model_parameter_hash(surface_model),
        .placement_parameter_hash = placement_parameter_hash,
    };
}

void reject_typed_entry(const std::filesystem::path& path, std::string& diagnostic,
                        std::string message) {
    append_diagnostic(diagnostic, std::move(message));
    std::error_code error;
    std::filesystem::remove(path, error);
    if (error) {
        append_diagnostic(diagnostic, "failed to remove rejected cache entry: " + error.message());
    }
}

class ProjectSurfaceClassifier final : public cubey::terrain::TerrainBackdropSurfaceClassifier {
  public:
    ProjectSurfaceClassifier(TerrainSurfaceModel model, const TerrainRasterClimateSource* climate)
        : model_(model), climate_(climate) {
        if (model_ == TerrainSurfaceModel::ClimateTransition && climate_ == nullptr) {
            throw std::runtime_error("climate surface model requires a climate source");
        }
    }

    [[nodiscard]] cubey::terrain::TerrainBackdropSurfaceChannels
    classify(const cubey::terrain::TerrainBackdropSurfaceQuery& query) const override {
        std::optional<TerrainClimateSample> climate;
        if (climate_ != nullptr) {
            climate = climate_->sample(query.source_xz);
            include_climate(climate.value());
        }
        const TerrainSurfaceWeights weights =
            terrain_surface_weights(model_, {
                                                .normalized_height = query.normalized_height,
                                                .slope = query.slope,
                                                .normal_y = query.normal_y,
                                                .concavity_m = query.concavity_m,
                                                .relief_scale_m = query.relief_scale_m,
                                                .climate = climate,
                                            });
        return {
            .rock = weights.rock,
            .snow = weights.snow,
            .ambient_visibility = weights.ambient_visibility,
            .vegetation = weights.vegetation,
            .moisture = weights.moisture,
        };
    }

    [[nodiscard]] TerrainBackdropClimateDiagnostics diagnostics() const {
        TerrainBackdropClimateDiagnostics result = sums_;
        if (result.sample_count == 0U) {
            return result;
        }
        const float inverse = 1.0F / static_cast<float>(result.sample_count);
        result.mean_temperature_c *= inverse;
        result.mean_temperature_stddev_c *= inverse;
        result.mean_precipitation_annual_mm *= inverse;
        result.mean_precipitation_cv *= inverse;
        result.mean_growing_season_days *= inverse;
        result.mean_thermal_growth *= inverse;
        result.mean_thermal_water_demand_proxy_mm *= inverse;
        result.mean_climate_moisture_ratio *= inverse;
        result.mean_seasonality_factor *= inverse;
        result.mean_effective_moisture *= inverse;
        result.mean_moisture_weight *= inverse;
        result.mean_cover_weight *= inverse;
        result.mean_annual_cold_potential *= inverse;
        result.mean_wet_snow_potential *= inverse;
        return result;
    }

  private:
    void include_climate(const TerrainClimateSample& sample) const {
        const TerrainClimatePotential potential = terrain_climate_potential(sample);
        ++sums_.sample_count;
        sums_.mean_temperature_c += sample.temperature_mean_c;
        sums_.mean_temperature_stddev_c += sample.temperature_stddev_c;
        sums_.mean_precipitation_annual_mm += sample.precipitation_annual_mm;
        sums_.mean_precipitation_cv += sample.precipitation_cv;
        sums_.mean_growing_season_days += potential.growing_season_days;
        sums_.mean_thermal_growth += potential.thermal_growth;
        sums_.mean_thermal_water_demand_proxy_mm += potential.thermal_water_demand_proxy_mm;
        sums_.mean_climate_moisture_ratio += potential.climate_moisture_ratio;
        sums_.mean_seasonality_factor += potential.seasonality_factor;
        sums_.mean_effective_moisture += potential.effective_moisture;
        sums_.mean_moisture_weight += potential.moisture_weight;
        sums_.mean_cover_weight += potential.cover_weight;
        sums_.mean_annual_cold_potential += potential.annual_cold_potential;
        sums_.mean_wet_snow_potential += potential.wet_snow_potential;
    }

    TerrainSurfaceModel model_ = TerrainSurfaceModel::MineralControl;
    const TerrainRasterClimateSource* climate_ = nullptr;
    mutable TerrainBackdropClimateDiagnostics sums_{};
};

} // namespace

cubey::terrain::TerrainBackdropProduct
make_project_terrain_backdrop_product(const cubey::terrain::TerrainBackdropProductRequest& request,
                                      const cubey::asset::TerrainHeightSource& source,
                                      TerrainSurfaceModel surface_model,
                                      const TerrainRasterClimateSource* climate_source,
                                      TerrainBackdropClimateDiagnostics* climate_diagnostics) {
    ProjectSurfaceClassifier classifier(surface_model, climate_source);
    cubey::terrain::TerrainBackdropProduct result =
        cubey::terrain::make_terrain_backdrop_product(request, source, classifier);
    if (climate_diagnostics != nullptr) {
        *climate_diagnostics = classifier.diagnostics();
    }
    return result;
}

std::vector<std::uint8_t>
encode_terrain_backdrop_climate_diagnostics(const TerrainBackdropClimateDiagnostics& diagnostics) {
    const std::array values{
        diagnostics.mean_temperature_c,
        diagnostics.mean_temperature_stddev_c,
        diagnostics.mean_precipitation_annual_mm,
        diagnostics.mean_precipitation_cv,
        diagnostics.mean_growing_season_days,
        diagnostics.mean_thermal_growth,
        diagnostics.mean_thermal_water_demand_proxy_mm,
        diagnostics.mean_climate_moisture_ratio,
        diagnostics.mean_seasonality_factor,
        diagnostics.mean_effective_moisture,
        diagnostics.mean_moisture_weight,
        diagnostics.mean_cover_weight,
        diagnostics.mean_annual_cold_potential,
        diagnostics.mean_wet_snow_potential,
    };
    if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("terrain climate diagnostics must be finite");
    }
    std::vector<std::uint8_t> payload;
    payload.reserve(kClimateDiagnosticsPayloadBytes);
    payload.insert(payload.end(), kClimateDiagnosticsMagic.begin(), kClimateDiagnosticsMagic.end());
    append_u32(payload, kClimateDiagnosticsVersion);
    append_u64(payload, diagnostics.sample_count);
    for (const float value : values) {
        append_float(payload, value);
    }
    return payload;
}

TerrainBackdropClimateDiagnostics
decode_terrain_backdrop_climate_diagnostics(std::span<const std::uint8_t> payload) {
    if (payload.size() != kClimateDiagnosticsPayloadBytes ||
        !std::ranges::equal(payload.first(kClimateDiagnosticsMagic.size()),
                            kClimateDiagnosticsMagic)) {
        throw std::runtime_error("terrain climate diagnostics payload is incompatible");
    }
    std::size_t offset = kClimateDiagnosticsMagic.size();
    if (read_u32(payload, offset) != kClimateDiagnosticsVersion) {
        throw std::runtime_error("terrain climate diagnostics version is incompatible");
    }
    TerrainBackdropClimateDiagnostics diagnostics;
    diagnostics.sample_count = read_u64(payload, offset);
    diagnostics.mean_temperature_c = read_float(payload, offset);
    diagnostics.mean_temperature_stddev_c = read_float(payload, offset);
    diagnostics.mean_precipitation_annual_mm = read_float(payload, offset);
    diagnostics.mean_precipitation_cv = read_float(payload, offset);
    diagnostics.mean_growing_season_days = read_float(payload, offset);
    diagnostics.mean_thermal_growth = read_float(payload, offset);
    diagnostics.mean_thermal_water_demand_proxy_mm = read_float(payload, offset);
    diagnostics.mean_climate_moisture_ratio = read_float(payload, offset);
    diagnostics.mean_seasonality_factor = read_float(payload, offset);
    diagnostics.mean_effective_moisture = read_float(payload, offset);
    diagnostics.mean_moisture_weight = read_float(payload, offset);
    diagnostics.mean_cover_weight = read_float(payload, offset);
    diagnostics.mean_annual_cold_potential = read_float(payload, offset);
    diagnostics.mean_wet_snow_potential = read_float(payload, offset);
    const std::array values{
        diagnostics.mean_temperature_c,
        diagnostics.mean_temperature_stddev_c,
        diagnostics.mean_precipitation_annual_mm,
        diagnostics.mean_precipitation_cv,
        diagnostics.mean_growing_season_days,
        diagnostics.mean_thermal_growth,
        diagnostics.mean_thermal_water_demand_proxy_mm,
        diagnostics.mean_climate_moisture_ratio,
        diagnostics.mean_seasonality_factor,
        diagnostics.mean_effective_moisture,
        diagnostics.mean_moisture_weight,
        diagnostics.mean_cover_weight,
        diagnostics.mean_annual_cold_potential,
        diagnostics.mean_wet_snow_potential,
    };
    if (offset != payload.size() ||
        !std::ranges::all_of(values, [](float value) { return std::isfinite(value); })) {
        throw std::runtime_error("terrain climate diagnostics payload is invalid");
    }
    return diagnostics;
}

PreparedProjectTerrainBackdropProduct prepare_project_terrain_backdrop_product(
    cubey::procedural::ProceduralArtifactCache& cache,
    const cubey::terrain::TerrainBackdropProductRequest& request,
    const TerrainRasterHeightSource& source, TerrainSurfaceModel surface_model,
    const TerrainRasterClimateSource* climate_source, std::uint64_t placement_parameter_hash) {
    if (surface_model == TerrainSurfaceModel::ClimateTransition && climate_source == nullptr) {
        throw std::runtime_error("climate surface model requires a climate source");
    }
    if (climate_source != nullptr) {
        validate_terrain_climate_binding(source, *climate_source);
    }
    const cubey::terrain::TerrainBackdropProductRecipeContext context =
        product_recipe_context(source, surface_model, climate_source, placement_parameter_hash);
    const TerrainHeightSourceMetadata source_metadata = source.metadata();
    const cubey::terrain::TerrainBackdropSourceInfo source_info{
        .id = std::string{source_metadata.id},
        .seed = source_metadata.seed,
        .base_height_m = source_metadata.base_height_m,
        .relief_scale_m = source_metadata.relief_scale_m,
        .gradient_step_m = source_metadata.gradient_step_m,
    };
    const cubey::procedural::ProceduralArtifactRecipe recipe =
        cubey::terrain::terrain_backdrop_product_cache_recipe(request, source_info, context);

    TerrainProductCacheDiagnostics diagnostics;
    const Clock::time_point load_started = Clock::now();
    cubey::procedural::ProceduralArtifactCacheLoadResult loaded = cache.load(recipe);
    diagnostics.load_milliseconds = elapsed_milliseconds(load_started);
    diagnostics.lookup = loaded.outcome;
    diagnostics.path = loaded.path;
    append_diagnostic(diagnostics.diagnostic, std::move(loaded.diagnostic));
    if (loaded.artifact.has_value()) {
        const Clock::time_point decode_started = Clock::now();
        try {
            cubey::terrain::DecodedTerrainBackdropProduct decoded =
                cubey::terrain::decode_terrain_backdrop_product(loaded.artifact->payload);
            TerrainBackdropClimateDiagnostics climate =
                decode_terrain_backdrop_climate_diagnostics(decoded.auxiliary);
            const cubey::procedural::ProceduralArtifactRecipe decoded_recipe =
                cubey::terrain::terrain_backdrop_product_cache_recipe(
                    decoded.product.request, decoded.product.source, context);
            if (cubey::procedural::procedural_artifact_recipe_hash(decoded_recipe) !=
                    cubey::procedural::procedural_artifact_recipe_hash(recipe) ||
                loaded.artifact->metadata.content_hash !=
                    decoded.product.diagnostics.content_hash ||
                (climate_source == nullptr) != (climate.sample_count == 0U)) {
                throw std::runtime_error("cached terrain product does not match its typed recipe");
            }
            diagnostics.decode_milliseconds = elapsed_milliseconds(decode_started);
            diagnostics.source = TerrainProductPreparationSource::Cache;
            return {
                .product = std::move(decoded.product),
                .climate = climate,
                .cache = std::move(diagnostics),
            };
        } catch (const std::exception& error) {
            diagnostics.decode_milliseconds = elapsed_milliseconds(decode_started);
            diagnostics.lookup = cubey::procedural::ProceduralArtifactCacheLoadOutcome::Rejected;
            reject_typed_entry(loaded.path, diagnostics.diagnostic, error.what());
        }
    }

    TerrainBackdropClimateDiagnostics climate;
    const Clock::time_point generation_started = Clock::now();
    cubey::terrain::TerrainBackdropProduct product = make_project_terrain_backdrop_product(
        request, source, surface_model, climate_source, &climate);
    diagnostics.generation_milliseconds = elapsed_milliseconds(generation_started);

    const Clock::time_point encode_started = Clock::now();
    const std::vector<std::uint8_t> climate_payload =
        encode_terrain_backdrop_climate_diagnostics(climate);
    const std::vector<std::uint8_t> product_payload =
        cubey::terrain::encode_terrain_backdrop_product(product, climate_payload);
    diagnostics.encode_milliseconds = elapsed_milliseconds(encode_started);

    const Clock::time_point store_started = Clock::now();
    const cubey::procedural::ProceduralArtifactCacheStoreResult stored = cache.store(
        recipe, product_metadata(recipe, product.diagnostics.content_hash), product_payload);
    diagnostics.store_milliseconds = elapsed_milliseconds(store_started);
    diagnostics.stored = stored.stored;
    diagnostics.path = stored.path;
    append_diagnostic(diagnostics.diagnostic, stored.diagnostic);
    return {
        .product = std::move(product),
        .climate = climate,
        .cache = std::move(diagnostics),
    };
}

} // namespace cubey::projects::terrain
