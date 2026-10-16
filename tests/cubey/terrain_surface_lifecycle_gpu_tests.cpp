#include <cubey/engine/terrain_surface_patch_scene.h>
#include <cubey/render/material_instance.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/vk_check.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace cubey;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class ShaderFiles {
  public:
    ShaderFiles()
        : path(std::filesystem::temp_directory_path() /
               ("cubey-terrain-lifecycle-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directory(path);
        for (const auto& file :
             std::filesystem::directory_iterator(CUBEY_TERRAIN_CAPTURE_TEST_SHADER_DIR))
            if (file.path().extension() == ".spv")
                restore(file.path().filename());
    }
    ~ShaderFiles() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    void hide(const std::filesystem::path& name) {
        std::filesystem::remove(path / name);
    }
    void restore(const std::filesystem::path& name) {
        std::filesystem::create_symlink(
            std::filesystem::path(CUBEY_TERRAIN_CAPTURE_TEST_SHADER_DIR) / name, path / name);
    }
    std::filesystem::path path;
};

void lifecycle(const vulkan::Device& device, vulkan::SubmissionCoordinator& submission,
               vulkan::GpuRuntime& gpu, unsigned slots) {
    ShaderFiles shaders;
    TerrainSurfacePatchScene scene(shaders.path);
    const std::array<float, 4> bed{};
    const std::array<float, 4> depth{0.1F, 0.1F, 0.1F, 0.1F};
    const std::array<math::Vec4, 4> velocity{};
    const std::array<float, 5> coverage{1, 1, 0, 0, 1};
    const auto upload = [&](const auto& data) {
        return vulkan::upload_device_buffer(gpu, data.data(), sizeof(data),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    };
    auto bed_buffer = upload(bed), depth_buffer = upload(depth), velocity_buffer = upload(velocity),
         coverage_buffer = upload(coverage);
    std::vector<vulkan::DescriptorSetBindingConfig> bindings;
    for (unsigned i = 0; i <= 8; ++i)
        bindings.push_back(
            {.binding = i,
             .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
             .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT});
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    render::MaterialDescriptorWriter writer(descriptors.set());
    for (unsigned i = 0; i <= 8; ++i) {
        const auto& buffer = i == 1   ? depth_buffer
                             : i == 2 ? velocity_buffer
                             : i == 8 ? coverage_buffer
                                      : bed_buffer;
        writer.storage_buffer(i, buffer.handle(), buffer.size());
    }
    writer.update(device);
    const render::TerrainSurfacePatchFields fields{bed_buffer, depth_buffer, velocity_buffer,
                                                   coverage_buffer, descriptors.set()};
    const render::TerrainSurfacePatchGrid grid{.grid_width = 2, .grid_height = 2};
    vulkan::Image output(
        device, vulkan::color_render_target_image_config({32, 24}, VK_FORMAT_R8G8B8A8_UNORM));
    const auto target =
        render::color_target_view({32, 24}, output.format(), output.handle(), output.view());
    const auto ensure = [&](render::ColorTargetView view, bool daylight = false,
                            const render::TerrainSurfacePatchSurface* surface = nullptr) {
        return scene.ensure_resources(device, gpu, slots, view, grid, bed, fields, false, surface,
                                      daylight);
    };
    {
        render::RenderGraphFrameExecutor uncreated(slots);
        bool rejected = false;
        try {
            scene.record(device, VK_NULL_HANDLE, uncreated, {0, slots}, target, false, fields,
                         grid, {}, {}, 0.0, render::terrain_surface_patch_macro_material());
        } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("not initialized") != std::string::npos;
        }
        require(rejected, "terrain recording accepted an uncreated target");
    }
    const auto reject = [&](const char* shader, auto&& create) {
        shaders.hide(shader);
        bool failed = false;
        try {
            create();
        } catch (const std::exception& error) {
            failed = std::string(error.what()).find(shader) != std::string::npos;
        }
        shaders.restore(shader);
        require(failed, "missing terrain shader did not produce the expected creation failure");
    };
    for (const auto* shader :
         {"terrain_backdrop_material.comp.spv", "fluid_25d_rain_visual.frag.spv",
          "fluid_25d_scenic_display.frag.spv", "fluid_25d_daylight.comp.spv"}) {
        const bool daylight = std::string_view(shader) == "fluid_25d_daylight.comp.spv";
        if (!daylight)
            scene.destroy_swapchain_resources();
        reject(shader, [&] { static_cast<void>(ensure(target, daylight)); });
        require(ensure(target, daylight),
                "terrain retry falsely reported a partially built target as ready");
        require(!ensure(target, daylight), "completed terrain resources did not remain reusable");
    }
    require(!ensure(target), "completed terrain target did not remain reusable");
    render::RenderGraphFrameExecutor executor(slots);
    vulkan::CommandPool pool(device, {.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT});
    std::vector<VkCommandBuffer> commands;
    std::vector<vulkan::Image> outputs;
    for (unsigned slot = 0; slot < slots; ++slot) {
        commands.push_back(pool.allocate_primary());
        outputs.emplace_back(
            device, vulkan::color_render_target_image_config(target.extent, target.format));
    }
    const auto record_frames = [&] {
        for (unsigned slot = 0; slot < slots; ++slot) {
            const auto cmd = commands[slot];
            vulkan::check(vkResetCommandBuffer(cmd, 0), "reset terrain lifecycle frame");
            vulkan::begin_command_buffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
            const auto view = render::color_target_view(
                target.extent, target.format, outputs[slot].handle(), outputs[slot].view());
            vulkan::transition_image_layout(cmd,
                                            vulkan::begin_color_attachment_transition(view.image));
            scene.record(device, cmd, executor, {slot, slots}, view, false, fields, grid,
                         {.position = {0, 5, 0}}, {}, 1.0,
                         render::terrain_surface_patch_macro_material());
            vulkan::end_command_buffer(cmd, "terrain lifecycle frame");
        }
        static_cast<void>(submission.submit_and_wait({.command_buffers = commands},
                                                     "terrain lifecycle slots",
                                                     "wait terrain lifecycle slots"));
    };
    record_frames();
    reject("fluid_25d_scenic_display.frag.spv", [&] {
        auto resized = target;
        resized.extent = {48, 32};
        static_cast<void>(ensure(resized));
    });
    require(!ensure(target), "failed target replacement discarded the previous complete target");
    record_frames();
    executor.clear();
    scene.destroy_swapchain_resources();
    scene.destroy_swapchain_resources();
    auto resized = target;
    resized.extent = {48, 32};
    require(ensure(resized), "terrain target recreation did not finish");
    scene.destroy();
    const std::array<math::Vec4, 1> invalid_samples{};
    const render::TerrainSurfacePatchSurface invalid{2, 2, invalid_samples, {}, {}};
    bool rejected = false;
    try {
        static_cast<void>(ensure(target, false, &invalid));
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    require(rejected, "terrain accepted inconsistent surface dimensions");
    require(ensure(target), "terrain failed to re-enable after full shutdown");
    scene.destroy();
    std::cout << "terrain surface lifecycle PASS: " << slots
              << " slots, early/middle/late/daylight failure retry, retained target, "
                 "record/recreate/restart\n";
}
} // namespace

int main() {
    try {
        vulkan::InstanceConfig config;
        config.application_name = "terrain lifecycle";
        config.require_validation = true;
        vulkan::Instance instance(config);
        vulkan::Device device(instance,
                              {.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                               .require_present = false,
                               .require_dynamic_rendering = true});
        vulkan::SubmissionCoordinator submission(device);
        vulkan::GpuRuntime gpu({.device = &device,
                                .submission = &submission,
                                .execution_mode = vulkan::GpuRuntimeExecutionMode::Inline});
        lifecycle(device, submission, gpu, 1);
        lifecycle(device, submission, gpu, 2);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
