#include "../sim/fluid_25d/fluid_25d_gpu_resources.h"
#include "../sim/pyro_3d/pyro_3d_gpu_resources.h"
#include "../sim/smoke_2d/smoke_2d_gpu_resources.h"
#include "../sim/water_2d/water_2d_gpu_resources.h"
#include "../sim/water_3d/water_3d_gpu_resources.h"

#include <cubey/engine/upload_queue.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/generated_ibl.h>

#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
namespace smoke = cubey::projects::fluid::smoke_2d;
namespace water2 = cubey::projects::fluid::water_2d;
namespace water3 = cubey::projects::fluid::water_3d;
namespace pyro = cubey::projects::fluid::pyro_3d;
namespace fluid = cubey::projects::fluid::fluid_25d;

template <typename Function> void must_fail(Function&& function) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    if (!failed)
        throw std::runtime_error("expected controlled resource failure");
}

void shader_links(const std::filesystem::path& source, const std::filesystem::path& target) {
    std::filesystem::create_directories(target);
    for (const auto& file : std::filesystem::directory_iterator(source)) {
        if (file.path().extension() != ".spv")
            continue;
        const auto link = target / file.path().filename();
        std::filesystem::remove(link);
        std::filesystem::create_symlink(file.path(), link);
    }
}

struct HiddenShader {
    std::filesystem::path path, hidden;
    explicit HiddenShader(std::filesystem::path value)
        : path(std::move(value)), hidden(path.string() + ".hidden") {
        std::filesystem::rename(path, hidden);
    }
    ~HiddenShader() {
        std::error_code error;
        std::filesystem::rename(hidden, path, error);
    }
};

template <typename Resources, typename Create, typename GlobalProbe, typename Identity,
          typename Target, typename TargetProbe>
void exercise(const char* name, const std::filesystem::path& shaders, Create create,
              GlobalProbe global_probe, Identity identity, Target target, TargetProbe target_probe,
              std::initializer_list<const char*> global_failures,
              std::initializer_list<const char*> target_failures) {
    for (const unsigned slots : {1U, 2U}) {
        Resources resources;
        must_fail([&] { target(resources, VkExtent2D{32, 24}); });
        for (const auto* shader : global_failures) {
            resources.destroy_all_resources();
            {
                HiddenShader hidden(shaders / shader);
                must_fail([&] { create(resources, slots); });
                must_fail([&] { static_cast<void>(identity(resources)); });
            }
            create(resources, slots);
            global_probe(resources);
        }
        const auto original = identity(resources);
        create(resources, slots);
        if (identity(resources) != original)
            throw std::runtime_error("complete globals were unexpectedly recreated");
        must_fail([&] { create(resources, 0); });
        must_fail([&] { create(resources, slots + 1); });
        if (identity(resources) != original)
            throw std::runtime_error("invalid shape changed live globals");
        target(resources, {32, 24});
        target_probe(resources);
        must_fail([&] { target(resources, VkExtent2D{}); });
        target_probe(resources);
        for (const auto* shader : target_failures) {
            {
                HiddenShader hidden(shaders / shader);
                must_fail([&] { target(resources, VkExtent2D{48, 32}); });
                must_fail([&] { target_probe(resources); });
            }
            if (identity(resources) != original)
                throw std::runtime_error("target failure destroyed simulation globals");
            target(resources, {48, 32});
            target_probe(resources);
        }
        resources.destroy_swapchain_resources();
        resources.destroy_swapchain_resources();
        target(resources, {40, 24});
        target_probe(resources);
        resources.destroy_all_resources();
        resources.destroy_all_resources();
        must_fail([&] { static_cast<void>(identity(resources)); });
        create(resources, slots);
        target(resources, {32, 24});
        global_probe(resources);
        target_probe(resources);
        resources.destroy_all_resources();
        std::cout << "PASS " << name << " slots=" << slots
                  << " failure/retry/shape/recreate/re-enable\n";
    }
}

