#include <chrono>
#include <cubey/engine/terrain_water_capture_runtime.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <iostream>

int main() {
    std::filesystem::path temporary;
    try {
        using namespace cubey;
        const auto source_path =
            std::filesystem::path(CUBEY_TEST_ASSET_DIR) / "terrain/backdrop-smoke/heightfield.json";
        asset::TerrainRasterHeightSource source(source_path);
        asset::TerrainWaterCapture c;
        c.grid = {5U, 5U, 128.0F, {4096.0F, 0.0F}};
        c.source_elevation_sha256 = source.provenance().elevation_sha256;
        c.capture_time_s = 100.0;
        c.rain_mm_per_hour = 512.0;
        c.recording_sha256 = std::string(64, 'a');
        c.frame_sha256 = std::string(64, 'b');
        for (unsigned z = 0; z < 5U; ++z)
            for (unsigned x = 0; x < 5U; ++x) {
                c.bed_m.push_back(source.sample_height(
                    {.world_xz = c.grid.sample_origin_xz_m +
                                 math::Vec2{float(x), float(z)} * c.grid.cell_size_m}));
                c.depth_m.push_back(2.0F);
                c.velocity_xz_m_per_s.push_back({1.0F, 0.0F});
            }
        temporary = std::filesystem::temp_directory_path() /
                    ("cubey-water-lifecycle-" +
                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        asset::write_terrain_water_capture(temporary, c);
        vulkan::Instance instance({.application_name = "captured water lifecycle",
                                   .application_version = 0U,
                                   .required_extensions = {},
                                   .validation = true,
                                   .require_validation = false});
        vulkan::Device device(instance,
                              {.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                               .require_present = false,
                               .require_dynamic_rendering = true});
        vulkan::SubmissionCoordinator submission(device);
        vulkan::GpuRuntime gpu(device, submission);
        TerrainWaterCaptureRuntime runtime(temporary, source_path,
                                           CUBEY_TERRAIN_CAPTURE_TEST_SHADER_DIR);
        for (unsigned size : {128U, 192U, 128U}) {
            runtime.destroy_target_resources(); // No outstanding commands: retired synchronously
                                                // below.
            const VkExtent2D extent{size, size};
            vulkan::Image image(
                device, vulkan::color_render_target_image_config(extent, VK_FORMAT_R8G8B8A8_UNORM));
            const auto target =
                render::color_target_view(extent, image.format(), image.handle(), image.view());
            runtime.ensure_resources(device, gpu, 2U, target);
            const Camera3D camera({.fovy_radians = 1.0F, .near_z = 1.0F, .far_z = 5000.0F});
            const auto transform = orbit_camera_transform({.target = {0.0F, c.bed_m[12], 0.0F},
                                                           .distance = 700.0F,
                                                           .yaw = 0.0F,
                                                           .pitch = -0.8F});
            const render::TerrainSurfacePatchCamera view{
                camera.view_projection_matrix(transform, 1.0F), transform.translation};
            for (unsigned frame = 0; frame < 6U; ++frame) {
                const auto slot = frame % 2U;
                (void)runtime.collect_timings(slot);
                vulkan::ImmediateCommands commands(device, submission);
                const VkImageMemoryBarrier barrier{
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .pNext = nullptr,
                    .srcAccessMask = 0U,
                    .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                    .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                    .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image.handle(),
                    .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0U, 1U, 0U, 1U}};
                vkCmdPipelineBarrier(commands.command_buffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0U, 0U, nullptr,
                                     0U, nullptr, 1U, &barrier);
                // Water on/off, then held animation clock, then resumed. No field re-upload.
                const double clock = frame < 2U   ? double(frame)
                                     : frame < 4U ? 1.0
                                                  : double(frame) - 2.0;
                runtime.record(device, commands.command_buffer(), {slot, 2U}, target, false, view,
                               clock, frame != 2U, true);
                commands.submit_and_wait();
            }
            runtime.verify_immutable_fields(gpu);
        }
        runtime.destroy_target_resources();
        runtime.destroy();
        std::filesystem::remove_all(temporary);
        std::cout << "terrain_water_capture_lifecycle: PASS two retired slots; water on/off, "
                     "held/resumed clock, resize 128/192/128, immutable inputs and teardown\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        if (!temporary.empty())
            std::filesystem::remove_all(temporary);
        if (std::string_view(e.what()).find("no Vulkan") != std::string_view::npos)
            return 77;
        return 1;
    }
}
