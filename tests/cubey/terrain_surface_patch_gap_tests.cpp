#include <array>
#include <cmath>
#include <cubey/core/math.h>
#include <cubey/render/pipeline_resource.h>
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
        vulkan::Instance instance({.application_name = "shared paired patch gap controls",
                                   .application_version = 0U,
                                   .required_extensions = {},
                                   .validation = true,
                                   .require_validation = false});
        vulkan::Device device(instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                         .require_present = false,
                                         .require_dynamic_rendering = false});
        vulkan::SubmissionCoordinator submission(device);
        const vulkan::BufferConfig input{.size = 25U * sizeof(float),
                                         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                         .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
        vulkan::Buffer bed(device, input), depth(device, input);
        constexpr auto bytes = 320U * sizeof(math::Vec4);
        vulkan::Buffer storage(device, vulkan::device_local_buffer_config(
                                           bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
        vulkan::Buffer readback(device, vulkan::readback_buffer_config(bytes));
        std::array<vulkan::DescriptorSetBindingConfig, 3> bindings{};
        for (std::uint32_t i = 0; i < 3U; ++i)
            bindings[i] = {.binding = i,
                           .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                           .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT};
        vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
        vulkan::DescriptorWriteBatch writes;
        writes.storage_buffer(descriptors.set(), 0U, bed.handle(), bed.size());
        writes.storage_buffer(descriptors.set(), 1U, depth.handle(), depth.size());
        writes.storage_buffer(descriptors.set(), 2U, storage.handle(), storage.size());
        writes.update(device);
        const std::array layouts{descriptors.layout()};
        render::ComputePipelineResource pipeline(
            device, {.shader_stage = render::compute_shader_file(
                         std::filesystem::path(CUBEY_TERRAIN_PATCH_TEST_SHADER_DIR) /
                         "terrain_surface_patch_gap.comp.spv"),
                     .descriptor_set_layouts = layouts});
        float worst = 0.0F;
        for (unsigned fixture = 0; fixture < 4; ++fixture) {
            std::array<float, 25> b{}, h{};
            for (unsigned z = 0; z < 5U; ++z)
                for (unsigned x = 0; x < 5U; ++x) {
                    const auto i = z * 5U + x;
                    b[i] = fixture == 0U   ? 10.0F
                           : fixture == 1U ? 2300.0F + float(x) * 16.0F + float(z) * 8.0F
                                           : 2100.0F + float(x * x + z * z) * 11.0F;
                    h[i] = fixture == 0U   ? 2.0F
                           : fixture == 1U ? float(x % 2U) * 0.35F
                           : fixture == 2U ? std::max(0.0F, 2400.0F - b[i])
                                           : 0.0F;
                }
            bed.upload(b.data(), sizeof(b));
            depth.upload(h.data(), sizeof(h));
            vulkan::ImmediateCommands commands(device, submission);
            const vulkan::CommandRecorder r(commands.command_buffer());
            r.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
            r.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0U,
                                  descriptors.set());
            r.dispatch(5U, 1U, 1U);
            const VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                                .pNext = nullptr,
                                                .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                                .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                                .buffer = storage.handle(),
                                                .offset = 0U,
                                                .size = storage.size()};
            r.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                               0U, {}, {&barrier, 1U}, {});
            const VkBufferCopy copy{.srcOffset = 0U, .dstOffset = 0U, .size = bytes};
            vkCmdCopyBuffer(commands.command_buffer(), storage.handle(), readback.handle(), 1U,
                            &copy);
            commands.submit_and_wait();
            std::array<math::Vec4, 320> values{};
            readback.download(values.data(), bytes);
            for (const auto v : values) {
                const float error = std::abs((v.y - v.x) - v.z);
                worst = std::max(worst, error);
                if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
                    !std::isfinite(v.w) || v.z < 0.0F || error > 0.001F || v.y < v.x)
                    throw std::runtime_error("paired GPU gap/boundary/triangle control failed");
                if (fixture == 0U && std::abs(v.z - 2.0F) > 0.00001F)
                    throw std::runtime_error("flat lake did not retain a flat depth");
                if (fixture == 3U && v.z != 0.0F)
                    throw std::runtime_error("dry patch created water");
            }
        }
        std::cout << "terrain_surface_patch_gap: PASS 1280 GPU samples; flat, steep, lake, dry, "
                     "boundary, triangle interiors, translated frame; max_error_m="
                  << worst << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        const std::string_view message(e.what());
        if (message.find("no Vulkan") != std::string_view::npos)
            return 77;
        return 1;
    }
}
