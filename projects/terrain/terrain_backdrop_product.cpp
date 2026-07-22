#include "terrain_backdrop_product.h"

#include <algorithm>
#include <optional>
#include <stdexcept>

namespace cubey::projects::terrain {
namespace {

class ProjectSurfaceClassifier final : public TerrainBackdropSurfaceClassifier {
  public:
    ProjectSurfaceClassifier(TerrainSurfaceModel model, const TerrainRasterClimateSource* climate)
        : model_(model), climate_(climate) {
        if (model_ == TerrainSurfaceModel::ClimateTransition && climate_ == nullptr) {
            throw std::runtime_error("climate surface model requires a climate source");
        }
    }

    [[nodiscard]] TerrainBackdropSurfaceChannels
    classify(const TerrainBackdropSurfaceQuery& query) const override {
        std::optional<TerrainClimateSample> climate;
        if (climate_ != nullptr) {
            climate = climate_->sample(query.source_xz);
            include_climate(climate.value());
        }
        const TerrainSurfaceWeights weights = terrain_surface_weights(
            model_,
            {
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

TerrainBackdropProduct make_project_terrain_backdrop_product(
    const TerrainBackdropProductRequest& request, const TerrainHeightSource& source,
    TerrainSurfaceModel surface_model,
    const TerrainRasterClimateSource* climate_source,
    TerrainBackdropClimateDiagnostics* climate_diagnostics) {
    ProjectSurfaceClassifier classifier(surface_model, climate_source);
    TerrainBackdropProduct result =
        cubey::render::make_terrain_backdrop_product(request, source, classifier);
    if (climate_diagnostics != nullptr) {
        *climate_diagnostics = classifier.diagnostics();
    }
    return result;
}

} // namespace cubey::projects::terrain
