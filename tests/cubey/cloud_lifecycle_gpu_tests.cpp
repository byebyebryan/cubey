#include <cubey/render/cloud_layer.h>
#include <cubey/render/render_graph.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/vk_check.h>

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using namespace cubey;

void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Deliberately record (never submit) one missing-barrier hazard. The dedicated
// runner must observe exactly this synchronization diagnostic, proving that the
// selected validation setting actually took effect.
void synchronization_probe(const vulkan::Device& device) {
    vulkan::Buffer buffer(device,
                          vulkan::device_local_buffer_config(16, VK_BUFFER_USAGE_TRANSFER_DST_BIT));
    vulkan::CommandPool pool(device, {});
    const auto command = pool.allocate_primary();
    vulkan::begin_command_buffer(command, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    vkCmdFillBuffer(command, buffer.handle(), 0, 16, 1);
    vkCmdFillBuffer(command, buffer.handle(), 0, 16, 2);
    vulkan::end_command_buffer(command, "end synchronization probe");
    std::cout << "SYNC_PROBE_RECORDED\n";
}

void lifecycle(vulkan::Device& device, vulkan::SubmissionCoordinator& submission,
               vulkan::GpuRuntime& gpu, std::uint32_t slots) {
    render::CloudLayerConfig config;
    config.temporal_enabled = true;
    config.view_steps_override = 4;
    const auto shaders = render::cloud_layer_runtime_shader_files(
        CUBEY_CLOUD_TEST_SHADER_DIR, render::CloudLayerCompositeMode::ExternalBackground);
    render::CloudLayerRuntime cloud;
    cloud.create_generated_resources(device, gpu, shaders.generated, config);
    const auto weather = cloud.generated_resources().weather.handle();
    vulkan::CommandPool pool(device, {.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT});
    std::vector<VkCommandBuffer> commands;
    for (std::uint32_t i = 0; i < slots; ++i) {
        commands.push_back(pool.allocate_primary());
    }
    render::RenderGraphFrameExecutor executor(slots);

    for (VkExtent2D extent : {VkExtent2D{32, 24}, VkExtent2D{48, 32}}) {
        cloud.create_swapchain_resources(device, shaders,
                                         render::CloudLayerCompositeMode::ExternalBackground,
                                         VK_FORMAT_R8G8B8A8_UNORM, extent, slots, config);
        require(cloud.generated_resources().weather.handle() == weather,
                "target recreation replaced target-independent weather");
        std::vector<vulkan::Image> outputs;
        for (std::uint32_t i = 0; i < slots; ++i) {
            outputs.emplace_back(
                device, vulkan::color_render_target_image_config(extent, VK_FORMAT_R8G8B8A8_UNORM));
        }
        for (std::uint32_t batch = 0; batch < 6; ++batch) {
            if (batch == 2) {
                cloud.invalidate_history();
            }
            config.temporal_enabled = batch != 3;
            const bool history_valid = batch == 1 || batch == 5;
            for (std::uint32_t index = 0; index < slots; ++index) {
                const render::FrameSlot slot{index, slots};
                vulkan::check(vkResetCommandBuffer(commands[index], 0),
                              "reset cloud lifecycle command");
                render::RenderGraphBuilder builder;
                const auto target = builder.import_color_target(
                    "output",
                    render::color_target_view(extent, outputs[index].format(),
                                              outputs[index].handle(), outputs[index].view()),
                    render::render_graph_undefined_texture_state());
                const auto& background = cloud.generated_resources().weather;
                const auto sampled = render::RenderGraphTextureState{
                    .layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    .access_mask = VK_ACCESS_SHADER_READ_BIT,
                    .stage_mask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT};
                const auto background_handle = builder.import_texture(
                    {.label = "generated background",
                     .extent = {background.extent().width, background.extent().height, 1},
                     .format = background.format(),
                     .aspects = VK_IMAGE_ASPECT_COLOR_BIT},
                    background.handle(), background.view(), sampled, sampled);
                auto uniforms = render::cloud_layer_frame_uniforms(
                    config, {.camera_position = {0, 30, 0},
                             .tan_half_fovy = .6F,
                             .target_extent = extent,
                             .temporal_frame_index = cloud.temporal_frame_index(),
                             .external_background = true});
                cloud.upload_frame_uniforms(slot, uniforms);
                const auto frame = cloud.declare_product(builder, extent, config, slot, uniforms);
                require(frame.temporal_pass_enabled == config.temporal_enabled,
                        "temporal toggle did not change graph participation");
                cloud.declare_composite(builder, target, frame, slot, background_handle);
                const auto graph = builder.compile();
                if (frame.temporal_pass_enabled) {
                    const auto state = graph.texture(frame.history_cloud_read).initial_state;
                    require(state.has_value(), "history import lacks initial state");
                    require(
                        (state->layout != VK_IMAGE_LAYOUT_UNDEFINED) == history_valid,
                        "history validity leaked across slots, invalidation, toggle or recreation");
                }
                executor.record(
                    {.device = &device, .command_buffer = commands[index], .frame_slot = slot},
                    graph, [&](const auto& resources) {
                        if (batch % 2U == 0U) {
                            cloud.update_product_descriptors(device, slot, graph, resources, frame);
                            cloud.update_composite_descriptors(device, slot, graph, resources,
                                                               frame, background_handle);
                        } else {
                            cloud.update_descriptors(device, slot, graph, resources, frame,
                                                     background_handle);
                        }
                    });
                cloud.complete_frame(slot, frame);
            }
            // Keep all slot recordings alive while subsequent slots update
            // descriptors, then execute the batch and finish before slot reuse.
            static_cast<void>(submission.submit_and_wait(
                {.waits = {}, .command_buffers = commands, .signals = {}},
                "submit cloud lifecycle slots", "wait cloud lifecycle slots"));
        }
        executor.clear();
        executor.resize(slots);
        cloud.destroy_swapchain_resources();
        cloud.destroy_swapchain_resources();
    }
    auto invalid = shaders;
    invalid.general_march.path = "missing-cloud-lifecycle-shader.comp.spv";
    bool failed = false;
    try {
        cloud.create_swapchain_resources(device, invalid,
                                         render::CloudLayerCompositeMode::ExternalBackground,
                                         VK_FORMAT_R8G8B8A8_UNORM, {32, 24}, slots, config);
    } catch (const std::exception& error) {
        failed = std::string_view(error.what()).find("missing-cloud-lifecycle-shader") !=
                 std::string_view::npos;
    }
    require(failed, "missing shader did not produce a controlled target creation failure");
    cloud.destroy_swapchain_resources();
    cloud.create_swapchain_resources(device, shaders,
                                     render::CloudLayerCompositeMode::ExternalBackground,
                                     VK_FORMAT_R8G8B8A8_UNORM, {32, 24}, slots, config);
    cloud.destroy_swapchain_resources();
    cloud.destroy_generated_resources();
    require(!cloud.generated_resources_created(), "cloud shutdown retained generated resources");
    std::cout << "cloud lifecycle PASS: " << slots
              << " slots, reuse/recreate/history/toggle/failure/shutdown\n";
}
} // namespace

int main(int argc, char** argv) {
    try {
        vulkan::Instance instance({.application_name = "cloud lifecycle",
                                   .required_extensions = {},
                                   .require_validation = true});
        vulkan::Device device(instance,
                              {.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT,
                               .require_present = false,
                               .require_dynamic_rendering = true});
        if (argc == 2 && std::string_view(argv[1]) == "--probe") {
            synchronization_probe(device);
            return 0;
        }
        vulkan::SubmissionCoordinator submission(device);
        vulkan::GpuRuntime gpu({.device = &device,
                                .submission = &submission,
                                .execution_mode = vulkan::GpuRuntimeExecutionMode::Inline});
        lifecycle(device, submission, gpu, 1);
        lifecycle(device, submission, gpu, 2);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
