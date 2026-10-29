#include <array>
#include <cmath>
#include <cubey/core/math.h>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>
using namespace cubey;
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
                {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}};
            vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
            const std::array layouts{descriptors.layout()};
            struct Result {
                math::Vec4 hit;
                std::array<unsigned, 4> stats;
            };
            static_assert(sizeof(Result) == 32);
            std::array<Result, 7> rows{};
            vulkan::Buffer output(device,
                                  {.size = sizeof(rows),
                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
            vulkan::DescriptorWriteBatch writes;
            writes.storage_buffer(descriptors.set(), 0, output.handle(), output.size());
            writes.update(device);
            nlohmann::json receipt = {
                {"scope", "Actual guarded loop with controlled 1-Lipschitz fields"},
                {"cases", nlohmann::json::array()}};
            for (const std::string variant : {"relaxed12", "relaxed15", "adaptive50"}) {
                render::ComputePipelineResource pipeline(
                    device,
                    {.shader_stage = render::compute_shader_file(
                         std::filesystem::path(argv[1]) / (variant + "_guarded_contract.comp.spv")),
                     .descriptor_set_layouts = layouts});
                static_cast<void>(context.gpu().submit_and_wait(
                    {.label = "guarded stepping contract",
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
                constexpr std::array<unsigned, 7> status{2, 1, 1, 1, 0, 2, 1};
                constexpr std::array<float, 7> depth{0, .3F, .2F, .2F, 0, 0, .5F};
                for (unsigned i = 0; i < rows.size(); i++) {
                    const auto& row = rows[i];
                    bool passed = row.hit.y == static_cast<float>(status[i]) && row.hit.z <= 4096;
                    if (status[i] == 1)
                        passed = passed && std::abs(row.hit.x - depth[i]) <= 4.F / (1792.F * 32.F);
                    if (i == 0)
                        passed = passed && row.hit.z == 4096;
                    if (i == 1 && variant != "adaptive50")
                        passed = passed && row.stats[2] > 0;
                    receipt["cases"].push_back({{"variant", variant},
                                                {"index", i},
                                                {"status", row.hit.y},
                                                {"t", row.hit.x},
                                                {"steps", row.hit.z},
                                                {"proposed", row.stats[0]},
                                                {"accepted", row.stats[1]},
                                                {"rejected", row.stats[2]},
                                                {"passed", passed}});
                    if (!passed)
                        throw std::runtime_error("guarded control failed at " + variant + ":" +
                                                 std::to_string(i));
                }
            }
            receipt["passed"] = true;
            std::ofstream(argv[2]) << receipt.dump(2) << '\n';
            std::cout << "21 guarded first-hit/inside/endpoint/cached/NaN/budget checks passed\n";
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
