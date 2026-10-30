#include <array>
#include <cubey/core/math.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
using namespace cubey;
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("shader-directory output.bin");
        host::CommonRunConfig config;
        config.headless = config.headless_render_only = config.require_validation = true;
        config.width = config.height = 16;
        config.frames = 1;
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& context) {
            auto& device = context.device();
            const std::array bindings{vulkan::DescriptorSetBindingConfig{
                0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}};
            vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
            const std::array layouts{descriptors.layout()};
            std::array<math::Vec4, 22 * 6> rows{};
            vulkan::Buffer output(device,
                                  {.size = sizeof(rows),
                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
            vulkan::DescriptorWriteBatch writes;
            writes.storage_buffer(descriptors.set(), 0, output.handle(), output.size());
            writes.update(device);
            render::ComputePipelineResource pipeline(
                device, {.shader_stage = render::compute_shader_file(
                             std::filesystem::path(argv[1]) / "cached_cell_contract.comp.spv"),
                         .descriptor_set_layouts = layouts});
            static_cast<void>(context.gpu().submit_and_wait(
                {.label = "cached trilinear cell oracle",
                 .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     const vulkan::CommandRecorder r(commands.command_buffer());
                     r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                     r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                           descriptors.set());
                     r.dispatch(22, 1, 1);
                     const std::array<VkMemoryBarrier, 1> barrier{
                         {{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, VK_ACCESS_SHADER_WRITE_BIT,
                           VK_ACCESS_HOST_READ_BIT}}};
                     r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_PIPELINE_STAGE_HOST_BIT, 0, barrier);
                     commands.submit_and_wait();
                 }}));
            output.download(rows.data(), sizeof(rows));
            std::ofstream file(argv[2], std::ios::binary);
            file.write(reinterpret_cast<const char*>(rows.data()), sizeof(rows));
            if (!file)
                throw std::runtime_error("cannot write cached oracle output");
            std::cout
                << "22 actual refinement-loop observations exported for independent CPU scoring\n";
        };
        callbacks.record_frame = [](host::HeadlessPngContext&, const host::HeadlessCaptureFrame&,
                                    VkCommandBuffer command,
                                    const host::HeadlessRenderTarget& target) {
            render::record_render_target_pass(vulkan::CommandRecorder(command),
                                              render::render_target_view(target),
                                              {.color = render::color_clear_value(0, 0, 0, 1)},
                                              [](const vulkan::CommandRecorder&) {});
        };
        return host::HeadlessPngHost(
                   {.run_config = config,
                    .required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT},
                   std::move(callbacks))
            .run();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
