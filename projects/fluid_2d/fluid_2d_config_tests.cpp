#include "fluid_2d_config.h"
#include "fluid_2d_injectors.h"

#include <cubey/core/frame_clock.h>
#include <cubey/core/run_config.h>

#include <array>
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

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (actual < expected - kTolerance || actual > expected + kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) == std::string::npos) {
        throw std::runtime_error(message);
    }
}

void require_not_contains(const std::string& haystack, const char* needle, const char* message) {
    if (haystack.find(needle) != std::string::npos) {
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

[[nodiscard]] float length_squared(std::array<float, 2> value) {
    return (value[0] * value[0]) + (value[1] * value[1]);
}

} // namespace

int main() {
    try {
        const cubey::projects::fluid_2d::Fluid2DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{1024} * std::size_t{1024};
        require(config.grid_width == 1024, "fluid grid should default to 1024 columns");
        require(config.grid_height == 1024, "fluid grid should default to 1024 rows");
        require(config.procedural_injector_count == 3,
                "fluid should default to three procedural injectors");
        require(cubey::projects::fluid_2d::field_cell_count(config) == kExpectedCellCount,
                "field cell count should multiply dimensions");
        require(cubey::projects::fluid_2d::field_byte_size(config) ==
                    sizeof(cubey::projects::fluid_2d::FluidCellGpu) * kExpectedCellCount,
                "field byte size should cover one cell per grid location");
        require(config.pressure_iterations == 32,
                "fluid pressure solve should default to 32 Jacobi iterations");
        require(config.dye_decay_per_second == 0.990F,
                "fluid dye decay should default to controlled linger");
        require(config.velocity_decay_per_second == 0.993F,
                "fluid velocity decay should default to controlled linger");
        require(config.injector_injection_radius == 0.032F,
                "fluid injector radius should be tuned for sharper moving sources");
        require(config.injector_injection_strength == 6.0F,
                "fluid injector strength should default to a visible multi-source impulse");
        require(config.injector_propulsion_strength == 1.0F,
                "fluid injector propulsion should default to neutral scaling");
        require(config.injector_speed == 1.0F,
                "fluid injector speed should default to realtime motion");
        require(config.vorticity_strength == 18.0F,
                "fluid vorticity strength should have a visible default");
        require(config.injector_motion ==
                    cubey::projects::fluid_2d::Fluid2DInjectorMotion::TwoRings,
                "fluid injector motion should default to two-rings");
        require(!config.obstacles_enabled, "fluid obstacles should default disabled");
        require(cubey::projects::fluid_2d::scalar_field_byte_size(config) ==
                    sizeof(float) * kExpectedCellCount,
                "scalar field byte size should cover one float per grid location");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Dye) ==
                    cubey::projects::fluid_2d::FluidDebugView::Velocity,
                "debug view should cycle from dye to velocity");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Velocity) ==
                    cubey::projects::fluid_2d::FluidDebugView::Divergence,
                "debug view should cycle from velocity to divergence");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Divergence) ==
                    cubey::projects::fluid_2d::FluidDebugView::Pressure,
                "debug view should cycle from divergence to pressure");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Pressure) ==
                    cubey::projects::fluid_2d::FluidDebugView::Speed,
                "debug view should cycle from pressure to speed");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Speed) ==
                    cubey::projects::fluid_2d::FluidDebugView::Vorticity,
                "debug view should cycle from speed to vorticity");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Vorticity) ==
                    cubey::projects::fluid_2d::FluidDebugView::Obstacle,
                "debug view should cycle from vorticity to obstacle");
        require(cubey::projects::fluid_2d::next_debug_view(
                    cubey::projects::fluid_2d::FluidDebugView::Obstacle) ==
                    cubey::projects::fluid_2d::FluidDebugView::Dye,
                "debug view should cycle from obstacle to dye");

        cubey::RunConfig run_config;
        run_config.grid_width = 1024;
        run_config.grid_height = 768;
        run_config.injectors = 8;
        run_config.injector_motion = "alternating-direction-orbits";
        run_config.injector_force = 7.5F;
        run_config.injector_propulsion = 1.6F;
        run_config.injector_speed = 1.75F;
        const cubey::projects::fluid_2d::Fluid2DConfig configured =
            cubey::projects::fluid_2d::fluid_config_from_run_config(run_config);
        require(configured.grid_width == 1024,
                "fluid config should honor run config grid width");
        require(configured.grid_height == 768,
                "fluid config should honor run config grid height");
        require(configured.procedural_injector_count == 8,
                "fluid config should honor run config injector count");
        require(configured.injector_motion ==
                    cubey::projects::fluid_2d::Fluid2DInjectorMotion::AlternatingDirectionOrbits,
                "fluid config should honor run config injector motion");
        require(configured.injector_injection_strength == 7.5F,
                "fluid config should honor run config injector force");
        require(configured.injector_propulsion_strength == 1.6F,
                "fluid config should honor run config injector propulsion");
        require(configured.injector_speed == 1.75F,
                "fluid config should honor run config injector speed");
        require(!configured.obstacles_enabled,
                "fluid config should keep obstacles disabled unless requested");
        run_config.obstacles = true;
        const cubey::projects::fluid_2d::Fluid2DConfig configured_with_obstacles =
            cubey::projects::fluid_2d::fluid_config_from_run_config(run_config);
        require(configured_with_obstacles.obstacles_enabled,
                "fluid config should honor run config obstacle toggle");

        bool threw_for_too_many_injectors = false;
        try {
            cubey::RunConfig invalid_injector_config;
            invalid_injector_config.injectors =
                cubey::projects::fluid_2d::kMaxProceduralInjectorCount + 1U;
            static_cast<void>(
                cubey::projects::fluid_2d::fluid_config_from_run_config(invalid_injector_config));
        } catch (const std::runtime_error&) {
            threw_for_too_many_injectors = true;
        }
        require(threw_for_too_many_injectors,
                "fluid config should reject injector counts above the shader policy limit");

        bool threw_for_unknown_motion = false;
        try {
            cubey::RunConfig invalid_motion_config;
            invalid_motion_config.injector_motion = "crossflow";
            static_cast<void>(
                cubey::projects::fluid_2d::fluid_config_from_run_config(invalid_motion_config));
        } catch (const std::runtime_error&) {
            threw_for_unknown_motion = true;
        }
        require(threw_for_unknown_motion,
                "fluid config should reject unknown injector motion names");

        std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> injectors =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(configured);
        require(injectors.size() == 8, "fluid injector state should match configured count");
        require(injectors[0].motion ==
                    cubey::projects::fluid_2d::Fluid2DInjectorMotion::AlternatingDirectionOrbits,
                "fluid injector state should remember its configured motion");
        require_close(injectors[0].hue, 0.0F, "first injector hue should start at red");
        require_close(injectors[1].hue, 0.125F, "injector hues should spread evenly");
        const std::array<float, 2> initial_position = injectors[0].position;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorGpu> initial_gpu =
            cubey::projects::fluid_2d::fluid_2d_injectors_to_gpu(injectors, configured);
        require(initial_gpu.size() == 8, "fluid GPU injector state should match state count");
        require(initial_gpu[0].velocity_carry_propulsion[2] > 1.0F,
                "fluid GPU injector should carry source velocity into the fluid");
        require(initial_gpu[0].velocity_carry_propulsion[3] > 0.0F,
                "fluid GPU injector should expose opposite-direction propulsion force");
        cubey::projects::fluid_2d::Fluid2DConfig no_propulsion_config = configured;
        no_propulsion_config.injector_propulsion_strength = 0.0F;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorGpu> no_propulsion_gpu =
            cubey::projects::fluid_2d::fluid_2d_injectors_to_gpu(injectors,
                                                                 no_propulsion_config);
        require(no_propulsion_gpu[0].velocity_carry_propulsion[3] == 0.0F,
                "fluid GPU injector should honor disabled propulsion force");
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorGpu> advanced_gpu =
            cubey::projects::fluid_2d::update_fluid_2d_injectors(
                injectors, configured,
                {
                    .delta_seconds = configured.fixed_delta_seconds,
                    .elapsed_seconds = configured.fixed_delta_seconds,
                    .frame_index = 1,
                });
        require(advanced_gpu.size() == 8, "fluid injector update should keep configured count");
        require(injectors[0].position != initial_position,
                "fluid injector physics should advance source positions");
        require(cubey::projects::fluid_2d::fluid_2d_injector_byte_size(configured) ==
                    sizeof(cubey::projects::fluid_2d::Fluid2DInjectorGpu) * 8U,
                "fluid injector byte size should cover one GPU record per injector");
        require(cubey::projects::fluid_2d::fluid_2d_injector_capacity_byte_size() ==
                    sizeof(cubey::projects::fluid_2d::Fluid2DInjectorGpu) *
                        cubey::projects::fluid_2d::kMaxProceduralInjectorCount,
                "fluid injector capacity should cover live-editable injector count");

        cubey::projects::fluid_2d::Fluid2DConfig one_ring_config = configured;
        one_ring_config.injector_motion =
            cubey::projects::fluid_2d::Fluid2DInjectorMotion::OneRing;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> one_ring =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(one_ring_config);
        require_close(one_ring[0].orbit_radius, one_ring[1].orbit_radius,
                      "one-ring injector motion should keep every source on one radius");
        require_close(one_ring[0].orbit_direction, one_ring[1].orbit_direction,
                      "one-ring injector motion should keep every source moving together");

        cubey::projects::fluid_2d::Fluid2DConfig two_ring_config = configured;
        two_ring_config.injector_motion =
            cubey::projects::fluid_2d::Fluid2DInjectorMotion::TwoRings;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> two_rings =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(two_ring_config);
        require_close(two_rings[0].orbit_radius, two_rings[3].orbit_radius,
                      "two-rings injector motion should split the first half onto the outer ring");
        require_close(two_rings[4].orbit_radius, two_rings[7].orbit_radius,
                      "two-rings injector motion should split the second half onto the inner ring");
        require(two_rings[4].orbit_radius < two_rings[0].orbit_radius,
                "two-rings injector motion should put the second half on the inner ring");
        require(two_rings[0].orbit_radius - two_rings[4].orbit_radius > 0.18F,
                "two-rings injector motion should leave clear distance between rings");
        require(two_rings[4].orbit_direction < 0.0F,
                "two-rings injector motion should counter-rotate inner sources");

        cubey::projects::fluid_2d::Fluid2DConfig same_orbit_config = configured;
        same_orbit_config.injector_motion =
            cubey::projects::fluid_2d::Fluid2DInjectorMotion::SameDirectionOrbits;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> same_orbits =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(same_orbit_config);
        require(same_orbits[0].orbit_direction > 0.0F && same_orbits[1].orbit_direction > 0.0F,
                "same-direction orbit motion should keep every source orbiting together");
        require(same_orbits.front().orbit_radius >= 0.14F &&
                    same_orbits.back().orbit_radius <= 0.36F,
                "same-direction orbit motion should keep radii inside the varied orbit band");
        for (std::size_t index = 1; index < same_orbits.size(); ++index) {
            const float radius_gap =
                same_orbits[index].orbit_radius - same_orbits[index - 1].orbit_radius;
            require(radius_gap > 0.015F,
                    "same-direction orbit motion should avoid clustered orbit radii");
            require(radius_gap < 0.040F,
                    "same-direction orbit motion should only add small radius jitter");
        }

        cubey::projects::fluid_2d::Fluid2DConfig alternating_orbit_config = configured;
        alternating_orbit_config.injector_motion =
            cubey::projects::fluid_2d::Fluid2DInjectorMotion::AlternatingDirectionOrbits;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> alternating_orbits =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(alternating_orbit_config);
        require(alternating_orbits[0].orbit_direction > 0.0F &&
                    alternating_orbits[1].orbit_direction < 0.0F,
                "alternating orbit motion should flip direction on alternating sources");
        require_close(alternating_orbits[0].orbit_radius, same_orbits[0].orbit_radius,
                      "same and alternating orbit modes should share the same radius band");
        require_close(alternating_orbits[7].orbit_radius, same_orbits[7].orbit_radius,
                      "same and alternating orbit modes should share the same outer radius band");

        cubey::projects::fluid_2d::Fluid2DConfig fast_ring_config = two_ring_config;
        fast_ring_config.injector_speed = 2.0F;
        const std::vector<cubey::projects::fluid_2d::Fluid2DInjectorState> fast_two_rings =
            cubey::projects::fluid_2d::create_fluid_2d_injectors(fast_ring_config);
        require(length_squared(fast_two_rings[0].velocity) >
                    length_squared(two_rings[0].velocity),
                "injector speed should increase initial source velocity");

        require(cubey::projects::fluid_2d::headless_frame_count(run_config) == 120,
                "headless frame count should default to 120 frames");
        run_config.frames = 8;
        require(cubey::projects::fluid_2d::headless_frame_count(run_config) == 8,
                "headless frame count should honor --frames");

        const cubey::FrameTiming timing =
            cubey::projects::fluid_2d::fixed_headless_timing(config, 5);
        require(timing.frame_index == 5, "fixed headless timing should preserve frame index");
        require(timing.delta_seconds == config.fixed_delta_seconds,
                "fixed headless timing should use fixed simulation delta");
        require(timing.elapsed_seconds == config.fixed_delta_seconds * 5.0,
                "fixed headless timing should use deterministic elapsed time");

        const std::filesystem::path source_root{CUBEY_FLUID_2D_SOURCE_DIR};
        const std::string inject_shader =
            read_text_file(source_root / "shaders/fluid_2d_inject.comp");
        require_contains(inject_shader, "cell.velocity.xy += force * source;",
                         "fluid injector force should scale velocity injection");
        require_not_contains(inject_shader, "force * splat * source_active * dt",
                             "fluid injector force should not bypass injection strength");

    } catch (const std::exception& error) {
        std::fprintf(stderr, "fluid_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
