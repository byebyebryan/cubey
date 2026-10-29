#include "../sdf_renderer.h"
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace cubey;
using namespace cubey::projects::sdf;
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::invalid_argument("shader-directory receipt.json");
        host::CommonRunConfig config;
        config.headless = config.headless_render_only = config.require_validation = true;
        config.width = config.height = 16;
        config.frames = 1;
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& context) {
            auto& device = context.device();
            const std::vector<vulkan::DescriptorSetBindingConfig> bindings{
                {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT},
                {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}};
            vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
            const std::array layouts{descriptors.layout()};
            const auto memory =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            vulkan::Buffer uniform(device, {.size = sizeof(LightingUniforms),
                                            .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                            .memory_properties = memory});
            LightingUniforms light{};
            light.direct_color = {1, 1, 1, 0};
            light.material = {.5F, .5F, .5F, 0};
            light.quality = {.5F, .04F, 0, 0};
            uniform.upload(&light, sizeof(light));
            struct Result {
                math::Vec4 radiance;
                std::array<unsigned, 4> observations;
            };
            static_assert(sizeof(Result) == 32);
            std::array<Result, 7> rows{};
            vulkan::Buffer output(device, {.size = sizeof(rows),
                                           .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           .memory_properties = memory});
            vulkan::DescriptorWriteBatch writes;
            writes.storage_buffer(descriptors.set(), 0, output.handle(), output.size());
            writes.uniform_buffer(descriptors.set(), 2, uniform.handle(), uniform.size());
            writes.update(device);
            nlohmann::json receipt = {
                {"scope",
                 "Controlled exhaustion fixture using actual surfaceRadiance and stub trace"},
                {"cases", nlohmann::json::array()}};
            for (const bool candidate : {false, true}) {
                render::ComputePipelineResource pipeline(
                    device, {.shader_stage = render::compute_shader_file(
                                 std::filesystem::path(argv[1]) /
                                 (candidate ? "shadow_contract_candidate.comp.spv"
                                            : "shadow_contract_reference.comp.spv")),
                             .descriptor_set_layouts = layouts});
                static_cast<void>(context.gpu().submit_and_wait(
                    {.label = "shadow exhaustion contract",
                     .work = [&](vulkan::GpuOwnerContext& owner) {
                         vulkan::ImmediateCommands commands(owner);
                         const vulkan::CommandRecorder r(commands.command_buffer());
                         r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                         r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                               descriptors.set());
                         r.dispatch(7, 1, 1);
                         const std::array<VkMemoryBarrier, 1> barrier{
                             {{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr,
                               VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT}}};
                         r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                            VK_PIPELINE_STAGE_HOST_BIT, 0, barrier);
                         commands.submit_and_wait();
                     }}));
                output.download(rows.data(), sizeof(rows));
                for (unsigned i = 0; i < rows.size(); ++i) {
                    const unsigned expected = candidate && (i == 0 || i == 2 || i == 4) ? 0U : 1U;
                    const auto& o = rows[i].observations;
                    if (o[0] != expected || o[1] != expected || o[2] != 0)
                        throw std::runtime_error(
                            "shadow query/exhaustion/origin contract failed at " +
                            std::to_string(i));
                    receipt["cases"].push_back({{"candidate", candidate},
                                                {"index", i},
                                                {"trace_calls", o[0]},
                                                {"exhausted", o[1]},
                                                {"wrong_origin", o[2]}});
                }
            }
            receipt["passed"] = true;
            std::ofstream(argv[2]) << receipt.dump(2) << '\n';
            std::cout << "14 controlled shadow exhaustion/nonfinite/origin checks passed\n";
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
