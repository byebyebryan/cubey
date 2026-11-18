#include <cubey/render/material_instance.h>
#include <cubey/render/render_graph_frame.h>
#include <cubey/render/temporal_aa.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <glm/gtc/packing.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace cubey;
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}

math::Vec4 independent_view_frame(const vulkan::Device& device,
                                  vulkan::SubmissionCoordinator& submission,
                                  render::TemporalAntiAliasing& taa, std::uint64_t view_id,
                                  math::Vec4 color) {
    constexpr VkExtent2D extent{8, 8};
    const auto frame = taa.begin_frame({0, 1}, {.view_id = view_id});
    render::RenderGraphBuilder builder;
    const auto input = builder.create_texture(
        {"view.color", {8, 8, 1}, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT});
    const auto depth = builder.create_texture(
        {"view.depth", {8, 8, 1}, VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT});
    builder.add_pass("view.input", render::RenderGraphQueueDomain::Transfer)
        .transfer_write_texture(input)
        .transfer_write_texture(depth)
        .execute([=](const render::RenderGraphExecutionContext& context) {
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            const VkClearColorValue clear_color{{color.r, color.g, color.b, color.a}};
            const VkClearColorValue clear_depth{{.5F, 0, 0, 0}};
            vkCmdClearColorImage(context.recorder().handle(), context.resolved_texture(input).image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_color, 1, &range);
            vkCmdClearColorImage(context.recorder().handle(), context.resolved_texture(depth).image,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear_depth, 1, &range);
        });
    const auto motion = taa.declare_camera_motion(builder, depth, frame);
    const auto output = taa.declare_resolve(builder, input, motion, frame);
    vulkan::Buffer readback(device, vulkan::readback_buffer_config(8U * 8U * 8U));
    const auto host = builder.import_buffer(
        {"view.readback", readback.size()}, readback.handle(),
        render::RenderGraphBufferState{VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT},
        render::RenderGraphBufferState{VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT});
    builder.add_pass("view.readback", render::RenderGraphQueueDomain::Transfer)
        .transfer_read_texture(output)
        .transfer_write_buffer(host)
        .execute([=, &readback](const render::RenderGraphExecutionContext& context) {
            const auto copy = vulkan::buffer_image_copy(VkExtent3D{extent.width, extent.height, 1});
            vkCmdCopyImageToBuffer(
                context.recorder().handle(), context.resolved_texture(output).image,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle(), 1, &copy);
        });
    const auto graph = builder.compile();
    render::RenderGraphFrameExecutor executor(1);
    vulkan::ImmediateCommands commands(device, submission);
    executor.record({.device = &device,
                     .command_buffer = commands.command_buffer(),
                     .frame_slot = {0, 1},
                     .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
                    graph, [&](const render::RenderGraphResourceSet& resources) {
                        taa.update_descriptors(device, graph, resources, frame);
                    });
    commands.submit_and_wait();
    taa.commit_frame(frame.token, submission.last_submitted());
    std::array<std::uint16_t, 8U * 8U * 4U> values{};
    readback.download(values.data(), readback.size());
    const auto center = 4U * (4U * 8U + 4U);
    return {glm::unpackHalf1x16(values[center]), glm::unpackHalf1x16(values[center + 1]),
            glm::unpackHalf1x16(values[center + 2]), glm::unpackHalf1x16(values[center + 3])};
}
} // namespace
int main(int argc, char** argv) {
    try {
        const std::filesystem::path shader_dir = CUBEY_TEMPORAL_TEST_SHADER_DIR;
        const auto resolve_path =
            argc > 1 ? std::filesystem::path(argv[1]) : shader_dir / "resolve.comp.spv";
        const std::filesystem::path dump = argc > 2 ? argv[2] : "";
        vulkan::Instance instance({.application_name = "TAA numerical and lifecycle fixtures",
                                   .required_extensions = {},
                                   .validation = true,
                                   .require_validation = true});
        vulkan::Device device(
            instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT, .require_present = false});
        vulkan::SubmissionCoordinator submission(device);
        render::TemporalAntiAliasing taa;
        const VkExtent2D extent{37, 23}; // partial workgroups and halo loads
        const unsigned slots = argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 2U;
        require(slots >= 1 && slots <= 3, "fixture requires one/two/three slots");
        const auto pixels = extent.width * extent.height;
        const auto bytes = pixels * 12U;
        vulkan::Buffer readback(device, vulkan::readback_buffer_config(bytes));
        const std::array<vulkan::DescriptorSetBindingConfig, 2> bindings{
            {{0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT},
             {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT}}};
        vulkan::DescriptorSetBundle fixture_set(device, vulkan::DescriptorSetInfo(bindings));
        const std::array layouts{fixture_set.layout()};
        const std::array pushes{VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32}};
        render::ComputePipelineResource fixture(
            device,
            {.shader_stage = render::compute_shader_file(shader_dir / "temporal_fixture.comp.spv"),
             .descriptor_set_layouts = layouts,
             .push_constants = pushes});
        render::TemporalAaConfig config{.extent = extent,
                                        .frame_slot_count = slots,
                                        .motion_shader = shader_dir / "motion.comp.spv",
                                        .resolve_shader = resolve_path};
        taa.create(device, config);
        render::RenderGraphFrameExecutor executor(slots);
        const auto projection = math::perspective(
            glm::radians(90.0F), float(extent.width) / float(extent.height), .01F, 100.0F);
        auto previous_eye = math::Vec3(0);
        std::uint64_t submitted = 0;
        double worst_motion = 0;
        for (unsigned attempt = 0; attempt < 26; ++attempt) {
            const bool discard = attempt == 0 || attempt == 12;
            const auto slot_index = std::array<unsigned, 4>{1, 0, 0, 1}[attempt % 4];
            const render::FrameSlot slot{slot_index % slots, slots};
            auto eye = math::Vec3{float(submitted) * .012F, float(submitted) * .004F, 0};
            if (attempt >= 4 && attempt <= 7)
                eye = previous_eye;
            if (attempt >= 23)
                eye += math::Vec3{8.0F, -4.0F, 0};
            // Current z=-2 plane was clipped by the previous camera, although
            // its previous clip w remains positive in both cases.
            if (attempt == 24)
                eye.z = 1.995F;
            if (attempt == 25)
                eye.z = -101.0F;
            const bool reset = attempt == 17;
            const auto frame = taa.begin_frame(
                slot, {.projection = projection,
                       .view = glm::lookAt(eye, eye + math::Vec3{0, 0, -1}, math::Vec3{0, 1, 0}),
                       .content_generation = attempt >= 20 ? 2U : 1U,
                       .reset = reset});
            render::RenderGraphBuilder builder;
            const auto color = builder.create_texture({"fixture.color",
                                                       {extent.width, extent.height, 1},
                                                       VK_FORMAT_R32G32B32A32_SFLOAT,
                                                       VK_IMAGE_ASPECT_COLOR_BIT});
            const auto depth = builder.create_texture({"fixture.depth",
                                                       {extent.width, extent.height, 1},
                                                       VK_FORMAT_R32_SFLOAT,
                                                       VK_IMAGE_ASPECT_COLOR_BIT});
            const auto depth_clip = projection * math::Vec4{0, 0, -2, 1};
            const std::array<math::Vec4, 2> constants{
                {{float(extent.width), float(extent.height), frame.sample_offset_pixels.x,
                  frame.sample_offset_pixels.y},
                 {depth_clip.z / depth_clip.w, float(submitted) * .11F, float(attempt % 3), 0}}};
            builder.add_pass("fixture.input", render::RenderGraphQueueDomain::Compute)
                .write_storage_texture(color)
                .write_storage_texture(depth)
                .execute([&](const render::RenderGraphExecutionContext& ctx) {
                    auto& r = ctx.recorder();
                    r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, fixture.pipeline());
                    r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, fixture.layout(), 0,
                                          fixture_set.set());
                    r.push_constants(fixture.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, constants);
                    r.dispatch(3, 2, 1);
                });
            const auto motion = taa.declare_camera_motion(builder, depth, frame);
            const auto resolved = taa.declare_resolve(builder, color, motion, frame);
            const auto host = builder.import_buffer(
                {"fixture.readback", bytes}, readback.handle(),
                render::RenderGraphBufferState{VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT},
                render::RenderGraphBufferState{VK_ACCESS_HOST_READ_BIT,
                                               VK_PIPELINE_STAGE_HOST_BIT});
            builder.add_pass("fixture.readback", render::RenderGraphQueueDomain::Transfer)
                .transfer_read_texture(resolved)
                .transfer_read_texture(motion)
                .transfer_write_buffer(host)
                .execute([=, &readback](const render::RenderGraphExecutionContext& ctx) {
                    for (const auto [handle, offset] :
                         std::array{std::pair{resolved, VkDeviceSize(0)},
                                    std::pair{motion, VkDeviceSize(pixels * 8U)}}) {
                        const auto copy = vulkan::buffer_image_copy(
                            {.extent = {extent.width, extent.height, 1}, .buffer_offset = offset});
                        vkCmdCopyImageToBuffer(
                            ctx.recorder().handle(), ctx.resolved_texture(handle).image,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback.handle(), 1, &copy);
                    }
                });
            const auto graph = builder.compile();
            vulkan::ImmediateCommands commands(device, submission);
            executor.record(
                {.device = &device,
                 .command_buffer = commands.command_buffer(),
                 .frame_slot = slot,
                 .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
                graph, [&](const render::RenderGraphResourceSet& resources) {
                    render::MaterialDescriptorWriter(fixture_set.set())
                        .storage_image(0, resources.texture(color)->view)
                        .storage_image(1, resources.texture(depth)->view)
                        .update(device);
                    taa.update_descriptors(device, graph, resources, frame);
                });
            if (discard) {
                taa.discard_frame();
                require(taa.metrics().submitted_frames == submitted,
                        "discard advanced GPU history");
                continue;
            }
            commands.submit_and_wait();
            taa.commit_frame(frame.token, submission.last_submitted());
            ++submitted;
            require(taa.metrics().submitted_frames == submitted,
                    "history not tied to accepted submissions");
            std::vector<std::uint16_t> values(bytes / 2U);
            readback.download(values.data(), bytes);
            for (unsigned p = 0; p < pixels; ++p) {
                for (unsigned c = 0; c < 4; ++c) {
                    const auto value = glm::unpackHalf1x16(values[p * 4U + c]);
                    require(std::isfinite(value) && value >= 0,
                            "resolve produced nonfinite/negative HDR");
                    if (c == 3)
                        require(value == 1, "beauty alpha became a status or opacity mask");
                }
                const float mx = glm::unpackHalf1x16(values[pixels * 4U + p * 2U]);
                const float my = glm::unpackHalf1x16(values[pixels * 4U + p * 2U + 1U]);
                require(std::isfinite(mx) && std::isfinite(my), "motion field is nonfinite");
                const bool sky = p / extent.width < 4;
                if (!sky && (attempt == 24 || attempt == 25)) {
                    require(mx == float(extent.width) + 16.0F && my == float(extent.height) + 16.0F,
                            "previous near/far-clipped geometry reused history");
                    continue;
                }
                const auto shift = frame.history_valid && !sky ? eye - previous_eye : math::Vec3(0);
                const auto expected = math::Vec2{float(extent.height) * shift.x / 4.0F,
                                                 -float(extent.height) * shift.y / 4.0F};
                const double error =
                    std::max(std::abs(double(mx - expected.x)), std::abs(double(my - expected.y)));
                worst_motion = std::max(worst_motion, error);
                const double quantization_bound =
                    .0003 +
                    .0006 * std::max(std::abs(double(expected.x)), std::abs(double(expected.y)));
                require(error <= quantization_bound,
                        "GPU motion disagreed with half-float pinhole oracle");
            }
            if (!dump.empty()) {
                std::filesystem::create_directories(dump);
                std::ofstream out(dump / (std::to_string(submitted) + ".bin"), std::ios::binary);
                out.write(reinterpret_cast<const char*>(values.data()), bytes);
            }
            previous_eye = eye;
            if (attempt == 8) {
                auto invalid = config;
                invalid.resolve_shader = shader_dir / "does-not-exist.spv";
                bool failed = false;
                try {
                    taa.create(device, invalid);
                } catch (const std::exception&) {
                    failed = true;
                }
                require(failed && taa.metrics().submitted_frames == submitted,
                        "failed replacement destroyed the accepted resource generation");
            }
        }
        require(submitted == 24, "fixture omitted accepted frames");
        const auto memory = taa.metrics();
        require(memory.persistent_requested_bytes == pixels * 16U &&
                    memory.per_slot_transient_requested_bytes == pixels * 12U,
                "TAA memory accounting mismatch");
        require(memory.persistent_image_allocation_bytes >= memory.persistent_requested_bytes &&
                    memory.per_slot_transient_image_allocation_bytes >=
                        memory.per_slot_transient_requested_bytes &&
                    memory.uniform_allocation_bytes >= memory.uniform_requested_bytes,
                "TAA allocation requirements did not cover requested resources");
        taa.destroy();
        auto resized = config;
        resized.extent = {41, 19};
        taa.create(device, resized);
        const auto first = taa.begin_frame({0, slots}, {.projection = projection});
        require(!first.history_valid, "recreated extent inherited stale history");
        taa.discard_frame();
        render::TemporalAntiAliasing red_view, green_view;
        auto independent = config;
        independent.extent = {8, 8};
        independent.frame_slot_count = 1;
        independent.clamping_factor = -1; // retain the distinct first-view color
        red_view.create(device, independent);
        green_view.create(device, independent);
        static_cast<void>(independent_view_frame(device, submission, red_view, 41, {2, 0, 0, 1}));
        static_cast<void>(independent_view_frame(device, submission, green_view, 82, {0, 2, 0, 1}));
        const auto red = independent_view_frame(device, submission, red_view, 41, {0, 0, 0, 1});
        const auto green = independent_view_frame(device, submission, green_view, 82, {0, 0, 0, 1});
        require(red.r > .1F && red.g < .001F && green.g > .1F && green.r < .001F &&
                    red_view.metrics().history_age == 1 && green_view.metrics().history_age == 1,
                "independent views mixed color history or submission sequence");
        std::cout
            << "PASS: 24 submitted frames, 2 discards, nonround slot order, sky/plane motion, "
               "reset/content change, finite HDR, failed replacement, resize, independent views; "
               "worst motion pixels="
            << worst_motion << '\n';
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
