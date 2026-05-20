#include "fluid_3d_config.h"
#include "fluid_3d_sources.h"

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
    try {
        const cubey::projects::fluid_3d::Fluid3DConfig config;
        constexpr std::size_t kExpectedCellCount =
            std::size_t{128} * std::size_t{128} * std::size_t{128};
        require(config.grid_width == 128, "fluid 3D should default to 128 columns");
        require(config.grid_height == 128, "fluid 3D should default to 128 rows");
        require(config.grid_depth == 128, "fluid 3D should default to 128 slices");
        require(config.source_count == 3, "fluid 3D should default to three sources");
        require(config.scenario == cubey::projects::fluid_3d::Fluid3DScenario::SmokePlume,
                "fluid 3D should default to the smoke plume scenario");
        require(config.pressure_iterations == 12,
                "fluid 3D pressure solve should default to a small 3D budget");
        require(config.raymarch_steps == 128, "fluid 3D should default to one step per slice");
        require(config.shadow_grid_width == 64, "fluid 3D should default to a 64-wide shadow grid");
        require(config.shadow_grid_height == 64,
                "fluid 3D should default to a 64-high shadow grid");
        require(config.shadow_grid_depth == 64,
                "fluid 3D should default to a 64-deep shadow grid");
        require(config.shadow_steps == 64, "fluid 3D shadow rays should default to 64 steps");
        require(config.shadow_update_interval == 1,
                "fluid 3D should update shadows every frame by default");
        require(config.density_decay_per_second == 0.99F,
                "fluid 3D density decay should match the tuned default");
        require(config.velocity_decay_per_second == 0.99F,
                "fluid 3D velocity decay should match the tuned default");
        require(config.source_radius == 0.05F,
                "fluid 3D source radius should match the tuned default");
        require(config.source_velocity_strength == 6.0F,
                "fluid 3D source force should match the tuned default");
        require(config.source_smoke_amount == 6.0F,
                "fluid 3D source smoke should match the tuned default");
        require(config.source_heat_amount == 1.4F,
                "fluid 3D source heat should match the plume default");
        require(config.source_flame_amount == 2.0F,
                "fluid 3D source flame should match the tuned default");
        require(config.explosion_interval_seconds == 3.0F,
                "fluid 3D explosion interval should match the tuned default");
        require(config.explosion_duration_seconds == 0.12F,
                "fluid 3D explosion duration should default to a short impulse");
        require(config.explosion_boost == 18.0F,
                "fluid 3D explosion boost should default to a high impulse scale");
        require(config.fire_ignition_temperature == 0.22F,
                "fluid 3D fire ignition should match the tuned default");
        require(config.fire_burn_rate == 2.2F,
                "fluid 3D fire burn rate should match the tuned default");
        require(config.fire_heat_output == 1.65F,
                "fluid 3D fire heat output should match the tuned default");
        require(config.fire_soot_yield == 0.070F,
                "fluid 3D fire soot yield should match the tuned default");
        require(config.fire_expansion == 0.65F,
                "fluid 3D fire expansion should match the tuned default");
        require(config.fire_flame_cooling == 5.5F,
                "fluid 3D fire flame cooling should match the tuned default");
        require(config.fire_shredding == 1.6F,
                "fluid 3D fire shredding should match the tuned default");
        require(config.fire_turbulence == 0.35F,
                "fluid 3D fire turbulence should match the tuned default");
        require(config.vorticity_strength == 1.0F,
                "fluid 3D vorticity should match the tuned default");
        require(config.buoyancy_strength == 1.0F,
                "fluid 3D buoyancy should default to a mild upward force");
        require(config.absorption == 8.0F,
                "fluid 3D absorption should match the tuned default");
        require(config.emission == 2.0F,
                "fluid 3D light emission should match the tuned default");
        require(config.shadow_absorption == 50.0F,
                "fluid 3D shadow absorption should match the tuned default");
        require(config.ambient_light == 0.5F,
                "fluid 3D ambient light should match the tuned default");
        require(cubey::projects::fluid_3d::volume_cell_count(config) == kExpectedCellCount,
                "fluid 3D cell count should multiply all dimensions");
        require(cubey::projects::fluid_3d::volume_byte_size(config, 8) ==
                    8U * kExpectedCellCount,
                "fluid 3D generic byte size should use caller-provided cell bytes");
        require(cubey::projects::fluid_3d::shadow_volume_cell_count(config) ==
                    std::size_t{64} * std::size_t{64} * std::size_t{64},
                "fluid 3D shadow cell count should use the decoupled shadow grid");
        require(cubey::projects::fluid_3d::next_debug_view(
                    cubey::projects::fluid_3d::Fluid3DDebugView::Smoke) ==
                    cubey::projects::fluid_3d::Fluid3DDebugView::DensitySlice,
                "fluid 3D debug view should cycle from smoke to density slice");
        require(cubey::projects::fluid_3d::next_debug_view(
                    cubey::projects::fluid_3d::Fluid3DDebugView::DensitySlice) ==
                    cubey::projects::fluid_3d::Fluid3DDebugView::Velocity,
                "fluid 3D debug view should cycle from density slice to velocity");
        require(cubey::projects::fluid_3d::next_debug_view(
                    cubey::projects::fluid_3d::Fluid3DDebugView::Velocity) ==
                    cubey::projects::fluid_3d::Fluid3DDebugView::Smoke,
                "fluid 3D debug view should cycle from velocity to smoke");

        cubey::RunConfig default_fire_run_config;
        default_fire_run_config.fluid_scenario = "fire";
        const cubey::projects::fluid_3d::Fluid3DConfig default_fire_config =
            cubey::projects::fluid_3d::fluid_3d_config_from_run_config(default_fire_run_config);
        require(default_fire_config.source_count == 1,
                "fluid 3D fire should default to a single burner source");
        require(default_fire_config.source_radius >
                    cubey::projects::fluid_3d::kDefaultFluid3DSourceRadius,
                "fluid 3D fire should default to a wider burner source");

        cubey::RunConfig run_config;
        run_config.grid_width = 64;
        run_config.grid_height = 48;
        run_config.grid_depth = 32;
        run_config.shadow_grid_width = 24;
        run_config.shadow_grid_height = 20;
        run_config.shadow_grid_depth = 16;
        run_config.shadow_steps = 48;
        run_config.shadow_update_interval = 3;
        run_config.fluid_scenario = "smoke-plume";
        run_config.fluid_sources = 6;
        run_config.fluid_source_radius = 0.08F;
        run_config.fluid_source_force = 7.5F;
        run_config.fluid_smoke = 6.5F;
        run_config.fluid_heat = 1.75F;
        run_config.fluid_flame = 2.5F;
        run_config.fluid_explosion_interval_seconds = 2.5F;
        run_config.fluid_explosion_duration_seconds = 0.18F;
        run_config.fluid_explosion_boost = 22.0F;
        run_config.fluid_fire_ignition_temperature = 0.31F;
        run_config.fluid_fire_burn_rate = 4.5F;
        run_config.fluid_fire_heat_output = 3.25F;
        run_config.fluid_fire_soot_yield = 0.22F;
        run_config.fluid_fire_expansion = 1.8F;
        run_config.fluid_fire_flame_cooling = 2.75F;
        run_config.fluid_fire_shredding = 3.5F;
        run_config.fluid_fire_turbulence = 0.85F;
        run_config.fluid_buoyancy = 1.75F;
        const cubey::projects::fluid_3d::Fluid3DConfig configured =
            cubey::projects::fluid_3d::fluid_3d_config_from_run_config(run_config);
        require(configured.grid_width == 64, "fluid 3D config should honor run config width");
        require(configured.grid_height == 48, "fluid 3D config should honor run config height");
        require(configured.grid_depth == 32, "fluid 3D config should honor run config depth");
        require(configured.shadow_grid_width == 24,
                "fluid 3D config should honor run config shadow width");
        require(configured.shadow_grid_height == 20,
                "fluid 3D config should honor run config shadow height");
        require(configured.shadow_grid_depth == 16,
                "fluid 3D config should honor run config shadow depth");
        require(configured.shadow_steps == 48,
                "fluid 3D config should honor run config shadow steps");
        require(configured.shadow_update_interval == 3,
                "fluid 3D config should honor run config shadow update interval");
        require(configured.source_count == 6,
                "fluid 3D config should honor run config source count");
        require(configured.source_radius == 0.08F,
                "fluid 3D config should honor run config source radius");
        require(configured.source_velocity_strength == 7.5F,
                "fluid 3D config should honor run config source force");
        require(configured.source_smoke_amount == 6.5F,
                "fluid 3D config should honor run config smoke amount");
        require(configured.source_heat_amount == 1.75F,
                "fluid 3D config should honor run config heat amount");
        require(configured.source_flame_amount == 2.5F,
                "fluid 3D config should honor run config flame amount");
        require(configured.explosion_interval_seconds == 2.5F,
                "fluid 3D config should honor run config explosion interval");
        require(configured.explosion_duration_seconds == 0.18F,
                "fluid 3D config should honor run config explosion duration");
        require(configured.explosion_boost == 22.0F,
                "fluid 3D config should honor run config explosion boost");
        require(configured.fire_ignition_temperature == 0.31F,
                "fluid 3D config should honor run config fire ignition");
        require(configured.fire_burn_rate == 4.5F,
                "fluid 3D config should honor run config fire burn rate");
        require(configured.fire_heat_output == 3.25F,
                "fluid 3D config should honor run config fire heat output");
        require(configured.fire_soot_yield == 0.22F,
                "fluid 3D config should honor run config fire soot yield");
        require(configured.fire_expansion == 1.8F,
                "fluid 3D config should honor run config fire expansion");
        require(configured.fire_flame_cooling == 2.75F,
                "fluid 3D config should honor run config fire flame cooling");
        require(configured.fire_shredding == 3.5F,
                "fluid 3D config should honor run config fire shredding");
        require(configured.fire_turbulence == 0.85F,
                "fluid 3D config should honor run config fire turbulence");
        require(configured.buoyancy_strength == 1.75F,
                "fluid 3D config should honor run config buoyancy");

        bool threw_for_too_many_sources = false;
        try {
            cubey::RunConfig invalid_source_config;
            invalid_source_config.fluid_sources =
                cubey::projects::fluid_3d::kMaxFluid3DSourceCount + 1U;
            static_cast<void>(cubey::projects::fluid_3d::fluid_3d_config_from_run_config(
                invalid_source_config));
        } catch (const std::runtime_error&) {
            threw_for_too_many_sources = true;
        }
        require(threw_for_too_many_sources,
                "fluid 3D config should reject source counts above the shader policy limit");

        bool threw_for_invalid_scenario = false;
        try {
            cubey::RunConfig invalid_scenario_config;
            invalid_scenario_config.fluid_scenario = "rainbow-orbit";
            static_cast<void>(cubey::projects::fluid_3d::fluid_3d_config_from_run_config(
                invalid_scenario_config));
        } catch (const std::runtime_error&) {
            threw_for_invalid_scenario = true;
        }
        require(threw_for_invalid_scenario,
                "fluid 3D config should reject unsupported source scenarios");

        std::vector<cubey::projects::fluid_3d::Fluid3DSourceState> sources =
            cubey::projects::fluid_3d::create_fluid_3d_sources(configured);
        require(sources.size() == 6, "fluid 3D source state should match configured count");
        require(sources.front().position[1] < 0.12F,
                "fluid 3D smoke plume should start near the lower volume");
        require(length_squared(sources.front().velocity) > 0.0F,
                "fluid 3D source should carry a velocity direction");
        require(sources.front().material_amount[0] == configured.source_smoke_amount,
                "fluid 3D source should carry smoke amount");
        require(sources.front().material_amount[1] == configured.source_heat_amount,
                "fluid 3D source should carry heat amount");
        require(sources.front().material_amount[2] == 0.0F,
                "fluid 3D smoke plume should not inject flame");
        const std::vector<cubey::projects::fluid_3d::Fluid3DSourceGpu> gpu_sources =
            cubey::projects::fluid_3d::fluid_3d_sources_to_gpu(sources, configured);
        require(gpu_sources.size() == 6, "fluid 3D GPU source state should match state count");
        require(gpu_sources.front().position_radius[3] == configured.source_radius,
                "fluid 3D GPU source should carry configured radius");
        require(gpu_sources.front().velocity_strength[3] == configured.source_velocity_strength,
                "fluid 3D GPU source should carry configured force");
        require(gpu_sources.front().material_amount[0] == configured.source_smoke_amount,
                "fluid 3D GPU source should carry smoke amount");
        require(cubey::projects::fluid_3d::fluid_3d_source_byte_size(configured) ==
                    sizeof(cubey::projects::fluid_3d::Fluid3DSourceGpu) * 6U,
                "fluid 3D active source byte size should match configured count");
        require(cubey::projects::fluid_3d::fluid_3d_source_capacity_byte_size() ==
                    sizeof(cubey::projects::fluid_3d::Fluid3DSourceGpu) *
                        cubey::projects::fluid_3d::kMaxFluid3DSourceCount,
                "fluid 3D source capacity byte size should cover the shader policy limit");

        cubey::RunConfig explosion_run_config = run_config;
        explosion_run_config.fluid_scenario = "explosion";
        const cubey::projects::fluid_3d::Fluid3DConfig explosion_config =
            cubey::projects::fluid_3d::fluid_3d_config_from_run_config(explosion_run_config);
        require(explosion_config.scenario == cubey::projects::fluid_3d::Fluid3DScenario::Explosion,
                "fluid 3D config should parse the explosion scenario");
        std::vector<cubey::projects::fluid_3d::Fluid3DSourceState> explosion_sources =
            cubey::projects::fluid_3d::create_fluid_3d_sources(explosion_config);
        require(explosion_sources.front().position[1] > 0.20F,
                "fluid 3D explosion should originate above the lower plume source");
        require(explosion_sources.front().material_amount[2] ==
                    explosion_config.source_flame_amount,
                "fluid 3D explosion should carry flame amount");
        const std::vector<cubey::projects::fluid_3d::Fluid3DSourceGpu> explosion_active_gpu =
            cubey::projects::fluid_3d::update_fluid_3d_sources(
                explosion_sources, explosion_config,
                {
                    .delta_seconds = explosion_config.fixed_delta_seconds,
                    .elapsed_seconds = 2.5,
                    .frame_index = 1,
                });
        require(explosion_active_gpu.front().material_amount[0] >
                    explosion_config.source_smoke_amount,
                "fluid 3D explosion scenario should boost smoke during the impulse window");
        require(explosion_active_gpu.front().velocity_strength[3] >
                    explosion_config.source_velocity_strength,
                "fluid 3D explosion scenario should boost force during the impulse window");
        const std::vector<cubey::projects::fluid_3d::Fluid3DSourceGpu> explosion_pause_gpu =
            cubey::projects::fluid_3d::update_fluid_3d_sources(
                explosion_sources, explosion_config,
                {
                    .delta_seconds = explosion_config.fixed_delta_seconds,
                    .elapsed_seconds = 2.75,
                    .frame_index = 2,
                });
        require(explosion_pause_gpu.front().material_amount[0] == 0.0F,
                "fluid 3D explosion scenario should pause smoke between impulses");
        require(explosion_pause_gpu.front().velocity_strength[3] == 0.0F,
                "fluid 3D explosion scenario should pause force between impulses");

        cubey::RunConfig fire_run_config = run_config;
        fire_run_config.fluid_scenario = "fire";
        const cubey::projects::fluid_3d::Fluid3DConfig fire_config =
            cubey::projects::fluid_3d::fluid_3d_config_from_run_config(fire_run_config);
        require(fire_config.scenario == cubey::projects::fluid_3d::Fluid3DScenario::Fire,
                "fluid 3D config should parse the fire scenario");
        std::vector<cubey::projects::fluid_3d::Fluid3DSourceState> fire_sources =
            cubey::projects::fluid_3d::create_fluid_3d_sources(fire_config);
        require(fire_sources.front().position[1] < 0.12F,
                "fluid 3D fire should originate near the lower burner");
        require(fire_sources.front().material_amount[0] < fire_config.source_smoke_amount,
                "fluid 3D fire should use less soot than the smoke plume");
        require(fire_sources.front().material_amount[1] > fire_config.source_heat_amount,
                "fluid 3D fire should boost heat");
        require(fire_sources.front().material_amount[2] == 0.0F,
                "fluid 3D fire source should let combustion create visible flame");
        require(fire_sources.front().material_amount[3] > fire_config.source_flame_amount,
                "fluid 3D fire should treat the source flame amount as fuel");
        const std::vector<cubey::projects::fluid_3d::Fluid3DSourceGpu> early_fire_gpu =
            cubey::projects::fluid_3d::update_fluid_3d_sources(
                fire_sources, fire_config,
                {
                    .delta_seconds = fire_config.fixed_delta_seconds,
                    .elapsed_seconds = 0.25,
                    .frame_index = 1,
                });
        const std::vector<cubey::projects::fluid_3d::Fluid3DSourceGpu> later_fire_gpu =
            cubey::projects::fluid_3d::update_fluid_3d_sources(
                fire_sources, fire_config,
                {
                    .delta_seconds = fire_config.fixed_delta_seconds,
                    .elapsed_seconds = 0.75,
                    .frame_index = 2,
                });
        require(early_fire_gpu.front().position_radius != later_fire_gpu.front().position_radius,
                "fluid 3D fire source turbulence should jitter source position/radius over time");
        require(early_fire_gpu.front().material_amount[3] != later_fire_gpu.front().material_amount[3],
                "fluid 3D fire source turbulence should vary fuel over time");

        require(cubey::projects::fluid_3d::fluid_3d_headless_frame_count(run_config) == 120,
                "fluid 3D headless PNG should default to a settled simulation frame");
        run_config.frames = 8;
        require(cubey::projects::fluid_3d::fluid_3d_headless_frame_count(run_config) == 8,
                "fluid 3D headless frame count should honor explicit frames");
        const cubey::FrameTiming fixed_timing =
            cubey::projects::fluid_3d::fixed_fluid_3d_headless_timing(configured, 5);
        require(fixed_timing.frame_index == 5,
                "fluid 3D fixed headless timing should preserve frame index");
        require(fixed_timing.delta_seconds == configured.fixed_delta_seconds,
                "fluid 3D fixed headless timing should use fixed dt");

        const std::filesystem::path source_dir = CUBEY_FLUID_3D_SOURCE_DIR;
        const std::string advect_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_advect.comp");
        const std::string advect_correct_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_advect_correct.comp");
        const std::string combustion_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_combust.comp");
        const std::string divergence_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_divergence.comp");
        const std::string render_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_raymarch.frag");
        const std::string shadow_shader =
            read_text_file(source_dir / "shaders" / "fluid_3d_shadow.comp");
        require_contains(advect_shader, "layout(rgba16f",
                         "fluid 3D bulk volumes should use explicit RGBA16F storage images");
        require_contains(shadow_shader, "imageSize(shadow_volume)",
                         "fluid 3D shadow dispatch should use the decoupled shadow volume size");
        require_contains(advect_correct_shader, "reversed_density",
                         "fluid 3D should use a MacCormack/BFECC correction pass");
        require_contains(advect_correct_shader, "source_bounds",
                         "fluid 3D MacCormack correction should use a source-neighborhood limiter");
        require_contains(advect_correct_shader, "spent_flame",
                         "fluid 3D correction pass should turn spent flame into smoke");
        require_contains(combustion_shader, "apply_combustion",
                         "fluid 3D combustion pass should consume hot fuel");
        require_contains(combustion_shader, "apply_fire_dissipation",
                         "fluid 3D combustion pass should keep fire fields bounded");
        require_contains(combustion_shader, "material.a",
                         "fluid 3D combustion pass should use the fuel channel");
        require_contains(combustion_shader, "flame_shredding_force",
                         "fluid 3D combustion pass should shred flame edges from heat gradients");
        require_contains(combustion_shader, "buoyancy_force",
                         "fluid 3D combustion pass should apply buoyancy");
        require_contains(divergence_shader, "fire_expansion",
                         "fluid 3D divergence pass should preserve fire gas release");
        require_contains(render_shader, "sampler3D",
                         "fluid 3D render shader should raymarch sampled 3D textures");
        require_contains(render_shader, "smoke_albedo",
                         "fluid 3D render shader should shade smoke as material, not RGB dye");
        require_contains(render_shader, "flame_emission",
                         "fluid 3D render shader should expose semantic flame emission");
        require_contains(render_shader, "flame_transfer",
                         "fluid 3D render shader should sharpen the flame transfer function");
        require_contains(render_shader, "flame_emission(density, position) * step_length",
                         "fluid 3D render shader should accumulate visible flame emission");
        require_contains(render_shader, "soot_cutoff",
                         "fluid 3D render shader should keep flame separate from smoke");
        require_contains(shadow_shader, "light_transmittance",
                         "fluid 3D shadow shader should precompute volume light transmittance");
        require_contains(shadow_shader, "soot_density_at",
                         "fluid 3D shadow shader should shadow from smoke/soot density");

        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_3d_config_tests: %s\n", error.what());
        return 1;
    }
}