void run(cubey::host::HeadlessPngContext& context) {
    cubey::UploadQueue uploads;
    cubey::ProjectGpuServices gpu(context.gpu(), uploads);
    auto& device = context.device();
    auto environment = cubey::render::create_generated_pbr_environment(device, context.gpu());
    const auto bindings = cubey::render::pbr_environment_texture_bindings(environment);
    const water3::Water3DEnvironmentTextureBindings water_environment{
        .pbr = bindings,
        .display_sampler = bindings.prefiltered_sampler,
        .display_view = bindings.prefiltered_view};
    smoke::Smoke2DConfig smoke_config;
    smoke_config.grid_width = smoke_config.grid_height = 16;
    exercise<smoke::Smoke2DGpuResources>(
        "Smoke 2D", CUBEY_SMOKE_2D_SHADER_DIR,
        [&](auto& r, unsigned slots) {
            r.create_global_resources_if_needed(device, gpu, smoke_config, slots);
        },
        [](auto& r) { static_cast<void>(r.projection_pipeline_resource()); },
        [](auto& r) { return r.field_a().handle(); },
        [&](auto& r, VkExtent2D extent) {
            r.create_render_pipeline(device, VK_FORMAT_R8G8B8A8_UNORM, extent);
        },
        [](auto& r) { static_cast<void>(r.render_pipeline_resource()); },
        {"smoke_2d_inject.comp.spv", "smoke_2d_advect_correct.comp.spv",
         "smoke_2d_projection.comp.spv"},
        {"smoke_2d_render.frag.spv"});
    water2::Water2DConfig water2_config;
    water2_config.grid_width = water2_config.grid_height = 16;
    water2_config.active_particle_count = water2_config.initial_particle_capacity = 8;
    water2_config.particle_capacity = 16;
    exercise<water2::Water2DGpuResources>(
        "Water 2D", CUBEY_WATER_2D_SHADER_DIR,
        [&](auto& r, unsigned slots) {
            r.create_global_resources_if_needed(device, gpu, water2_config, slots);
        },
        [](auto& r) { static_cast<void>(r.diagnostics_pipeline_resource()); },
        [](auto& r) { return r.particle_positions().handle(); },
        [&](auto& r, VkExtent2D extent) {
            r.create_render_pipeline(device, VK_FORMAT_R8G8B8A8_UNORM, extent);
        },
        [](auto& r) { static_cast<void>(r.surface_composite_pipeline_resource()); },
        {"water_2d_reset.comp.spv", "water_2d_force.comp.spv", "water_2d_diagnostics.comp.spv"},
        {"water_2d_render.frag.spv", "water_2d_surface_composite.frag.spv"});
    water3::Water3DConfig water3_config;
    water3_config.grid_width = water3_config.grid_height = water3_config.grid_depth = 16;
    water3_config.active_particle_count = water3_config.initial_particle_capacity = 8;
    water3_config.particle_capacity = water3_config.whitewater_capacity = 16;
    exercise<water3::Water3DGpuResources>(
        "Water 3D", CUBEY_WATER_3D_SHADER_DIR,
        [&](auto& r, unsigned slots) {
            r.create_global_resources_if_needed(device, context.gpu(), gpu, water3_config, slots);
        },
        [](auto& r) { static_cast<void>(r.diagnostics_pipeline_resource()); },
        [](auto& r) { return r.particle_positions().handle(); },
        [&](auto& r, VkExtent2D extent) {
            r.create_render_pipeline(device, VK_FORMAT_R8G8B8A8_UNORM, extent, water_environment);
        },
        [](auto& r) { static_cast<void>(r.surface_composite_pipeline_resource()); },
        {"water_3d_reset.comp.spv", "water_3d_pressure.comp.spv", "water_3d_diagnostics.comp.spv"},
        {"water_3d_scene.frag.spv", "water_3d_surface_composite.frag.spv"});
    for (const auto mode : {pyro::Pyro3DMode::Fire, pyro::Pyro3DMode::Explosion}) {
        pyro::Pyro3DConfig config;
        config.mode = mode;
        config.grid_width = config.grid_height = config.grid_depth = 4;
        config.shadow_grid_width = config.shadow_grid_height = config.shadow_grid_depth = 4;
        config.source_count = mode == pyro::Pyro3DMode::Fire ? 1U : 2U;
        exercise<pyro::Pyro3DGpuResources>(
            pyro::pyro_3d_mode_name(mode), CUBEY_PYRO_3D_SHADER_DIR,
            [&](auto& r, unsigned slots) {
                r.create_global_resources_if_needed(device, context.gpu(), gpu, config, slots);
            },
            [](auto& r) { static_cast<void>(r.shadow_pipeline()); },
            [](auto& r) { return r.density_a().image().handle(); },
            [&](auto& r, VkExtent2D extent) {
                r.create_render_pipeline(device, VK_FORMAT_R8G8B8A8_UNORM, extent, std::nullopt);
            },
            [](auto& r) { static_cast<void>(r.render_pipeline()); },
            {"pyro_3d_reset.comp.spv", "pyro_3d_combust.comp.spv", "pyro_3d_shadow.comp.spv"},
            {"pyro_3d_raymarch.frag.spv"});
    }
    for (unsigned mode = 0; mode < 3; ++mode) {
        fluid::Fluid25DConfig config;
        config.grid_width = config.grid_height = 8;
        config.solver =
            mode == 1 ? fluid::Fluid25DSolver::FiniteVolume : fluid::Fluid25DSolver::VirtualPipes;
        auto scenario = fluid::make_fluid_25d_scenario(config.scenario, 8, 8, config.cell_size_m);
        exercise<fluid::Fluid25DGpuResources>(
            mode == 2   ? "Fluid replay"
            : mode == 1 ? "Fluid finite volume"
                        : "Fluid virtual pipes",
            CUBEY_FLUID_25D_SHADER_DIR,
            [&](auto& r, unsigned slots) {
                r.create_global_resources_if_needed(device, gpu, config, scenario, slots, mode == 2,
                                                    mode == 2 ? 2U : 0U);
            },
            [](auto& r) { static_cast<void>(r.quiver_update_pipeline()); },
            [](auto& r) { return r.terrain().handle(); },
            [&](auto& r, VkExtent2D extent) {
                r.create_render_pipelines(device, VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_D32_SFLOAT,
                                          extent, mode == 2);
            },
            [](auto& r) { static_cast<void>(r.quiver_pipeline()); },
            {mode == 2   ? "fluid_25d_presentation_cue_reset.comp.spv"
             : mode == 1 ? "fluid_25d_fv_reset.comp.spv"
                         : "fluid_25d_reset.comp.spv",
             "fluid_25d_presentation_cue_advect.comp.spv", "fluid_25d_quiver_update.comp.spv"},
            {"fluid_25d_render.frag.spv", "fluid_25d_quiver.frag.spv"});
    }
}
} // namespace

