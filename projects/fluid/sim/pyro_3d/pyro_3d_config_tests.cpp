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
        require(config.pressure_iterations == 12,
                "pyro 3D pressure solve should default to a small 3D budget");
        require(config.raymarch_steps == 128, "pyro 3D should default to one step per slice");
        require(config.shadow_grid_width == 64, "pyro 3D should default to a 64-wide shadow grid");
        require(config.shadow_grid_height == 64,
                "pyro 3D should default to a 64-high shadow grid");
        require(config.shadow_grid_depth == 64,
                "pyro 3D should default to a 64-deep shadow grid");
        require(config.shadow_steps == 64, "pyro 3D shadow rays should default to 64 steps");
        require(config.shadow_update_interval == 1,
                "pyro 3D should update shadows every frame by default");
        require(config.density_decay_per_second == 0.99F,
                "pyro 3D density decay should match the tuned default");
        require(config.velocity_decay_per_second == 0.99F,
                "pyro 3D velocity decay should match the tuned default");
        require(config.source_velocity_strength == 6.0F,
                "pyro 3D source force should match the tuned default");
        require(config.source_smoke_amount == 6.0F,
                "pyro 3D source soot should match the tuned default");
        require(config.source_heat_amount == 1.4F,
                "pyro 3D source temperature should match the tuned default");
        require(config.source_flame_amount == 2.0F,
                "pyro 3D source fuel should match the tuned default");
        require(config.explosion_interval_seconds == 3.0F,
                "pyro 3D explosion interval should match the tuned default");
        require(config.explosion_duration_seconds == 0.12F,
                "pyro 3D explosion duration should default to a short impulse");
        require(config.explosion_boost == 18.0F,
                "pyro 3D explosion boost should default to a high impulse scale");
        require(config.fire_ignition_temperature == 0.22F,
                "pyro 3D ignition should match the tuned default");
        require(config.fire_burn_rate == 2.2F,
                "pyro 3D burn rate should match the tuned default");
        require(config.fire_heat_output == 1.65F,
                "pyro 3D heat output should match the tuned default");
        require(config.fire_soot_yield == 0.070F,
                "pyro 3D soot yield should match the tuned default");
        require(config.fire_expansion == 0.65F,
                "pyro 3D fire expansion should match the tuned default");
        require(config.fire_flame_cooling == 5.5F,
                "pyro 3D fire flame cooling should match the tuned default");
        require(config.fire_shredding == 1.6F,
                "pyro 3D fire shredding should match the tuned default");
        require(config.fire_turbulence == 0.35F,
                "pyro 3D fire turbulence should match the tuned default");
        require(config.vorticity_strength == 1.0F,
                "pyro 3D vorticity should match the tuned default");
        require(config.buoyancy_strength == 1.0F,
                "pyro 3D buoyancy should default to a mild upward force");
        require(config.absorption == 8.0F,
                "pyro 3D absorption should match the tuned default");
        require(config.emission == 2.0F,
                "pyro 3D light emission should match the tuned default");
        require(config.shadow_absorption == 50.0F,
                "pyro 3D shadow absorption should match the tuned default");
        require(config.ambient_light == 0.5F,
                "pyro 3D ambient light should match the tuned default");
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

        cubey::RunConfig run_config;
        run_config.grid_width = 64;
        run_config.grid_height = 48;
        run_config.grid_depth = 32;
        run_config.shadow_grid_width = 24;
        run_config.shadow_grid_height = 20;
        run_config.shadow_grid_depth = 16;
        run_config.shadow_steps = 48;
        run_config.shadow_update_interval = 3;
        run_config.pyro_sources = 6;
        run_config.pyro_source_radius = 0.08F;
        run_config.pyro_source_force = 7.5F;
        run_config.pyro_soot = 6.5F;
        run_config.pyro_temperature = 1.75F;
        run_config.pyro_fuel = 2.5F;
        run_config.pyro_ignition_temperature = 0.31F;
        run_config.pyro_burn_rate = 4.5F;
        run_config.pyro_heat_output = 3.25F;
        run_config.pyro_soot_yield = 0.22F;
        run_config.pyro_expansion = 1.8F;
        run_config.pyro_flame_cooling = 2.75F;
        run_config.pyro_shredding = 3.5F;
        run_config.pyro_turbulence = 0.85F;
        run_config.pyro_buoyancy = 1.75F;
        run_config.explosion_interval_seconds = 2.5F;
        run_config.explosion_duration_seconds = 0.18F;
        run_config.explosion_boost = 22.0F;

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
        require(fire_config.buoyancy_strength == 1.75F,
                "pyro 3D config should honor run config buoyancy");

        bool threw_for_too_many_sources = false;
        try {
            cubey::RunConfig invalid_source_config;
            invalid_source_config.pyro_sources = pyro::kMaxPyro3DSourceCount + 1U;
            static_cast<void>(pyro::pyro_3d_config_from_run_config(
                invalid_source_config, pyro::Pyro3DMode::Fire));
        } catch (const std::runtime_error&) {
            threw_for_too_many_sources = true;
        }
        require(threw_for_too_many_sources,
                "pyro 3D config should reject source counts above the shader policy limit");

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
        require(pyro::pyro_3d_source_byte_size(fire_config) ==
                    sizeof(pyro::Pyro3DSourceGpu) * 6U,
                "pyro 3D active source byte size should match configured count");
        require(pyro::pyro_3d_source_capacity_byte_size() ==
                    sizeof(pyro::Pyro3DSourceGpu) * pyro::kMaxPyro3DSourceCount,
                "pyro 3D source capacity byte size should cover the shader policy limit");

        const std::vector<pyro::Pyro3DSourceGpu> early_fire_gpu =
            pyro::update_pyro_3d_sources(
                fire_sources, fire_config,
                {
                    .delta_seconds = fire_config.fixed_delta_seconds,
                    .elapsed_seconds = 0.25,
                    .frame_index = 1,
                });
        const std::vector<pyro::Pyro3DSourceGpu> later_fire_gpu =
            pyro::update_pyro_3d_sources(
                fire_sources, fire_config,
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
        require(explosion_sources.front().position[1] > 0.20F,
                "pyro 3D explosion should originate above the lower burner");
        require(explosion_sources.front().material_amount[2] ==
                    explosion_config.source_flame_amount,
                "pyro 3D explosion should carry flame amount");
        const std::vector<pyro::Pyro3DSourceGpu> explosion_active_gpu =
            pyro::update_pyro_3d_sources(
                explosion_sources, explosion_config,
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
        const std::vector<pyro::Pyro3DSourceGpu> explosion_pause_gpu =
            pyro::update_pyro_3d_sources(
                explosion_sources, explosion_config,
                {
                    .delta_seconds = explosion_config.fixed_delta_seconds,
                    .elapsed_seconds = 2.75,
                    .frame_index = 2,
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
        const cubey::FrameTiming fixed_timing =
            pyro::fixed_pyro_3d_headless_timing(fire_config, 5);
        require(fixed_timing.frame_index == 5,
                "pyro 3D fixed headless timing should preserve frame index");
        require(fixed_timing.delta_seconds == fire_config.fixed_delta_seconds,
                "pyro 3D fixed headless timing should use fixed dt");

        const std::filesystem::path source_dir = CUBEY_PYRO_3D_SOURCE_DIR;
        const std::string advect_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_advect.comp");
        const std::string advect_correct_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_advect_correct.comp");
        const std::string combustion_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_combust.comp");
        const std::string shadow_shader =
            read_text_file(source_dir / "shaders" / "pyro_3d_shadow.comp");
        require_contains(advect_shader, "previous_uv",
                         "pyro 3D advection shader should keep backtraced advection");
        require_contains(advect_correct_shader, "limited_density",
                         "pyro 3D correction shader should clamp corrected density");
        require_contains(combustion_shader, "apply_combustion",
                         "pyro 3D combustion shader should run the fire model");
        require_contains(combustion_shader, "fire_mode",
                         "pyro 3D combustion shader should branch on pyro mode");
        require_contains(shadow_shader, "light_transmittance",
                         "pyro 3D shadow shader should retain shadow raymarching");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "pyro_3d_config_tests: %s\n", error.what());
        return 1;
    }

    return 0;
}
