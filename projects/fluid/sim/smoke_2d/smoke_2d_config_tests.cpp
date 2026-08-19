#include "smoke_2d_config.h"
#include "smoke_2d_injectors.h"
#include "../../smoke_2d/smoke_2d_project_config.h"

#include <cubey/core/frame_clock.h>

#include <array>
#include <cmath>
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

[[nodiscard]] cubey::projects::fluid::smoke_2d::Smoke2DProjectConfig parse_project(
    std::vector<std::string> arguments) {
    std::vector<char*> argv;
    argv.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    return cubey::projects::fluid::smoke_2d::parse_smoke_2d_project_config(
        static_cast<int>(argv.size()), argv.data());
}

[[nodiscard]] float length_squared(std::array<float, 2> value) {
    return (value[0] * value[0]) + (value[1] * value[1]);
}

} // namespace

int main() {
    try {
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig config;
        constexpr std::size_t kExpectedCellCount = std::size_t{1024} * std::size_t{1024};
        require(config.grid_width == 1024, "smoke grid should default to 1024 columns");
        require(config.grid_height == 1024, "smoke grid should default to 1024 rows");
        require(config.procedural_injector_count == 5,
                "smoke should default to five procedural injectors");
        require(cubey::projects::fluid::smoke_2d::field_cell_count(config) == kExpectedCellCount,
                "field cell count should multiply dimensions");
        require(cubey::projects::fluid::smoke_2d::field_byte_size(config) ==
                    sizeof(cubey::projects::fluid::smoke_2d::SmokeCellGpu) * kExpectedCellCount,
                "field byte size should cover one cell per grid location");
        require(config.pressure_iterations == 40,
                "fluid pressure solve should default to 40 Jacobi iterations");
        require(config.pressure_solver ==
                    cubey::projects::fluid::smoke_2d::Smoke2DPressureSolver::Jacobi,
                "fluid pressure solve should default to Jacobi");
        require(config.dye_decay_per_second == 0.992F,
                "fluid dye decay should default to controlled linger");
        require(config.velocity_decay_per_second == 0.996F,
                "fluid velocity decay should default to controlled linger");
        require(config.injector_injection_radius == 0.026F,
                "smoke injector radius should be tuned for sharper moving sources");
        require(config.injector_injection_strength == 7.2F,
                "smoke injector strength should default to a visible multi-source impulse");
        require(config.injector_propulsion_strength == 1.35F,
                "smoke injector propulsion should default to a visible pushback");
        require(config.injector_orbit_radius == 0.26F,
                "smoke injector orbit radius should default near the middle of the field");
        require(config.injector_orbit_radius_spread == 0.28F,
                "smoke injector orbit radius spread should default to a broad band");
        require(config.injector_orbit_angular_speed == 0.0F,
                "smoke injector orbit speed should default to a centered signed band");
        require(config.injector_orbit_angular_speed_spread == 1.1F,
                "smoke injector orbit speed spread should default to mixed directions");
        require(config.injector_orbit_phase_spread == 1.0F,
                "smoke injector orbit phase spread should default around a full turn");
        require(config.vorticity_strength == 22.0F,
                "fluid vorticity strength should have a visible default");
        require(config.advection_strength == 0.18F,
                "smoke advection strength should default to the tuned shader scale");
        require(config.low_energy_cleanup_strength == 0.12F,
                "smoke low-energy cleanup should default to the tuned artifact suppression");
        require(config.low_energy_cleanup_start == 0.030F && config.low_energy_cleanup_end == 0.20F,
                "smoke low-energy cleanup should expose the tuned residual range");
        require(!config.profile_diagnostics && config.profile_diagnostic_interval == 1U,
                "smoke diagnostics should be opt-in with per-frame sampling by default");
        require(cubey::projects::fluid::smoke_2d::scalar_field_byte_size(config) ==
                    sizeof(float) * kExpectedCellCount,
                "scalar field byte size should cover one float per grid location");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Dye) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Velocity,
                "debug view should cycle from dye to velocity");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Velocity) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Divergence,
                "debug view should cycle from velocity to divergence");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Divergence) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Pressure,
                "debug view should cycle from divergence to pressure");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Pressure) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Speed,
                "debug view should cycle from pressure to speed");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Speed) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Vorticity,
                "debug view should cycle from speed to vorticity");
        require(cubey::projects::fluid::smoke_2d::next_debug_view(
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Vorticity) ==
                    cubey::projects::fluid::smoke_2d::Smoke2DDebugView::Dye,
                "debug view should cycle from vorticity to dye");
        require(cubey::projects::fluid::smoke_2d::smoke_2d_pressure_solver_from_name("rbgs") ==
                    cubey::projects::fluid::smoke_2d::Smoke2DPressureSolver::RedBlackGaussSeidel,
                "pressure solver parser should accept rbgs");

        const cubey::projects::fluid::smoke_2d::Smoke2DProjectConfig default_project;
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig default_from_options =
            cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                default_project.grid, default_project.smoke, default_project.common);
        require(default_from_options.grid_width == config.grid_width,
                "default project config should preserve smoke grid width");
        require(default_from_options.grid_height == config.grid_height,
                "default project config should preserve smoke grid height");
        require(default_from_options.procedural_injector_count ==
                    config.procedural_injector_count,
                "default project config should preserve smoke injector count");
        require(default_from_options.injector_injection_strength ==
                    config.injector_injection_strength,
                "default project config should preserve smoke injector strength");
        require(default_from_options.pressure_iterations == config.pressure_iterations,
                "default project config should preserve smoke pressure iterations");
        require(default_from_options.pressure_solver == config.pressure_solver,
                "default project config should preserve smoke pressure solver");
        require(default_from_options.dye_decay_per_second == config.dye_decay_per_second,
                "default project config should preserve smoke dye decay");
        require(default_from_options.velocity_decay_per_second ==
                    config.velocity_decay_per_second,
                "default project config should preserve smoke velocity decay");
        require(default_from_options.injector_injection_radius ==
                    config.injector_injection_radius,
                "default project config should preserve smoke injector radius");
        require(default_from_options.injector_orbit_radius == config.injector_orbit_radius,
                "default project config should preserve smoke orbit radius");
        require(default_from_options.vorticity_strength == config.vorticity_strength,
                "default project config should preserve smoke vorticity");
        require(default_from_options.advection_strength == config.advection_strength,
                "default project config should preserve smoke advection strength");
        require(default_from_options.low_energy_cleanup_strength ==
                    config.low_energy_cleanup_strength,
                "default project config should preserve smoke cleanup strength");
        std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> default_injectors =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(config);
        require(default_injectors.size() == 5,
                "default smoke injector state should match the default injector count");
        for (const cubey::projects::fluid::smoke_2d::Smoke2DInjectorState& injector :
             default_injectors) {
            require(std::abs(injector.angular_speed) > 0.001F,
                    "default smoke injector speed spread should not leave an injector static");
        }

        cubey::projects::fluid::smoke_2d::Smoke2DProjectConfig project_config;
        project_config.grid.width = 1024U;
        project_config.grid.height = 768U;
        project_config.smoke.injectors = 8U;
        project_config.smoke.pressure_iterations = 48U;
        project_config.smoke.pressure_solver = "rbgs";
        project_config.smoke.dye_decay = 0.985F;
        project_config.smoke.velocity_decay = 0.991F;
        project_config.smoke.injector_radius = 0.041F;
        project_config.smoke.injector_force = 7.5F;
        project_config.smoke.injector_propulsion = 1.6F;
        project_config.smoke.injector_orbit_radius = 0.24F;
        project_config.smoke.injector_orbit_radius_spread = 0.18F;
        project_config.smoke.injector_orbit_angular_speed = 0.1F;
        project_config.smoke.injector_orbit_angular_speed_spread = 1.2F;
        project_config.smoke.injector_orbit_phase_spread = 0.75F;
        project_config.smoke.vorticity = 24.0F;
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig configured =
            cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                project_config.grid, project_config.smoke, project_config.common);
        require(configured.grid_width == 1024, "smoke config should honor project grid width");
        require(configured.grid_height == 768, "smoke config should honor project grid height");
        require(configured.procedural_injector_count == 8,
                "smoke config should honor project injector count");
        require(configured.pressure_iterations == 48,
                "smoke config should honor project pressure iterations");
        require(configured.pressure_solver ==
                    cubey::projects::fluid::smoke_2d::Smoke2DPressureSolver::RedBlackGaussSeidel,
                "smoke config should honor project pressure solver");
        require(configured.dye_decay_per_second == 0.985F,
                "smoke config should honor project dye decay");
        require(configured.velocity_decay_per_second == 0.991F,
                "smoke config should honor project velocity decay");
        require(configured.injector_injection_radius == 0.041F,
                "smoke config should honor project injector radius");
        require(configured.injector_injection_strength == 7.5F,
                "smoke config should honor project injector force");
        require(configured.injector_propulsion_strength == 1.6F,
                "smoke config should honor project injector propulsion");
        require(configured.injector_orbit_radius == 0.24F,
                "smoke config should honor project injector orbit radius");
        require(configured.injector_orbit_radius_spread == 0.18F,
                "smoke config should honor project injector orbit radius spread");
        require(configured.injector_orbit_angular_speed == 0.1F,
                "smoke config should honor project injector orbit angular speed");
        require(configured.injector_orbit_angular_speed_spread == 1.2F,
                "smoke config should honor project injector orbit angular speed spread");
        require(configured.injector_orbit_phase_spread == 0.75F,
                "smoke config should honor project injector orbit phase spread");
        require(configured.vorticity_strength == 24.0F,
                "smoke config should honor project vorticity");
        project_config.common.headless = true;
        project_config.common.profile_diagnostics = true;
        project_config.common.profile_diagnostic_interval = 7U;
        const cubey::projects::fluid::smoke_2d::Smoke2DConfig diagnostics_config =
            cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                project_config.grid, project_config.smoke, project_config.common);
        require(diagnostics_config.profile_diagnostics &&
                    diagnostics_config.profile_diagnostic_interval == 7U &&
                    diagnostics_config.headless,
                "smoke run-config construction should preserve profile diagnostics flags");
        bool rejected_windowed_diagnostics = false;
        try {
            cubey::host::CommonRunConfig windowed_diagnostics;
            windowed_diagnostics.profile_diagnostics = true;
            cubey::projects::fluid::smoke_2d::Smoke2DStartupOptions options;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                {}, options, windowed_diagnostics));
        } catch (const std::runtime_error&) {
            rejected_windowed_diagnostics = true;
        }
        require(rejected_windowed_diagnostics,
                "smoke profile diagnostics should require headless mode");

        bool threw_for_too_many_injectors = false;
        try {
            cubey::projects::fluid::smoke_2d::Smoke2DStartupOptions options;
            options.injectors =
                cubey::projects::fluid::smoke_2d::kMaxProceduralInjectorCount + 1U;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                {}, options, {}));
        } catch (const std::runtime_error&) {
            threw_for_too_many_injectors = true;
        }
        require(threw_for_too_many_injectors,
                "smoke config should reject injector counts above the shader policy limit");
        bool threw_for_invalid_decay = false;
        try {
            cubey::projects::fluid::smoke_2d::Smoke2DStartupOptions options;
            options.dye_decay = 1.2F;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                {}, options, {}));
        } catch (const std::runtime_error&) {
            threw_for_invalid_decay = true;
        }
        require(threw_for_invalid_decay, "smoke config should reject dye decay above one");
        bool threw_for_invalid_radius = false;
        try {
            cubey::projects::fluid::smoke_2d::Smoke2DStartupOptions options;
            options.injector_radius = 0.0F;
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                {}, options, {}));
        } catch (const std::runtime_error&) {
            threw_for_invalid_radius = true;
        }
        require(threw_for_invalid_radius, "smoke config should reject nonpositive injector radius");
        bool threw_for_invalid_pressure_solver = false;
        try {
            cubey::projects::fluid::smoke_2d::Smoke2DStartupOptions options;
            options.pressure_solver = "sor";
            static_cast<void>(cubey::projects::fluid::smoke_2d::smoke_2d_config_from_options(
                {}, options, {}));
        } catch (const std::runtime_error&) {
            threw_for_invalid_pressure_solver = true;
        }
        require(threw_for_invalid_pressure_solver,
                "smoke config should reject unsupported pressure solvers");

        const std::filesystem::path layered_path =
            std::filesystem::temp_directory_path() / "cubey-smoke-2d-config-test.json";
        const std::filesystem::path template_path =
            std::filesystem::temp_directory_path() / "cubey-smoke-2d-template-test.json";
        {
            std::ofstream file(layered_path);
            file << R"({"grid":{"size":24},"smoke":{"injectors":3}})";
        }
        const auto layered = parse_project({"smoke_2d", "--config", layered_path.string(),
                                            "--smoke-injectors", "5", "--set",
                                            "smoke.injectors=7"});
        require(layered.grid.size == 24U && layered.grid.width == 24U &&
                    layered.grid.height == 24U,
                "smoke grid.size should fan out to both dimensions");
        require(layered.smoke.injectors == 7U,
                "smoke --set should override named flags and config files");
        const auto named = parse_project({"smoke_2d", "--grid-width", "40", "--grid-height",
                                          "32", "--smoke-injectors", "6", "--no-validation"});
        require(named.grid.width == 40U && named.grid.height == 32U &&
                    named.smoke.injectors == 6U,
                "smoke named flags should bind project-owned options");
        require(!named.common.validation && !named.common.require_validation,
                "smoke negative validation alias should preserve host coupling");
        bool rejected_unknown_key = false;
        try {
            static_cast<void>(parse_project({"smoke_2d", "--set", "water2d.wave=true"}));
        } catch (const std::runtime_error&) {
            rejected_unknown_key = true;
        }
        require(rejected_unknown_key, "smoke schema should reject options owned by another target");
        const auto templated = parse_project(
            {"smoke_2d", "--write-config-template", template_path.string()});
        (void)templated;
        const std::string template_json = read_text_file(template_path);
        require_contains(template_json, "\"smoke\"",
                         "smoke template should expose live smoke options");
        require_not_contains(template_json, "water2d",
                             "smoke template should omit water-only options");
        std::error_code cleanup_error;
        std::filesystem::remove(layered_path, cleanup_error);
        std::filesystem::remove(template_path, cleanup_error);

        std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> injectors =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(configured);
        require(injectors.size() == 8, "smoke injector state should match configured count");
        require_close(injectors[0].hue, 0.0F, "first injector hue should start at red");
        require_close(injectors[1].hue, 0.125F, "injector hues should spread evenly");
        require(injectors.front().orbit_radius < injectors.back().orbit_radius,
                "smoke injector orbit radii should spread across the configured band");
        require(injectors.front().orbit_radius >=
                    configured.injector_orbit_radius -
                        (configured.injector_orbit_radius_spread * 0.5F) - 0.01F,
                "smoke injector minimum orbit radius should stay inside the configured band");
        require(injectors.back().orbit_radius <=
                    configured.injector_orbit_radius +
                        (configured.injector_orbit_radius_spread * 0.5F) + 0.01F,
                "smoke injector maximum orbit radius should stay inside the configured band");
        require(injectors.front().angular_speed < configured.injector_orbit_angular_speed &&
                    injectors.back().angular_speed > configured.injector_orbit_angular_speed,
                "smoke injector angular speeds should spread around the configured base speed");
        require(injectors[1].anchor_angle > injectors[0].anchor_angle,
                "smoke injector phases should spread across the configured phase range");
        for (std::size_t index = 1; index < injectors.size(); ++index) {
            const float radius_gap =
                injectors[index].orbit_radius - injectors[index - 1].orbit_radius;
            require(radius_gap > 0.010F,
                    "smoke injector radii should avoid clustering inside the orbit band");
            require(radius_gap < 0.040F,
                    "smoke injector radii should stay close to an even orbit-band spread");
        }
        const std::array<float, 2> initial_position = injectors[0].position;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu> initial_gpu =
            cubey::projects::fluid::smoke_2d::smoke_2d_injectors_to_gpu(injectors, configured);
        require(initial_gpu.size() == 8, "smoke GPU injector state should match state count");
        require(initial_gpu[0].velocity_carry_propulsion[2] > 1.0F,
                "smoke GPU injector should carry source velocity into the fluid");
        require(initial_gpu[0].velocity_carry_propulsion[3] > 0.0F,
                "smoke GPU injector should expose opposite-direction propulsion force");
        cubey::projects::fluid::smoke_2d::Smoke2DConfig no_propulsion_config = configured;
        no_propulsion_config.injector_propulsion_strength = 0.0F;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu> no_propulsion_gpu =
            cubey::projects::fluid::smoke_2d::smoke_2d_injectors_to_gpu(injectors,
                                                                        no_propulsion_config);
        require(no_propulsion_gpu[0].velocity_carry_propulsion[3] == 0.0F,
                "smoke GPU injector should honor disabled propulsion force");
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu> advanced_gpu =
            cubey::projects::fluid::smoke_2d::update_smoke_2d_injectors(
                injectors, configured,
                {
                    .delta_seconds = configured.fixed_delta_seconds,
                    .elapsed_seconds = configured.fixed_delta_seconds,
                    .frame_index = 1,
                });
        require(advanced_gpu.size() == 8, "smoke injector update should keep configured count");
        require(injectors[0].position != initial_position,
                "smoke injector physics should advance source positions");
        require(cubey::projects::fluid::smoke_2d::smoke_2d_injector_byte_size(configured) ==
                    sizeof(cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu) * 8U,
                "smoke injector byte size should cover one GPU record per injector");
        require(cubey::projects::fluid::smoke_2d::smoke_2d_injector_capacity_byte_size() ==
                    sizeof(cubey::projects::fluid::smoke_2d::Smoke2DInjectorGpu) *
                        cubey::projects::fluid::smoke_2d::kMaxProceduralInjectorCount,
                "smoke injector capacity should cover live-editable injector count");

        cubey::projects::fluid::smoke_2d::Smoke2DConfig slow_orbit_config = configured;
        slow_orbit_config.injector_orbit_angular_speed_spread = 0.4F;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> slow_orbits =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(slow_orbit_config);
        cubey::projects::fluid::smoke_2d::Smoke2DConfig fast_orbit_config = configured;
        fast_orbit_config.injector_orbit_angular_speed_spread = 2.0F;
        const std::vector<cubey::projects::fluid::smoke_2d::Smoke2DInjectorState> fast_orbits =
            cubey::projects::fluid::smoke_2d::create_smoke_2d_injectors(fast_orbit_config);
        require(length_squared(fast_orbits.back().velocity) >
                    length_squared(slow_orbits.back().velocity),
                "injector angular speed spread should increase initial source velocity");

        cubey::host::CommonRunConfig common_config;
        require(cubey::projects::fluid::smoke_2d::headless_frame_count(common_config) == 120,
                "headless frame count should default to 120 frames");
        common_config.frames = 8;
        require(cubey::projects::fluid::smoke_2d::headless_frame_count(common_config) == 8,
                "headless frame count should honor --frames");

        const cubey::FrameTiming timing =
            cubey::projects::fluid::smoke_2d::fixed_headless_timing(config, 5);
        require(timing.frame_index == 5, "fixed headless timing should preserve frame index");
        require(timing.delta_seconds == config.fixed_delta_seconds,
                "fixed headless timing should use fixed simulation delta");
        require(timing.elapsed_seconds == config.fixed_delta_seconds * 5.0,
                "fixed headless timing should use deterministic elapsed time");

        const std::filesystem::path source_root{CUBEY_SMOKE_2D_SOURCE_DIR};
        const std::string inject_shader =
            read_text_file(source_root / "shaders/smoke_2d_inject.comp");
        require_contains(inject_shader, "cell.velocity.xy += force * source;",
                         "smoke injector force should scale velocity injection");
        require_not_contains(inject_shader, "force * splat * source_active * dt",
                             "smoke injector force should not bypass injection strength");

        const std::string commands_source = read_text_file(source_root / "smoke_2d_commands.cpp");
        const std::string advect_predict_shader =
            read_text_file(source_root / "shaders/smoke_2d_advect_predict.comp");
        const std::string advect_correct_shader =
            read_text_file(source_root / "shaders/smoke_2d_advect_correct.comp");
        require_contains(advect_predict_shader, "params.tuning_options.x",
                         "smoke advect prediction should use push-constant advection strength");
        require_contains(advect_correct_shader, "params.tuning_options.y",
                         "smoke advect correction should use push-constant cleanup strength");
        require_not_contains(advect_predict_shader, "0.18",
                             "smoke advect prediction should not hardcode advection strength");
        require_contains(commands_source, "\"advect_predict\"",
                         "smoke commands should profile advect prediction");
        require_contains(commands_source, "\"pressure\"",
                         "smoke commands should profile the pressure solve");
        require_contains(commands_source, "pressure_rbgs_pipeline_resource",
                         "smoke commands should support the RBGS pressure path");
        require_contains(commands_source, "\"render\"",
                         "smoke commands should profile the render pass");
        require_contains(commands_source, "GpuTimestampScope",
                         "smoke commands should use GPU timestamp scopes");
        const std::string app_source = read_text_file(source_root / "smoke_2d_app.cpp");
        require_contains(app_source, "record_gpu_timings(context.profile_recorder()",
                         "smoke app should export GPU timings into the profile recorder");
        require_contains(app_source, "AlreadyRecording",
                         "smoke app should own command buffer timing around render graph record");
        require_contains(app_source, "resources_.field_a().handle()",
                         "smoke headless path should read back field diagnostics");
        require_not_contains(app_source, "update_obstacle_mask",
                             "smoke app should not retain removed obstacle-mask updates");
        const std::string diagnostics_source =
            read_text_file(source_root / "smoke_2d_diagnostics.cpp");
        require_contains(diagnostics_source, "smoke_2d.field",
                         "smoke diagnostics readback should export field metrics");
        require_contains(diagnostics_source, "divergence_abs_max",
                         "smoke diagnostics readback should export solver residual metrics");
        const std::string ui_source = read_text_file(source_root / "smoke_2d_ui.cpp");
        require_not_contains(ui_source, "Obstacles",
                             "smoke UI should not expose removed obstacle controls");
        const std::string gpu_resources_source =
            read_text_file(source_root / "smoke_2d_gpu_resources.cpp");
        require_not_contains(gpu_resources_source, "obstacle",
                             "smoke GPU resources should not retain obstacle buffers");
        require_contains(gpu_resources_source, "smoke_2d_pressure_rbgs.comp.spv",
                         "smoke GPU resources should create the RBGS pressure pipeline");
        const std::string pressure_rbgs_shader =
            read_text_file(source_root / "shaders/smoke_2d_pressure_rbgs.comp");
        require_contains(pressure_rbgs_shader, "params.decay_options.z",
                         "RBGS pressure shader should select red/black parity from push constants");

    } catch (const std::exception& error) {
        std::fprintf(stderr, "smoke_2d_config_tests: %s\n", error.what());
        return 1;
    }
}
