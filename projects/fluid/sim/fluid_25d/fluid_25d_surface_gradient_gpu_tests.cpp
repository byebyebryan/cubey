#include <cubey/core/math.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using cubey::math::Vec3;
using cubey::math::Vec4;
constexpr std::array<Vec3, 16> normals{{{0, 1, 0},
                                        {1, 0, 0},
                                        {-1, 0, 0},
                                        {0, 0, 1},
                                        {0, 0, -1},
                                        {0, -1, 0},
                                        {0.3F, 1, 0.6F},
                                        {-0.3F, 1, -0.6F},
                                        {0.70711F, 0.70710F, 0},
                                        {0.70709F, 0.70710F, 0},
                                        {1.00001F, 1, 1},
                                        {0.99999F, 1, 1},
                                        {0.99F, 0.01F, 0},
                                        {-0.99F, 0.01F, 0},
                                        {0.01F, 0.01F, 0.99F},
                                        {0.01F, 0.01F, -0.99F}}};

Vec3 expected(std::size_t i, unsigned mode) {
    const auto n = glm::normalize(normals[i]);
    if (mode == 1U || mode == 2U || mode == 3U)
        return n; // Zero strength, flat maps, or distant fade are identities.
    auto weights = glm::pow(glm::abs(n), Vec3{4.0F});
    weights /= weights.x + weights.y + weights.z;
    // Independent analytic linear-height fields in ZY, XZ, XY planes.
    const Vec3 x = mode == 4U ? Vec3{0, 128, 128} : Vec3{0, -0.1F, 0.3F};
    const Vec3 y = mode == 4U ? Vec3{128, 0, 128} : Vec3{0.2F, 0, 0.3F};
    const Vec3 z = mode == 4U ? Vec3{128, 128, 0} : Vec3{0.2F, -0.1F, 0};
    const auto gradient = weights.x * x + weights.y * y + weights.z * z;
    const auto tangent = gradient - glm::dot(n, gradient) * n;
    return glm::normalize(n - tangent * (mode == 4U ? 2.0F : 0.12F));
}

void run() {
    cubey::vulkan::Instance instance({.application_name = "fluid25d surface gradient controls",
                                      .application_version = 0,
                                      .required_extensions = {},
                                      .validation = true,
                                      .require_validation = false});
    cubey::vulkan::Device device(instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                            .require_present = false,
                                            .require_dynamic_rendering = false});
    cubey::vulkan::SubmissionCoordinator submission(device);
    constexpr auto bytes = sizeof(Vec4) * 80U;
    cubey::vulkan::Buffer storage(
        device, cubey::vulkan::device_local_buffer_config(
                    bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    cubey::vulkan::Buffer readback(device, cubey::vulkan::readback_buffer_config(bytes));
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{
        {{.binding = 0,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
    cubey::vulkan::DescriptorSetBundle descriptors(device,
                                                   cubey::vulkan::DescriptorSetInfo(bindings));
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, storage.handle(), storage.size());
    writes.update(device);
    const std::array layouts{descriptors.layout()};
    cubey::render::ComputePipelineResource pipeline(
        device, {.shader_stage = cubey::render::compute_shader_file(
                     std::filesystem::path{CUBEY_FLUID25D_NORMAL_TEST_SHADER_DIR} /
                     "fluid_25d_surface_gradient_test.comp.spv"),
                 .descriptor_set_layouts = layouts});
    cubey::vulkan::ImmediateCommands commands(device, submission);
    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptors.set());
    recorder.dispatch(1, 1, 1);
    const VkBufferMemoryBarrier barrier{.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
                                        .pNext = nullptr,
                                        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                                        .buffer = storage.handle(),
                                        .offset = 0,
                                        .size = storage.size()};
    recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, {}, {&barrier, 1}, {});
    const VkBufferCopy copy{.srcOffset = 0, .dstOffset = 0, .size = bytes};
    vkCmdCopyBuffer(commands.command_buffer(), storage.handle(), readback.handle(), 1, &copy);
    commands.submit_and_wait();
    std::array<Vec4, 80> values{};
    readback.download(values.data(), bytes);
    for (unsigned mode = 0; mode < 5; ++mode) {
        for (std::size_t i = 0; i < normals.size(); ++i) {
            const auto actual = values[mode * 16U + i];
            const auto target = expected(i, mode);
            if (!std::isfinite(actual.w) || glm::length(Vec3{actual} - target) > 0.00005F ||
                std::abs(glm::length(Vec3{actual}) - 1.0F) > 0.00001F ||
                std::abs(actual.w) > 0.0001F)
                throw std::runtime_error("GPU surface-gradient analytic control failed at mode " +
                                         std::to_string(mode) + " normal " + std::to_string(i));
        }
    }
    if (glm::length(Vec3{values[8]} - Vec3{values[9]}) > 0.0001F ||
        glm::length(Vec3{values[10]} - Vec3{values[11]}) > 0.0001F)
        throw std::runtime_error("GPU projection-seam continuity failed");
    std::cout << "PASS: 80 GPU analytic normal/orientation/identity/seam/bound controls\n";
}
} // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        const std::string_view text{error.what()};
        if (text.find("no Vulkan physical devices found") != std::string_view::npos ||
            text.find("vkEnumeratePhysicalDevices") != std::string_view::npos)
            return 77;
        std::cerr << error.what() << '\n';
        return 1;
    }
}
