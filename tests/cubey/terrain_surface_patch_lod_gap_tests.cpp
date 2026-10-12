#include <array>
#include <cmath>
#include <cubey/render/pipeline_resource.h>
#include <cubey/terrain/terrain_surface_patch_lod.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        using namespace cubey;
        vulkan::Instance instance({.application_name = "fixed paired mesh gap controls",
                                   .required_extensions = {},
                                   .validation = true,
                                   .require_validation = false});
        vulkan::Device device(instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                         .require_present = false,
                                         .require_dynamic_rendering = false});
        vulkan::SubmissionCoordinator submission(device);
        constexpr std::uint32_t count = 4096;
        const asset::TerrainWaterCaptureGrid grid{65, 49, 16, {-512, -384}};
        const auto mesh = terrain::make_terrain_surface_patch_lod(grid, {120, 200}, 256);
        const auto host = [&](const void* data, std::size_t bytes) {
            vulkan::Buffer buffer(device,
                                  {.size = bytes,
                                   .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
            buffer.upload(data, bytes);
            return buffer;
        };
        std::vector<float> b(65 * 49), h(b.size());
        auto bed = host(b.data(), b.size() * sizeof(float)),
             depth = host(h.data(), h.size() * sizeof(float));
        auto coordinates =
            host(mesh.coordinates.data(), mesh.coordinates.size() * sizeof(math::Vec2));
        auto indices = host(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
        constexpr auto bytes = count * sizeof(math::Vec4);
        vulkan::Buffer storage(device, vulkan::device_local_buffer_config(
                                           bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        vulkan::Buffer readback(device, vulkan::readback_buffer_config(bytes));
        std::array<vulkan::DescriptorSetBindingConfig, 5> bindings;
        for (unsigned i = 0; i < bindings.size(); ++i)
            bindings[i] = {.binding = i,
                           .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT};
        vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
        vulkan::DescriptorWriteBatch writes;
        const std::array buffers{&bed, &depth, &coordinates, &indices, &storage};
        for (unsigned i = 0; i < buffers.size(); ++i)
            writes.storage_buffer(descriptors.set(), i, buffers[i]->handle(), buffers[i]->size());
        writes.update(device);
        const std::array layouts{descriptors.layout()};
        const std::array ranges{VkPushConstantRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 16}};
        render::ComputePipelineResource pipeline(
            device, {.shader_stage = render::compute_shader_file(
                         std::filesystem::path(CUBEY_TERRAIN_PATCH_TEST_SHADER_DIR) /
                         "terrain_surface_patch_lod_gap.comp.spv"),
                     .descriptor_set_layouts = layouts,
                     .push_constants = ranges});
        float worst = 0;
        for (unsigned fixture = 0; fixture < 4; ++fixture) {
            for (unsigned z = 0; z < 49; ++z)
                for (unsigned x = 0; x < 65; ++x) {
                    const auto i = z * 65 + x;
                    b[i] = fixture == 0 ? 10.0F : 2100.0F + float(x) * 4 + float(z) * 2;
                    h[i] = fixture == 0   ? 2.0F
                           : fixture == 1 ? float(x % 2) * .35F
                           : fixture == 2 ? 2500.0F - b[i]
                                          : 0;
                }
            bed.upload(b.data(), b.size() * sizeof(float));
            depth.upload(h.data(), h.size() * sizeof(float));
            vulkan::ImmediateCommands commands(device, submission);
            vulkan::CommandRecorder r(commands.command_buffer());
            r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
            r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                  descriptors.set());
            r.push_constants(
                pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                glm::uvec4{grid.width, grid.height, std::uint32_t(mesh.indices.size() / 3), count});
            r.dispatch(count / 64, 1, 1);
            const VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                                .pNext = nullptr,
                                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                .buffer = storage.handle(),
                                                .offset = 0,
                                                .size = bytes};
            r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0, {}, {&barrier, 1}, {});
            const VkBufferCopy copy{0, 0, bytes};
            vkCmdCopyBuffer(commands.command_buffer(), storage.handle(), readback.handle(), 1,
                            &copy);
            commands.submit_and_wait();
            std::array<math::Vec4, count> values;
            readback.download(values.data(), bytes);
            for (const auto v : values) {
                const auto error = std::abs((v.y - v.x) - v.z);
                worst = std::max(worst, error);
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) || v.z < 0 ||
                    error > .001F)
                    throw std::runtime_error("fixed paired GPU separation failed");
                if (fixture == 2 && std::abs(v.y - 2500.0F) > .001F)
                    throw std::runtime_error("fixed mesh did not preserve flat lake elevation");
                if (fixture == 3 && v.z != 0)
                    throw std::runtime_error("fixed dry mesh created water");
            }
        }
        std::cout << "terrain_surface_patch_lod_gap: PASS 16384 GPU samples; indexed vertices, "
                     "interiors, edges, "
                     "flat lake, steep and dry; max_error_m="
                  << worst << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        if (std::string_view(e.what()).find("no Vulkan") != std::string_view::npos)
            return 77;
        return 1;
    }
}
