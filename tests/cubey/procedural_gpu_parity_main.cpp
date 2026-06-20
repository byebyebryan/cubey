#include <cubey/procedural/noise.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/device.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>
#include <cubey/vulkan/submission_coordinator.h>

#include <vulkan/vulkan.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

constexpr int kSkip = 77;
constexpr std::size_t kSampleCount = 5;

[[nodiscard]] bool contains(std::string_view haystack, std::string_view needle) {
    return haystack.find(needle) != std::string_view::npos;
}

[[nodiscard]] bool is_vulkan_unavailable_error(std::string_view message) {
    return contains(message, "vkCreateInstance") ||
           contains(message, "vkEnumeratePhysicalDevices") ||
           contains(message, "no Vulkan physical devices found") ||
           contains(message, "no Vulkan device with required queues found") ||
           contains(message, "vkCreateDevice");
}

[[nodiscard]] std::filesystem::path parity_shader_path() {
    return std::filesystem::path{CUBEY_PROCEDURAL_GPU_PARITY_SHADER_DIR} /
           "procedural_gpu_parity.comp.spv";
}

[[nodiscard]] std::array<float, kSampleCount> expected_samples() {
    using cubey::procedural::CoherentFractalType;
    using cubey::procedural::CoherentNoiseConfig;
    using cubey::procedural::CoherentNoiseType;

    const CoherentNoiseConfig simplex{
        .seed = 2024,
        .frequency = 0.015F,
        .noise_type = CoherentNoiseType::OpenSimplex2,
        .fractal_type = CoherentFractalType::None,
    };
    const CoherentNoiseConfig perlin_fbm{
        .seed = 99,
        .frequency = 0.025F,
        .noise_type = CoherentNoiseType::Perlin,
        .fractal_type = CoherentFractalType::Fbm,
        .octaves = 4,
        .lacunarity = 2.1F,
        .gain = 0.48F,
        .weighted_strength = 0.13F,
    };

    return {
        cubey::procedural::hash_to_unit_masked_24(123456789U),
        cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U),
        cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5}),
        cubey::procedural::sample_coherent_noise_3d(12.5F, -7.25F, 3.5F, simplex),
        cubey::procedural::sample_coherent_noise_2d(6.25F, -2.75F, perlin_fbm),
    };
}

void require_near(float actual, float expected, float tolerance, const char* label) {
    if (std::fabs(actual - expected) <= tolerance) {
        return;
    }
    std::cerr << "procedural GPU parity mismatch for " << label << ": actual=" << actual
              << " expected=" << expected << " tolerance=" << tolerance << '\n';
    throw std::runtime_error("procedural GPU parity mismatch");
}

void run_parity_test() {
    cubey::vulkan::Instance instance({
        .application_name = "cubey procedural gpu parity",
        .application_version = 0,
        .required_extensions = {},
        .validation = true,
        .require_validation = false,
    });
    cubey::vulkan::Device device(instance, {
                                               .surface = VK_NULL_HANDLE,
                                               .required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                               .require_present = false,
                                               .require_dynamic_rendering = false,
                                           });
    cubey::vulkan::SubmissionCoordinator submission(device);

    constexpr VkDeviceSize kBufferSize = sizeof(float) * kSampleCount;
    cubey::vulkan::Buffer storage(
        device, cubey::vulkan::device_local_buffer_config(
                    kBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    cubey::vulkan::Buffer readback(device, cubey::vulkan::readback_buffer_config(kBufferSize));

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, storage.handle(), storage.size());
    writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors.layout()};
    const cubey::render::ComputePipelineResource pipeline(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage =
                        cubey::render::compute_shader_file(parity_shader_path()),
                    .descriptor_set_layouts = set_layouts,
                });

    cubey::vulkan::ImmediateCommands commands(device, submission);
    const cubey::vulkan::CommandRecorder recorder(commands.command_buffer());
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptors.set());
    recorder.dispatch(1, 1, 1);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = storage.handle();
    barrier.offset = 0;
    barrier.size = storage.size();
    recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT, 0, {}, {&barrier, 1}, {});

    const VkBufferCopy copy{
        .srcOffset = 0,
        .dstOffset = 0,
        .size = kBufferSize,
    };
    vkCmdCopyBuffer(commands.command_buffer(), storage.handle(), readback.handle(), 1, &copy);
    commands.submit_and_wait();

    std::array<float, kSampleCount> actual{};
    readback.download(actual.data(), kBufferSize);

    const std::array<float, kSampleCount> expected = expected_samples();
    constexpr std::array labels{
        "cubey_proc_hash01_u32",
        "cubey_proc_value_noise_3d",
        "cubey_proc_fbm_3d",
        "FastNoiseLite OpenSimplex2 3D",
        "FastNoiseLite Perlin FBM 2D",
    };
    constexpr std::array tolerances{
        0.000001F,
        0.000001F,
        0.000001F,
        0.00001F,
        0.00001F,
    };
    for (std::size_t i = 0; i < kSampleCount; ++i) {
        require_near(actual[i], expected[i], tolerances[i], labels[i]);
    }
}

} // namespace

int main() {
    try {
        run_parity_test();
        return 0;
    } catch (const std::exception& error) {
        if (is_vulkan_unavailable_error(error.what())) {
            std::cerr << "Skipping procedural GPU parity test: " << error.what() << '\n';
            return kSkip;
        }
        std::cerr << "procedural GPU parity test failed: " << error.what() << '\n';
        return 1;
    }
}