int main() {
    try {
        shader_links(CUBEY_TEST_SMOKE_2D_SOURCE_SHADER_DIR, CUBEY_SMOKE_2D_SHADER_DIR);
        shader_links(CUBEY_TEST_WATER_2D_SOURCE_SHADER_DIR, CUBEY_WATER_2D_SHADER_DIR);
        shader_links(CUBEY_TEST_WATER_3D_SOURCE_SHADER_DIR, CUBEY_WATER_3D_SHADER_DIR);
        shader_links(CUBEY_TEST_PYRO_3D_SOURCE_SHADER_DIR, CUBEY_PYRO_3D_SHADER_DIR);
        shader_links(CUBEY_TEST_FLUID_25D_SOURCE_SHADER_DIR, CUBEY_FLUID_25D_SHADER_DIR);
        for (const auto mode : {cubey::vulkan::GpuRuntimeExecutionMode::Inline,
                                cubey::vulkan::GpuRuntimeExecutionMode::Threaded}) {
            cubey::host::CommonRunConfig config;
            config.headless = config.headless_render_only = true;
            config.require_validation = true;
            config.width = config.height = 32;
            config.frames = 1;
            cubey::host::HeadlessPngHostCallbacks callbacks;
            callbacks.create_resources = run;
            callbacks.record_frame = [](auto&, const auto&, VkCommandBuffer commands,
                                        const auto& target) {
                cubey::render::RenderClearValues values;
                const cubey::render::RenderTargetRenderingInfo info(
                    cubey::render::render_target_view(target), values);
                vkCmdBeginRendering(commands, &info.info());
                vkCmdEndRendering(commands);
            };
            cubey::host::HeadlessPngHost host(
                {.run_config = config,
                 .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                 .require_tessellation_shader = true,
                 .gpu_execution_mode = mode},
                callbacks);
            if (host.run() != 0)
                return 1;
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
