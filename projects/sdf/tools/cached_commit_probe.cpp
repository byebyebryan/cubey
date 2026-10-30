#include "../sdf_regions.h"
#include <bit>
#include <cubey/host/headless_png_host.h>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
using namespace cubey;
using namespace cubey::projects::sdf;
using Json = nlohmann::json;
int main(int argc, char** argv) {
    try {
        if (argc != 4)
            throw std::invalid_argument("input.json output.json region-axis");
        const auto input = Json::parse(std::ifstream(argv[1]));
        const auto source = parse_authored_scene(input.at("scene").dump());
        const auto axis = static_cast<unsigned>(std::stoul(argv[3]));
        if (axis != 4 && axis != 16)
            throw std::invalid_argument("probe axis must be 4 or 16");
        std::vector<std::array<std::uint32_t, 2>> queries;
        for (const auto& q : input.at("samples")) {
            const auto code = q.at("morton").get<std::uint32_t>(),
                       sample = q.at("sample").get<std::uint32_t>();
            if (code > 0xffffffU || sample >= 512)
                throw std::invalid_argument("invalid sample coordinate");
            queries.push_back({code, sample});
        }
        if (queries.empty() || queries.size() > 65536)
            throw std::invalid_argument("invalid probe count");
        host::CommonRunConfig config;
        config.headless = config.headless_render_only = config.require_validation = true;
        config.width = config.height = 16;
        config.frames = 1;
        host::HeadlessPngHostCallbacks callbacks;
        callbacks.create_resources = [&](host::HeadlessPngContext& ctx) {
            UploadQueue uploads;
            ProjectGpuServices gpu(ctx.gpu(), uploads);
            GpuRegionPrograms regions(ctx.device(), source, axis, region_reference_limit, false,
                                      nullptr, "off");
            const auto program = authored_program_bytes(&source);
            auto src = gpu.upload_device_buffer<std::uint8_t>(
                program, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "probe source");
            auto qs = gpu.upload_device_buffer<std::array<std::uint32_t, 2>>(
                queries, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "probe samples");
            vulkan::Buffer values(
                ctx.device(),
                vulkan::BufferConfig{.size = queries.size() * 16,
                                     .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                     .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
            std::vector<vulkan::DescriptorSetBindingConfig> bindings;
            for (unsigned binding : {0U, 1U, 10U, 16U, 17U})
                bindings.push_back(
                    {binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
            vulkan::DescriptorSetBundle ds(ctx.device(), vulkan::DescriptorSetInfo(bindings));
            vulkan::DescriptorWriteBatch writes;
            const std::array<std::pair<unsigned, const vulkan::Buffer*>, 5> buffers{
                {{0, &qs},
                 {1, &values},
                 {10, &src},
                 {16, regions.cells.get()},
                 {17, regions.references.get()}}};
            for (const auto& [binding, b] : buffers)
                writes.storage_buffer(ds.set(), binding, b->handle(), b->size());
            writes.update(ctx.device());
            const std::array layouts{ds.layout()};
            const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4}}};
            render::ComputePipelineResource pipeline(
                ctx.device(),
                {.shader_stage = render::compute_shader_file(
                     std::filesystem::path(CUBEY_SDF_SHADER_DIR) / "commit_prune_probe.comp.spv"),
                 .descriptor_set_layouts = layouts,
                 .push_constants = push});
            static_cast<void>(gpu.submit_and_wait(
                {.label = "full and pruned same-kernel diagnostic",
                 .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     vulkan::CommandRecorder r(commands.command_buffer());
                     r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                     r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                           ds.set());
                     r.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                      static_cast<unsigned>(queries.size()));
                     r.dispatch(static_cast<unsigned>((queries.size() + 63) / 64), 1, 1);
                     const VkMemoryBarrier b{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                             .pNext = nullptr,
                                             .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                             .dstAccessMask = VK_ACCESS_HOST_READ_BIT};
                     r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        VK_PIPELINE_STAGE_HOST_BIT, 0, std::span(&b, 1));
                     commands.submit_and_wait();
                 }}));
            std::vector<std::array<std::uint32_t, 4>> rows(queries.size());
            values.download(rows.data(), rows.size() * 16);
            Json result{{"geometry_hash", authored_geometry_hash(source)},
                        {"axis", axis},
                        {"samples", Json::array()},
                        {"scope", "Same-kernel source/pruned float diagnostic; packed half is not "
                                  "raw image encoding"}};
            for (std::size_t i = 0; i < rows.size(); ++i)
                result["samples"].push_back({{"query", input.at("samples").at(i)},
                                             {"full", std::bit_cast<float>(rows[i][0])},
                                             {"pruned", std::bit_cast<float>(rows[i][1])},
                                             {"full_bits", rows[i][0]},
                                             {"pruned_bits", rows[i][1]},
                                             {"full_packed_half", rows[i][2] & 65535U},
                                             {"pruned_packed_half", rows[i][2] >> 16}});
            std::ofstream file(argv[2]);
            file << result.dump(2) << '\n';
            if (!file)
                throw std::runtime_error("cannot save probe");
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
        std::cerr << "commit probe: " << e.what() << '\n';
        return 1;
    }
}
