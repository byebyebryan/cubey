#include <chrono>
#include <cubey/engine/terrain_water_capture_runtime.h>
#include <cubey/render/generated_ibl.h>
#include <cubey/render/pass.h>
#include <cubey/render/texture.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/vulkan/image.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <cubey/vulkan/vk_check.h>
#include <iostream>

namespace {
void test_pending_external_lighting(const cubey::vulkan::Device& device,
                                    cubey::vulkan::SubmissionCoordinator& submission,
                                    cubey::vulkan::GpuRuntime& gpu,
                                    const std::filesystem::path& capture,
                                    const std::filesystem::path& source, float bed) {
    using namespace cubey;
    const VkExtent2D extent{128, 128};
    const auto generated = render::create_generated_pbr_environment(device, gpu);
    const auto daylight = render::terrain_surface_patch_fixed_daylight();
    const render::TerrainSurfacePatchEnvironment lighting{
        render::pbr_environment_texture_bindings(generated),
        render::atmosphere_environment_lighting(daylight),
        0.0F,
        generated.prefiltered_cube.sampler().handle(),
        generated.prefiltered_cube.view(),
        render::atmosphere_environment_frame_uniforms(daylight, {})};
    render::Texture2D hdr(device, {.extent = extent,
                                   .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                   .usage = render::Texture2DUsage::ColorAttachmentSampled,
                                   .sampler = {}});
    render::DepthTexture depth(device,
                               {.extent = extent, .format = VK_FORMAT_D32_SFLOAT, .sampler = {}});
    const render::TerrainSurfacePatchOpaqueLayer layer{
        render::color_target_view(extent, hdr.format(), hdr.handle(), hdr.view()),
        render::depth_target_view(depth)};
    {
        vulkan::ImmediateCommands init(device, submission);
        const auto cmd = init.command_buffer();
        vulkan::transition_image_layout(cmd,
                                        vulkan::begin_color_attachment_transition(hdr.handle()));
        vulkan::transition_image_layout(cmd,
                                        vulkan::begin_depth_attachment_transition(depth.handle()));
        render::record_render_target_pass(vulkan::CommandRecorder(cmd),
                                          render::render_target_view(layer.color, layer.depth),
                                          {.depth = render::depth_clear_value()},
                                          {.color = vulkan::clear_store_attachment_ops(),
                                           .depth = vulkan::clear_store_attachment_ops()},
                                          [](const auto&) {});
        vulkan::transition_image_layout(
            cmd, {.image = hdr.handle(),
                  .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                  .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                  .src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                  .dst_access_mask = VK_ACCESS_SHADER_READ_BIT,
                  .src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                  .dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
        vulkan::transition_image_layout(
            cmd, vulkan::finish_depth_attachment_for_sampling_transition(depth.handle()));
        init.submit_and_wait();
    }
    const Camera3D camera({.fovy_radians = 1, .near_z = 1, .far_z = 5000});
    const auto transform =
        orbit_camera_transform({.target = {0, bed, 0}, .distance = 700, .yaw = 0, .pitch = -.8F});
    const render::TerrainSurfacePatchCamera view{camera.view_projection_matrix(transform, 1),
                                                 transform.translation};
    for (unsigned slots : {2U, 3U}) {
        TerrainWaterCaptureRuntime runtime(capture, source, CUBEY_TERRAIN_CAPTURE_TEST_SHADER_DIR);
        runtime.prepare_fixed_lod(runtime.capture().grid.center_xz_m(), 256);
        std::vector<vulkan::Image> outputs;
        for (unsigned i = 0; i < slots; ++i)
            outputs.emplace_back(
                device, vulkan::color_render_target_image_config(extent, VK_FORMAT_R8G8B8A8_UNORM));
        const auto target = [&](unsigned i) {
            return render::color_target_view(extent, outputs[i].format(), outputs[i].handle(),
                                             outputs[i].view());
        };
        runtime.ensure_resources(device, gpu, slots, target(0), true);
        vulkan::CommandPool pool(device,
                                 {.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT});
        std::vector<VkCommandBuffer> buffers;
        for (unsigned i = 0; i < slots; ++i)
            buffers.push_back(pool.allocate_primary());
        for (unsigned batch = 0; batch < 2; ++batch) {
            for (unsigned slot = 0; slot < slots; ++slot) {
                const auto cmd = buffers[slot];
                vulkan::check(vkResetCommandBuffer(cmd, 0), "reset pending lighting command");
                vulkan::begin_command_buffer(cmd, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
                // Keep earlier recordings live while the next slot updates its
                // bindings. Updating one shared non-update-after-bind set would
                // invalidate those command buffers before the batch submission.
                vulkan::transition_image_layout(
                    cmd, vulkan::begin_color_attachment_transition(outputs[slot].handle()));
                runtime.record(device, cmd, {slot, slots}, target(slot), false, view,
                               double(batch * slots + slot), true, false, false, 0, true, &lighting,
                               &layer);
                vulkan::end_command_buffer(cmd, "pending lighting regression");
            }
            (void)submission.submit({.waits = {}, .command_buffers = buffers, .signals = {}},
                                    "submit live lighting slots");
            submission.wait_idle("wait pending lighting batch");
            submission.mark_completed(submission.last_submitted());
        }
        runtime.verify_immutable_fields(gpu);
        std::cout << "terrain_external_lighting_pending: PASS " << slots
                  << " in-flight slots and reuse\n";
    }
}

void test_opaque_layer(cubey::TerrainWaterCaptureRuntime& runtime,
                       const cubey::vulkan::Device& device,
                       cubey::vulkan::SubmissionCoordinator& submission,
                       cubey::vulkan::GpuRuntime& gpu, float bed, unsigned size) {
    using namespace cubey;
    const VkExtent2D extent{size, size};
    vulkan::Image output(
        device, vulkan::color_render_target_image_config(extent, VK_FORMAT_R8G8B8A8_UNORM));
    const auto target =
        render::color_target_view(extent, output.format(), output.handle(), output.view());
    render::Texture2D hdr(device, {.extent = extent,
                                   .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                                   .usage = render::Texture2DUsage::ColorAttachmentSampled,
                                   .sampler = {}});
    render::DepthTexture depth(device,
                               {.extent = extent, .format = VK_FORMAT_D32_SFLOAT, .sampler = {}});
    render::TerrainSurfacePatchOpaqueLayer layer{
        render::color_target_view(extent, hdr.format(), hdr.handle(), hdr.view()),
        render::depth_target_view(depth)};
    const Camera3D camera({.fovy_radians = 1.0F, .near_z = 1.0F, .far_z = 5000.0F});
    const auto transform =
        orbit_camera_transform({.target = {0, bed, 0}, .distance = 700, .yaw = 0, .pitch = -.8F});
    const render::TerrainSurfacePatchCamera view{camera.view_projection_matrix(transform, 1),
                                                 transform.translation};
    runtime.ensure_resources(device, gpu, 2U, target);
    unsigned frame = 0;
    const auto pixels = [&](std::optional<float> z, math::Vec2 remap = {1, 0}) {
        layer.depth_scale_bias = remap;
        const auto slot = frame++ % 2U;
        (void)runtime.collect_timings(slot);
        vulkan::Buffer readback(device,
                                vulkan::readback_buffer_config(VkDeviceSize(size) * size * 4U));
        vulkan::ImmediateCommands commands(device, submission);
        const auto cmd = commands.command_buffer();
        vulkan::transition_image_layout(cmd,
                                        vulkan::begin_color_attachment_transition(output.handle()));
        if (z) {
            vulkan::transition_image_layout(
                cmd, vulkan::begin_color_attachment_transition(hdr.handle()));
            vulkan::transition_image_layout(
                cmd, vulkan::begin_depth_attachment_transition(depth.handle()));
            render::record_render_target_pass(vulkan::CommandRecorder(cmd),
                                              render::render_target_view(layer.color, layer.depth),
                                              {.color = render::color_clear_value(1, 0, 0, 1),
                                               .depth = render::depth_clear_value(*z)},
                                              {.color = vulkan::clear_store_attachment_ops(),
                                               .depth = vulkan::clear_store_attachment_ops()},
                                              [](const auto&) {});
            vulkan::transition_image_layout(
                cmd, {.image = hdr.handle(),
                      .aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT,
                      .old_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      .new_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      .src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                      .dst_access_mask = VK_ACCESS_SHADER_READ_BIT,
                      .src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                      .dst_stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT});
            vulkan::transition_image_layout(
                cmd, vulkan::finish_depth_attachment_for_sampling_transition(depth.handle()));
        }
        runtime.record(device, cmd, {slot, 2}, target, false, view, 0, true, false, false, 0, true,
                       nullptr, z ? &layer : nullptr);
        vulkan::transition_image_layout(
            cmd, vulkan::finish_color_attachment_for_readback_transition(output.handle()));
        const VkBufferImageCopy copy{.bufferOffset = 0,
                                     .bufferRowLength = 0,
                                     .bufferImageHeight = 0,
                                     .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                     .imageOffset = {0, 0, 0},
                                     .imageExtent = {size, size, 1}};
        vkCmdCopyImageToBuffer(cmd, output.handle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.handle(), 1, &copy);
        commands.submit_and_wait();
        std::vector<std::uint8_t> bytes(std::size_t(size) * size * 4U);
        readback.download(bytes.data(), bytes.size());
        return bytes;
    };
    const auto baseline = pixels(std::nullopt);
    const auto empty = pixels(1.0F);
    if (baseline != empty)
        throw std::runtime_error("empty foreground depth changed the terrain/water image");
    const auto near = pixels(0.0F);
    for (std::size_t i = 0; i < near.size(); i += 4)
        // One linear-red unit, exposure 0, one ACES+sRGB transform => 232/255.
        if (near[i] < 231 || near[i] > 233 || near[i + 1] > 1 || near[i + 2] > 1)
            throw std::runtime_error(
                "near opaque layer was covered by terrain/water/whitewater or double-tonemapped");
    const auto remapped = pixels(.999F, {80, -79});
    if (remapped != near)
        throw std::runtime_error(
            "foreground perspective depth remap did not move the layer in front of terrain");
    const auto far = pixels(.9999F);
    const auto center = (size / 2U * size + size / 2U) * 4U;
    for (unsigned channel = 0; channel < 3; ++channel)
        if (far[center + channel] != baseline[center + channel])
            throw std::runtime_error("far foreground incorrectly covered the nearer terrain/water");
    runtime.verify_immutable_fields(gpu);
    std::cout << "terrain_opaque_layer: PASS empty-depth exact, near opaque covers water, far "
                 "layer rejected, two slots, immutable fields\n";
}
} // namespace

int main() {
    std::filesystem::path temporary;
    try {
        using namespace cubey;
        const auto source_path =
            std::filesystem::path(CUBEY_TEST_ASSET_DIR) / "terrain/backdrop-smoke/heightfield.json";
        asset::TerrainRasterHeightSource source(source_path);
        asset::TerrainWaterCapture c;
        c.grid = {5U, 5U, 128.0F, {4096.0F, 512.0F}};
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
        runtime.prepare_context();
        runtime.prepare_fixed_lod(c.grid.center_xz_m(), 256.0F);
        const math::Vec3 pivot{0, 10000, 0};
        const auto distance_bounds = runtime.camera_distance_bounds(pivot);
        if (distance_bounds != runtime.camera_distance_bounds(pivot) || distance_bounds.x <= 0 ||
            distance_bounds.y < distance_bounds.x)
            throw std::runtime_error("retained triangle distance bounds are invalid or unstable");
        const auto check_distance = [&](math::Vec3 p) {
            const float d = glm::length(p - pivot);
            if (d < distance_bounds.x || d > distance_bounds.y)
                throw std::runtime_error("camera distance bounds do not enclose retained geometry");
        };
        for (const auto& v : runtime.context_product()->vertices)
            check_distance(math::Vec3{v.position_wet_hint});
        for (unsigned z = 0; z < 17; ++z)
            for (unsigned x = 0; x < 17; ++x) {
                const math::Vec2 p{float(x) * .25F, float(z) * .25F};
                const auto v = terrain::terrain_surface_context_core_sample(c, p);
                check_distance({(p.x - 2) * 128, v.bed_m, (p.y - 2) * 128});
                check_distance({(p.x - 2) * 128, v.bed_m + v.depth_m, (p.y - 2) * 128});
            }
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
                               clock, frame != 2U, true, frame % 2U == 0U, 0U, frame % 2U == 1U);
                commands.submit_and_wait();
            }
            runtime.verify_immutable_fields(gpu);
        }
        for (unsigned size : {128U, 192U, 128U}) {
            runtime.destroy_target_resources();
            test_opaque_layer(runtime, device, submission, gpu, c.bed_m[12], size);
        }
        runtime.destroy_target_resources();
        runtime.destroy();
        test_pending_external_lighting(device, submission, gpu, temporary, source_path,
                                       c.bed_m[12]);
        std::filesystem::remove_all(temporary);
        std::cout << "terrain_water_capture_lifecycle: PASS two retired slots; uniform/fixed mesh, "
                     "water on/off, "
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
