#include "pyro_3d_config.h"
#include "pyro_3d_sources.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::size_t count_occurrences(const std::string& haystack, const char* needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = haystack.find(needle, offset)) != std::string::npos) {
        ++count;
        offset += std::string_view{needle}.size();
    }
    return count;
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open " + path.string());
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

[[nodiscard]] float length_squared(std::array<float, 3> value) {
    return (value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]);
}

} // namespace

int main() {
    namespace pyro = cubey::projects::fluid::pyro_3d;

    try {
        const pyro::Pyro3DConfig config;
        constexpr std::size_t kExpectedCellCount =
            std::size_t{128} * std::size_t{128} * std::size_t{128};
        require(config.grid_width == 128, "pyro 3D should default to 128 columns");
        require(config.grid_height == 128, "pyro 3D should default to 128 rows");
        require(config.grid_depth == 128, "pyro 3D should default to 128 slices");
        require(config.mode == pyro::Pyro3DMode::Fire,
                "pyro 3D standalone config should default to fire mode");
        require(config.source_count == 1, "pyro 3D fire should default to one source");
        require(config.source_radius == pyro::kDefaultFireSourceRadius,
                "pyro 3D fire should default to the wider burner radius");
        require(config.source_center_height == pyro::kDefaultFire3DSourceHeight,
                "pyro 3D fire should default to the lower burner height");
        require(config.pressure_iterations == 12,
                "pyro 3D pressure solve should default to a small 3D budget");
        require(config.raymarch_steps == 128, "pyro 3D should default to one step per slice");
        require(config.shadow_grid_width == 64, "pyro 3D should default to a 64-wide shadow grid");
        require(config.shadow_grid_height == 64, "pyro 3D should default to a 64-high shadow grid");
        require(config.shadow_grid_depth == 64, "pyro 3D should default to a 64-deep shadow grid");
        require(config.shadow_steps == 64, "pyro 3D shadow rays should default to 64 steps");
        require(config.shadow_update_interval == 1,
                "pyro 3D should update shadows every frame by default");
        require(config.density_decay_per_second == 0.99F,
                "pyro 3D density decay should match the tuned default");
        require(config.velocity_decay_per_second == 0.99F,
                "pyro 3D velocity decay should match the tuned default");
        require(config.source_velocity_strength == 8.5F,
                "pyro 3D source force should match the tuned default");
        require(config.source_smoke_amount == 16.0F,
                "pyro 3D source soot should match the tuned default");
        require(config.source_heat_amount == 2.8F,
                "pyro 3D source temperature should match the tuned default");
        require(config.source_flame_amount == 4.0F,
                "pyro 3D source fuel should match the tuned default");
        require(config.explosion_interval_seconds == 3.0F,
                "pyro 3D explosion interval should match the tuned default");
        require(config.explosion_duration_seconds == 0.50F,
                "pyro 3D explosion duration should match the tuned default");
        require(config.explosion_boost == 20.0F,
                "pyro 3D explosion boost should match the tuned default");
        require(config.fire_ignition_temperature == 0.22F,
                "pyro 3D ignition should match the tuned default");
        require(config.fire_burn_rate == 4.0F, "pyro 3D burn rate should match the tuned default");
        require(config.fire_heat_output == 2.8F,
                "pyro 3D heat output should match the tuned default");
        require(config.fire_soot_yield == 0.45F,
                "pyro 3D soot yield should match the tuned default");
        require(config.fire_expansion == 1.35F,
                "pyro 3D fire expansion should match the tuned default");
        require(config.fire_flame_cooling == 3.8F,
                "pyro 3D fire flame cooling should match the tuned default");
        require(config.fire_shredding == 3.6F,
                "pyro 3D fire shredding should match the tuned default");
        require(config.fire_turbulence == 0.95F,
                "pyro 3D fire turbulence should match the tuned default");
        require(config.obstacle_center_height == pyro::kDefaultPyro3DObstacleHeight,
                "pyro 3D obstacle height should match the tuned default");
        require(config.obstacle_radius == pyro::kDefaultPyro3DObstacleRadius,
                "pyro 3D obstacle radius should match the tuned default");
        require(config.vorticity_strength == 1.0F,
                "pyro 3D vorticity should match the tuned default");
        require(config.buoyancy_strength == 2.5F,
                "pyro 3D buoyancy should default to a presentation plume force");
        require(config.absorption == 28.0F, "pyro 3D absorption should match the tuned default");
        require(config.emission == 2.0F, "pyro 3D light emission should match the tuned default");
        require(config.shadow_absorption == 50.0F,
                "pyro 3D shadow absorption should match the tuned default");
        require(config.ambient_light == 0.5F,
                "pyro 3D ambient light should match the tuned default");
        require(pyro::kPyro3DRenderPushConstantFloatCount == 32,
                "pyro 3D render push constants should include style controls");
        require(config.render_exposure == 0.42F,
                "pyro 3D fire render exposure should match the tuned default");
        require(config.render_background_lift == 0.42F,
                "pyro 3D backdrop lift should match the tuned default");
        require(config.render_rim_strength == 1.25F,
                "pyro 3D rim strength should match the tuned default");
        require(config.render_scatter_strength == 1.15F,
                "pyro 3D scatter strength should match the tuned default");
        require(config.render_smoke_warmth == 0.18F,
                "pyro 3D smoke warmth should match the tuned default");
        require(config.render_flame_intensity == 1.65F,
                "pyro 3D flame intensity should match the tuned default");
        require(config.render_flame_core_strength == 1.35F,
                "pyro 3D flame core should match the tuned default");
        require(pyro::volume_cell_count(config) == kExpectedCellCount,
                "pyro 3D cell count should multiply all dimensions");
        require(pyro::volume_byte_size(config, 8) == 8U * kExpectedCellCount,
                "pyro 3D generic byte size should use caller-provided cell bytes");
        require(pyro::shadow_volume_cell_count(config) ==
                    std::size_t{64} * std::size_t{64} * std::size_t{64},
                "pyro 3D shadow cell count should use the decoupled shadow grid");
        require(pyro::next_debug_view(pyro::Pyro3DDebugView::Smoke) ==
                    pyro::Pyro3DDebugView::DensitySlice,
                "pyro 3D debug view should cycle from smoke to density slice");
        require(pyro::next_debug_view(pyro::Pyro3DDebugView::DensitySlice) ==
                    pyro::Pyro3DDebugView::Velocity,
                "pyro 3D debug view should cycle from density slice to velocity");
        require(pyro::next_debug_view(pyro::Pyro3DDebugView::Velocity) ==
                    pyro::Pyro3DDebugView::Smoke,
                "pyro 3D debug view should cycle from velocity to smoke");

        const cubey::RunConfig default_run_config;
        const pyro::Pyro3DConfig default_fire_config =
            pyro::pyro_3d_config_from_run_config(default_run_config, pyro::Pyro3DMode::Fire);
        require(default_fire_config.source_count == config.source_count,
                "default run config should preserve fire source count");
        require(default_fire_config.source_radius == config.source_radius,
                "default run config should preserve fire source radius");
        require(default_fire_config.source_center_height == config.source_center_height,
                "default run config should preserve fire source height");
        require(default_fire_config.fire_burn_rate == config.fire_burn_rate,
                "default run config should preserve fire burn rate");
        require(default_fire_config.fire_heat_output == config.fire_heat_output,
                "default run config should preserve fire heat output");
        require(default_fire_config.fire_expansion == config.fire_expansion,
                "default run config should preserve fire expansion");
        require(default_fire_config.fire_turbulence == config.fire_turbulence,
                "default run config should preserve fire turbulence");
        require(default_fire_config.render_exposure == config.render_exposure,
                "default run config should preserve fire render exposure");
        require(default_fire_config.render_smoke_warmth == config.render_smoke_warmth,
                "default run config should preserve fire smoke warmth");
        const pyro::Pyro3DConfig default_explosion_config =
            pyro::pyro_3d_config_from_run_config(default_run_config, pyro::Pyro3DMode::Explosion);
        require(default_explosion_config.source_count == pyro::kDefaultExplosion3DSourceCount,
                "pyro 3D explosion should default to a shell source layout");
        require(default_explosion_config.source_radius == pyro::kDefaultExplosion3DSourceRadius,
                "pyro 3D explosion should default to a compact impulse radius");
        require(default_explosion_config.source_center_height ==
                    pyro::kDefaultExplosion3DSourceHeight,
                "pyro 3D explosion should default to a lower source height");
        require(default_explosion_config.density_decay_per_second == 0.97F,
                "pyro 3D explosion should use the tuned density decay");
        require(default_explosion_config.explosion_duration_seconds == 0.50F,
                "pyro 3D explosion should use the tuned impulse duration");
        require(default_explosion_config.explosion_boost == 20.0F,
                "pyro 3D explosion should use the tuned impulse boost");
        require(default_explosion_config.source_velocity_strength == 8.5F,
                "pyro 3D explosion should keep its impulse force baseline");
        require(default_explosion_config.source_smoke_amount == 8.0F,
                "pyro 3D explosion should keep its smoke baseline");
        require(default_explosion_config.source_heat_amount == 2.6F,
                "pyro 3D explosion should keep its heat baseline");
        require(default_explosion_config.source_flame_amount == 4.5F,
                "pyro 3D explosion should keep its flame baseline");
        require(default_explosion_config.fire_expansion == 1.0F,
                "pyro 3D explosion should keep its expansion baseline");
        require(default_explosion_config.fire_flame_cooling == 3.4F,
                "pyro 3D explosion should keep its cooling baseline");
        require(default_explosion_config.obstacle_center_height ==
                    pyro::kDefaultExplosion3DObstacleHeight,
                "pyro 3D explosion should keep the higher-volume obstacle baseline");
        require(default_explosion_config.obstacle_radius == 0.0F,
                "pyro 3D explosion should disable the obstacle by default");
        require(default_explosion_config.buoyancy_strength == 1.35F,
                "pyro 3D explosion should keep its buoyancy baseline");
        require(default_explosion_config.absorption == 15.0F,
                "pyro 3D explosion should keep its smoke opacity baseline");
        require(default_explosion_config.render_exposure == 0.64F,
                "pyro 3D explosion should default to higher render exposure");
        require(default_explosion_config.render_rim_strength == 1.55F,
                "pyro 3D explosion should default to stronger rim lighting");
        require(default_explosion_config.render_scatter_strength == 1.38F,
                "pyro 3D explosion should default to stronger forward scatter");
        require(default_explosion_config.render_smoke_warmth == 0.24F,
                "pyro 3D explosion should default to cooler smoke");
        require(default_explosion_config.render_flame_intensity == 2.35F,
                "pyro 3D explosion should default to brighter flame");
        require(default_explosion_config.render_flame_core_strength == 2.10F,
                "pyro 3D explosion should default to a brighter flame core");
        cubey::RunConfig explicit_default_radius_config;
        explicit_default_radius_config.pyro.source_radius = pyro::kDefaultPyro3DSourceRadius;
        const pyro::Pyro3DConfig explicit_fire_radius_config = pyro::pyro_3d_config_from_run_config(
            explicit_default_radius_config, pyro::Pyro3DMode::Fire);
        const pyro::Pyro3DConfig explicit_explosion_radius_config =
            pyro::pyro_3d_config_from_run_config(explicit_default_radius_config,
                                                 pyro::Pyro3DMode::Explosion);
        require(explicit_fire_radius_config.source_radius == pyro::kDefaultPyro3DSourceRadius,
                "explicit default-size source radius should override fire mode radius");
        require(explicit_explosion_radius_config.source_radius == pyro::kDefaultPyro3DSourceRadius,
                "explicit default-size source radius should override explosion mode radius");

        cubey::RunConfig explicit_source_height_config;
        explicit_source_height_config.pyro.source_height = 0.21F;
        const pyro::Pyro3DConfig explicit_fire_height_config = pyro::pyro_3d_config_from_run_config(
            explicit_source_height_config, pyro::Pyro3DMode::Fire);
        const pyro::Pyro3DConfig explicit_explosion_height_config =
            pyro::pyro_3d_config_from_run_config(explicit_source_height_config,
                                                 pyro::Pyro3DMode::Explosion);
        require(explicit_fire_height_config.source_center_height == 0.21F,
                "explicit source height should override fire mode height");
        require(explicit_explosion_height_config.source_center_height == 0.21F,
                "explicit source height should override explosion mode height");

        cubey::RunConfig run_config;
        run_config.grid.width = 64;
        run_config.grid.height = 48;
        run_config.grid.depth = 32;
        run_config.pyro.shadow_grid.width = 24;
        run_config.pyro.shadow_grid.height = 20;
        run_config.pyro.shadow_grid.depth = 16;
        run_config.pyro.shadow_steps = 48;
        run_config.pyro.shadow_update_interval = 3;
        run_config.pyro.sources = 6;
        run_config.pyro.source_radius = 0.08F;
        run_config.pyro.source_force = 7.5F;
        run_config.pyro.soot = 6.5F;
        run_config.pyro.temperature = 1.75F;
        run_config.pyro.fuel = 2.5F;
        run_config.pyro.ignition_temperature = 0.31F;
        run_config.pyro.burn_rate = 4.5F;
        run_config.pyro.heat_output = 3.25F;
        run_config.pyro.soot_yield = 0.22F;
        run_config.pyro.expansion = 1.8F;
        run_config.pyro.flame_cooling = 2.75F;
        run_config.pyro.shredding = 3.5F;
        run_config.pyro.turbulence = 0.85F;
        run_config.pyro.obstacle_height = 0.58F;
        run_config.pyro.obstacle_radius = 0.18F;
        run_config.pyro.buoyancy = 1.75F;
        run_config.pyro.explosion_interval_seconds = 2.5F;
        run_config.pyro.explosion_duration_seconds = 0.18F;
        run_config.pyro.explosion_boost = 22.0F;

        const pyro::Pyro3DConfig fire_config =
            pyro::pyro_3d_config_from_run_config(run_config, pyro::Pyro3DMode::Fire);
        require(fire_config.mode == pyro::Pyro3DMode::Fire,
                "pyro 3D config should honor requested fire mode");
        require(fire_config.grid_width == 64, "pyro 3D config should honor run config width");
        require(fire_config.grid_height == 48, "pyro 3D config should honor run config height");
        require(fire_config.grid_depth == 32, "pyro 3D config should honor run config depth");
        require(fire_config.shadow_grid_width == 24,
                "pyro 3D config should honor run config shadow width");
        require(fire_config.shadow_grid_height == 20,
                "pyro 3D config should honor run config shadow height");
        require(fire_config.shadow_grid_depth == 16,
                "pyro 3D config should honor run config shadow depth");
        require(fire_config.shadow_steps == 48,
                "pyro 3D config should honor run config shadow steps");
        require(fire_config.shadow_update_interval == 3,
                "pyro 3D config should honor run config shadow update interval");
        require(fire_config.source_count == 6,
                "pyro 3D config should honor run config source count");
        require(fire_config.source_radius == 0.08F,
                "pyro 3D config should honor run config source radius");
        require(fire_config.source_velocity_strength == 7.5F,
                "pyro 3D config should honor run config source force");
        require(fire_config.source_smoke_amount == 6.5F,
                "pyro 3D config should honor run config soot amount");
        require(fire_config.source_heat_amount == 1.75F,
                "pyro 3D config should honor run config temperature amount");
        require(fire_config.source_flame_amount == 2.5F,
                "pyro 3D config should honor run config fuel amount");
        require(fire_config.explosion_interval_seconds == 2.5F,
                "pyro 3D config should honor run config explosion interval");
        require(fire_config.explosion_duration_seconds == 0.18F,
                "pyro 3D config should honor run config explosion duration");
        require(fire_config.explosion_boost == 22.0F,
                "pyro 3D config should honor run config explosion boost");
        require(fire_config.fire_ignition_temperature == 0.31F,
                "pyro 3D config should honor run config ignition");
        require(fire_config.fire_burn_rate == 4.5F,
                "pyro 3D config should honor run config burn rate");
        require(fire_config.fire_heat_output == 3.25F,
                "pyro 3D config should honor run config heat output");
        require(fire_config.fire_soot_yield == 0.22F,
                "pyro 3D config should honor run config soot yield");
        require(fire_config.fire_expansion == 1.8F,
                "pyro 3D config should honor run config expansion");
        require(fire_config.fire_flame_cooling == 2.75F,
                "pyro 3D config should honor run config flame cooling");
        require(fire_config.fire_shredding == 3.5F,
                "pyro 3D config should honor run config shredding");
        require(fire_config.fire_turbulence == 0.85F,
                "pyro 3D config should honor run config turbulence");
        require(fire_config.obstacle_center_height == 0.58F,
                "pyro 3D config should honor run config obstacle height");
        require(fire_config.obstacle_radius == 0.18F,
                "pyro 3D config should honor run config obstacle radius");
        require(fire_config.buoyancy_strength == 1.75F,
                "pyro 3D config should honor run config buoyancy");

        bool threw_for_too_many_sources = false;
        try {
            cubey::RunConfig invalid_source_config;
            invalid_source_config.pyro.sources = pyro::kMaxPyro3DSourceCount + 1U;
            static_cast<void>(pyro::pyro_3d_config_from_run_config(invalid_source_config,
                                                                   pyro::Pyro3DMode::Fire));
        } catch (const std::runtime_error&) {
            threw_for_too_many_sources = true;
        }
        require(threw_for_too_many_sources,
                "pyro 3D config should reject source counts above the shader policy limit");
        bool threw_for_invalid_source_height = false;
        try {
            cubey::RunConfig invalid_source_config;
            invalid_source_config.pyro.source_height = 1.2F;
            static_cast<void>(pyro::pyro_3d_config_from_run_config(invalid_source_config,
                                                                   pyro::Pyro3DMode::Fire));
        } catch (const std::runtime_error&) {
            threw_for_invalid_source_height = true;
        }
        require(threw_for_invalid_source_height,
                "pyro 3D config should reject source heights outside the volume");
        bool threw_for_invalid_obstacle_height = false;
        try {
            cubey::RunConfig invalid_obstacle_config;
            invalid_obstacle_config.pyro.obstacle_height = 1.2F;
            static_cast<void>(pyro::pyro_3d_config_from_run_config(invalid_obstacle_config,
                                                                   pyro::Pyro3DMode::Fire));
        } catch (const std::runtime_error&) {
            threw_for_invalid_obstacle_height = true;
        }
        require(threw_for_invalid_obstacle_height,
                "pyro 3D config should reject obstacle heights outside the volume");
        bool threw_for_invalid_obstacle_radius = false;
        try {
            cubey::RunConfig invalid_obstacle_config;
            invalid_obstacle_config.pyro.obstacle_radius = 0.6F;
            static_cast<void>(pyro::pyro_3d_config_from_run_config(invalid_obstacle_config,
                                                                   pyro::Pyro3DMode::Fire));
        } catch (const std::runtime_error&) {
            threw_for_invalid_obstacle_radius = true;
        }
        require(threw_for_invalid_obstacle_radius,
                "pyro 3D config should reject obstacle radii outside the volume");

        std::vector<pyro::Pyro3DSourceState> fire_sources =
            pyro::create_pyro_3d_sources(fire_config);
        require(fire_sources.size() == 6, "pyro 3D source state should match configured count");
        require(fire_sources.front().position[1] < 0.12F,
                "pyro 3D fire should originate near the lower burner");
        require(length_squared(fire_sources.front().velocity) > 0.0F,
                "pyro 3D source should carry a velocity direction");
        require(fire_sources.front().material_amount[0] < fire_config.source_smoke_amount,
                "pyro 3D fire should use less soot than configured source soot");
        require(fire_sources.front().material_amount[1] > fire_config.source_heat_amount,
                "pyro 3D fire should boost source temperature");
        require(fire_sources.front().material_amount[2] == 0.0F,
                "pyro 3D fire source should let combustion create visible flame");
        require(fire_sources.front().material_amount[3] > fire_config.source_flame_amount,
                "pyro 3D fire should treat the source fuel amount as fuel");
        const std::vector<pyro::Pyro3DSourceGpu> fire_gpu =
            pyro::pyro_3d_sources_to_gpu(fire_sources, fire_config);
        require(fire_gpu.size() == 6, "pyro 3D GPU source state should match state count");
        require(fire_gpu.front().position_radius[3] == fire_config.source_radius,
                "pyro 3D GPU source should carry configured radius");
        require(fire_gpu.front().velocity_strength[3] == fire_config.source_velocity_strength,
                "pyro 3D GPU source should carry configured force");
        require(pyro::pyro_3d_source_byte_size(fire_config) == sizeof(pyro::Pyro3DSourceGpu) * 6U,
                "pyro 3D active source byte size should match configured count");
        require(pyro::pyro_3d_source_capacity_byte_size() ==
                    sizeof(pyro::Pyro3DSourceGpu) * pyro::kMaxPyro3DSourceCount,
                "pyro 3D source capacity byte size should cover the shader policy limit");

        const std::vector<pyro::Pyro3DSourceGpu> early_fire_gpu =
            pyro::update_pyro_3d_sources(fire_sources, fire_config,
                                         {
                                             .delta_seconds = fire_config.fixed_delta_seconds,
                                             .elapsed_seconds = 0.25,
                                             .frame_index = 1,
                                         });
        const std::vector<pyro::Pyro3DSourceGpu> later_fire_gpu =
            pyro::update_pyro_3d_sources(fire_sources, fire_config,
                                         {
                                             .delta_seconds = fire_config.fixed_delta_seconds,
                                             .elapsed_seconds = 0.75,
                                             .frame_index = 2,
                                         });
        require(early_fire_gpu.front().position_radius != later_fire_gpu.front().position_radius,
                "pyro 3D fire source turbulence should jitter source position/radius over time");
        require(early_fire_gpu.front().material_amount[3] !=
                    later_fire_gpu.front().material_amount[3],
                "pyro 3D fire source turbulence should vary fuel over time");

        const pyro::Pyro3DConfig explosion_config =
            pyro::pyro_3d_config_from_run_config(run_config, pyro::Pyro3DMode::Explosion);
        require(explosion_config.mode == pyro::Pyro3DMode::Explosion,
                "pyro 3D config should honor requested explosion mode");
        std::vector<pyro::Pyro3DSourceState> explosion_sources =
            pyro::create_pyro_3d_sources(explosion_config);
        require(explosion_sources.front().position[1] > 0.09F &&
                    explosion_sources.front().position[1] < 0.13F,
                "pyro 3D explosion should originate near the lower source height");
        require(explosion_sources.front().material_amount[2] > explosion_config.source_flame_amount,
                "pyro 3D explosion should start with a hot flame core");
        const std::vector<pyro::Pyro3DSourceGpu> explosion_active_gpu =
            pyro::update_pyro_3d_sources(explosion_sources, explosion_config,
                                         {
                                             .delta_seconds = explosion_config.fixed_delta_seconds,
                                             .elapsed_seconds = 2.5,
                                             .frame_index = 1,
                                         });
        require(explosion_active_gpu.front().material_amount[0] >
                    explosion_config.source_smoke_amount,
                "pyro 3D explosion should boost smoke during the impulse window");
        require(explosion_active_gpu.front().velocity_strength[3] >
                    explosion_config.source_velocity_strength,
                "pyro 3D explosion should boost force during the impulse window");
        const std::vector<pyro::Pyro3DSourceGpu> explosion_shell_gpu = pyro::update_pyro_3d_sources(
            explosion_sources, explosion_config,
            {
                .delta_seconds = explosion_config.fixed_delta_seconds,
                .elapsed_seconds = 2.5 + explosion_config.explosion_duration_seconds * 0.72,
                .frame_index = 2,
            });
        require(explosion_shell_gpu.front().position_radius[3] >
                    explosion_active_gpu.front().position_radius[3],
                "pyro 3D explosion should expand the source radius across the impulse");
        require(explosion_shell_gpu.front().material_amount[0] >
                    explosion_active_gpu.front().material_amount[0],
                "pyro 3D explosion should shift from flash to smoke shell");
        require(explosion_shell_gpu.front().material_amount[2] <
                    explosion_active_gpu.front().material_amount[2],
                "pyro 3D explosion should fade flame after the initial flash");
        const std::vector<pyro::Pyro3DSourceGpu> explosion_pause_gpu =
            pyro::update_pyro_3d_sources(explosion_sources, explosion_config,
                                         {
                                             .delta_seconds = explosion_config.fixed_delta_seconds,
                                             .elapsed_seconds = 2.75,
                                             .frame_index = 3,
                                         });
        require(explosion_pause_gpu.front().material_amount[0] == 0.0F,
                "pyro 3D explosion should pause smoke between impulses");
        require(explosion_pause_gpu.front().velocity_strength[3] == 0.0F,
                "pyro 3D explosion should pause force between impulses");

        require(pyro::pyro_3d_headless_frame_count(run_config) == 120,
                "pyro 3D headless PNG should default to a settled simulation frame");
        run_config.frames = 8;
        require(pyro::pyro_3d_headless_frame_count(run_config) == 8,
                "pyro 3D headless frame count should honor explicit frames");
        const cubey::FrameTiming fixed_timing = pyro::fixed_pyro_3d_headless_timing(fire_config, 5);
        require(fixed_timing.frame_index == 5,
                "pyro 3D fixed headless timing should preserve frame index");
        require(fixed_timing.delta_seconds == fire_config.fixed_delta_seconds,
                "pyro 3D fixed headless timing should use fixed dt");

        const std::filesystem::path source_dir = CUBEY_PYRO_3D_SOURCE_DIR;
        const std::string commands_source = read_text_file(source_dir / "pyro_3d_commands.cpp");
        const std::string advect_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_advect.comp");
        const std::string advect_correct_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_advect_correct.comp");
        const std::string combustion_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_combust.comp");
        const std::string divergence_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_divergence.comp");
        const std::string pressure_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_pressure.comp");
        const std::string projection_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_projection.comp");
        const std::string raymarch_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_raymarch.frag");
        const std::string shadow_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_shadow.comp");
        const std::string app_source = read_text_file(source_dir / "pyro_3d_app.cpp");
        const std::string gpu_resources_source =
            read_text_file(source_dir / "pyro_3d_gpu_resources.cpp");
        const std::string cmake_source =
            read_text_file(source_dir / ".." / ".." / "CMakeLists.txt");
        require(count_occurrences(commands_source,
                                  "static_cast<float>(static_cast<std::uint32_t>(config.mode))") >=
                    2,
                "pyro 3D commands should pass mode to simulation and render push constants");
        require_contains(advect_shader, "previous_uv",
                         "pyro 3D advection shader should keep backtraced advection");
        require_contains(advect_correct_shader, "limited_density",
                         "pyro 3D correction shader should clamp corrected density");
        require_contains(combustion_shader, "apply_combustion",
                         "pyro 3D combustion shader should run the fire model");
        require_contains(combustion_shader, "spent_flame * 0.085",
                         "pyro 3D combustion shader should convert cooled flame into soot");
        require_contains(combustion_shader, "fire_mode",
                         "pyro 3D combustion shader should branch on pyro mode");
        require_contains(combustion_shader, "apply_explosion_dissipation",
                         "pyro 3D combustion shader should cool direct explosion flames");
        require_contains(combustion_shader, "smoke_mask * 0.72",
                         "pyro 3D combustion shader should keep explosion smoke turbulent");
        require_contains(divergence_shader, "pyro_expansion",
                         "pyro 3D divergence shader should apply pyro expansion");
        require_contains(advect_shader, "inside_obstacle",
                         "pyro 3D advection shader should clear the obstacle volume");
        require_contains(divergence_shader, "inside_obstacle_coord",
                         "pyro 3D divergence shader should treat the obstacle as solid");
        require_contains(pressure_shader, "pressure_neighbor_or_center",
                         "pyro 3D pressure shader should use solid boundary pressure");
        require_contains(projection_shader, "pressure_neighbor_or_center",
                         "pyro 3D projection shader should use solid boundary pressure");
        require_contains(raymarch_shader, "ray_sphere_intersection",
                         "pyro 3D raymarch shader should render the ball obstacle");
        require_contains(raymarch_shader, "cubey/color_space.glsl",
                         "pyro 3D raymarch shader should use shared color conversion");
        require_contains(raymarch_shader, "cubey/environment_lighting.glsl",
                         "pyro 3D raymarch shader should use shared environment lighting");
        require_contains(raymarch_shader, "layout(set = 1, binding = 0)",
                         "pyro 3D raymarch shader should bind environment lighting uniforms");
        require_contains(raymarch_shader, "style_options",
                         "pyro 3D raymarch shader should expose render style controls");
        require_contains(raymarch_shader, "color_options",
                         "pyro 3D raymarch shader should expose palette controls");
        require_contains(raymarch_shader, "display_transform",
                         "pyro 3D raymarch shader should apply exposure display transform");
        require_contains(raymarch_shader, "cubey_env_exposure",
                         "pyro 3D raymarch shader should use resolved shared exposure");
        require_contains(raymarch_shader, "cubey_env_primary_light(",
                         "pyro 3D raymarch lighting should use shared environment light");
        require_contains(raymarch_shader, "soot_signal",
                         "pyro 3D raymarch shader should darken smoke as soot dominates");
        require_contains(raymarch_shader, "flame_height",
                         "pyro 3D raymarch shader should fade flame emission into upper smoke");
        require_contains(raymarch_shader, "explosion_render_mode",
                         "pyro 3D raymarch shader should keep explosion flash separate from fire");
        require_contains(raymarch_shader, "external_background_enabled",
                         "pyro 3D raymarch shader should composite over direct backgrounds");
        require_contains(raymarch_shader, "scene_depth_texture",
                         "pyro 3D raymarch should consume opaque scene depth");
        require_contains(raymarch_shader, "far_t = min(far_t, scene_ray_distance(direction))",
                         "pyro 3D raymarch should stop at terrain and other scene geometry");
        require_contains(raymarch_shader, "external_background ? 0.0",
                         "pyro 3D should not reveal its empty volume bounds over terrain");
        require_contains(shadow_shader, "light_transmittance",
                         "pyro 3D shadow shader should retain shadow raymarching");
        require_contains(shadow_shader, "cubey/environment_lighting.glsl",
                         "pyro 3D shadow shader should use shared environment lighting");
        require_contains(shadow_shader, "cubey_env_primary_light_direction",
                         "pyro 3D shadow rays should follow the shared environment light");
        require_contains(commands_source, "environment_descriptor_set(frame_slot_index)",
                         "pyro 3D commands should bind per-frame environment descriptors");
        require_contains(commands_source, "atmosphere_background_descriptor_set",
                         "pyro 3D commands should draw the direct atmosphere background");
        require_contains(commands_source, "RenderGraphFrameExecutor",
                         "pyro 3D presentation should use the shared render graph executor");
        require_contains(commands_source, "pyro terrain shadow",
                         "pyro 3D terrain should declare its cached shadow pass");
        require_contains(commands_source, "terrain->record_surface_draws",
                         "pyro 3D scene depth should include the shared terrain surface");
        require_contains(gpu_resources_source, "EnvironmentLightingUniforms",
                         "pyro 3D GPU resources should allocate environment lighting uniforms");
        require_contains(gpu_resources_source, "AtmosphereBackgroundFrameMaterialConfig",
                         "pyro 3D GPU resources should create atmosphere background descriptors");
        require_contains(gpu_resources_source, "atmosphere_background_.create_pipeline",
                         "pyro 3D GPU resources should create the atmosphere background pipeline");
        require_contains(cmake_source, "sky/celestial_body.vert",
                         "pyro 3D build should compile the shared celestial body vertex shader");
        require_contains(cmake_source, "sky/celestial_body.frag",
                         "pyro 3D build should compile the shared celestial body fragment shader");
        require_contains(gpu_resources_source, "CelestialBodyFrameMaterialConfig",
                         "pyro 3D should create the shared geometry moon frame material");
        require_contains(gpu_resources_source, "lunar_surface_sampler",
                         "pyro 3D moon geometry should bind the visible moon surface map");
        require_contains(gpu_resources_source, "CelestialBodyDepthMode::None",
                         "pyro 3D moon geometry should render as a no-depth sky backdrop");
        require_contains(commands_source, "moon_body_frame().record_draw",
                         "pyro 3D commands should draw moon geometry inside the scene pass");
        require_contains(gpu_resources_source, "ComputePipelineResourceConfig",
                         "pyro 3D shadow pipeline should support multiple descriptor sets");
        require_contains(app_source, "draw_atmosphere_environment_controls",
                         "pyro 3D should expose shared environment controls");
        require_contains(app_source, "TerrainRasterHeightSource",
                         "pyro 3D should load terrain through the shared raster source");
        require_contains(app_source, "TerrainBackdropRuntime",
                         "pyro 3D should use the shared terrain runtime");
        require_contains(cmake_source, "cubey_terrain_backdrop_shader_sources",
                         "pyro 3D should compile the shared terrain shader set");
        require_contains(app_source, "create_atmosphere_background_cached_textures",
                         "pyro 3D should reuse shared cached atmosphere background atlases");
        require_contains(app_source, "upload_atmosphere_background",
                         "pyro 3D should upload direct atmosphere background uniforms");
        require_contains(app_source, "environment.render_moon_disk = false",
                         "pyro 3D atmosphere background should suppress the inline moon disk");
        require_contains(app_source, "upload_moon_body",
                         "pyro 3D should upload geometry moon uniforms");
        require_contains(app_source, "resolved_render_exposure",
                         "pyro 3D should combine shared exposure with the project render bias");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pyro_3d_config_tests: %s\n", error.what());
        return 1;
    }

    return 0;
}
