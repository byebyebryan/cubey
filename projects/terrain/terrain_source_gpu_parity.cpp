#include "terrain_source.h"
#include "terrain_source_gpu.h"

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
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr int kSkip = 77;
constexpr std::size_t kSampleCount = 7;

struct alignas(16) TerrainSourceParityInput {
    cubey::projects::terrain::TerrainSourceGpuParameters parameters{};
    std::array<std::array<float, 4>, kSampleCount> queries{};
};

struct alignas(16) TerrainSourceParityOutput {
    std::array<std::array<float, 4>, kSampleCount * 2U> values{};
};

static_assert(sizeof(TerrainSourceParityInput) == 464U);
static_assert(sizeof(TerrainSourceParityOutput) == 224U);

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
    return std::filesystem::path{CUBEY_TERRAIN_GPU_PARITY_SHADER_DIR} /
           "terrain_source_parity.comp.spv";
}

void require_near(float actual, float expected, float tolerance, std::string_view label) {
    if (std::abs(actual - expected) <= tolerance) {
        return;
    }
    throw std::runtime_error(std::string(label) + " mismatch: actual=" + std::to_string(actual) +
                             " expected=" + std::to_string(expected));
}

void run_parity_test() {
    cubey::vulkan::Instance instance({
        .application_name = "cubey terrain source gpu parity",
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

    cubey::vulkan::Buffer input(device,
                                {
                                    .size = sizeof(TerrainSourceParityInput),
                                    .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                });
    cubey::vulkan::Buffer output(
        device, cubey::vulkan::device_local_buffer_config(sizeof(TerrainSourceParityOutput),
                                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                              VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    cubey::vulkan::Buffer readback(
        device, cubey::vulkan::readback_buffer_config(sizeof(TerrainSourceParityOutput)));

    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 2> bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
        {
            .binding = 1,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo descriptor_info(bindings);
    cubey::vulkan::DescriptorSetBundle descriptors(device, descriptor_info);
    cubey::vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, input.handle(), input.size());
    writes.storage_buffer(descriptors.set(), 1, output.handle(), output.size());
    writes.update(device);

    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors.layout()};
    const cubey::render::ComputePipelineResource pipeline(
        device, cubey::render::ComputePipelineResourceConfig{
                    .shader_stage = cubey::render::compute_shader_file(parity_shader_path()),
                    .descriptor_set_layouts = set_layouts,
                });

    constexpr std::array<cubey::projects::terrain::TerrainQuery, kSampleCount> queries{{
        {.world_xz = {0.0F, 0.0F}, .footprint_m = 0.0F},
        {.world_xz = {1532.5F, -873.25F}, .footprint_m = 2.0F},
        {.world_xz = {-8192.0F, 4096.0F}, .footprint_m = 8.0F},
        {.world_xz = {12'500.0F, 22'750.0F}, .footprint_m = 32.0F},
        {.world_xz = {27'125.0F, -11'375.0F}, .footprint_m = 64.0F},
        {.world_xz = {-48'000.0F, -17'000.0F}, .footprint_m = 128.0F},
        {.world_xz = {61'000.0F, -52'000.0F}, .footprint_m = 512.0F},
    }};
    constexpr std::array presets{
        cubey::projects::terrain::TerrainPreset::Mountain,
        cubey::projects::terrain::TerrainPreset::Upland,
        cubey::projects::terrain::TerrainPreset::Plains,
    };
    constexpr std::array weathering_modes{
        cubey::projects::terrain::TerrainWeatheringMode::Off,
        cubey::projects::terrain::TerrainWeatheringMode::Local,
    };
    constexpr std::array versions{
        cubey::projects::terrain::TerrainSourceVersion::V1,
        cubey::projects::terrain::TerrainSourceVersion::V2,
        cubey::projects::terrain::TerrainSourceVersion::V3,
    };

    for (const auto version : versions) {
        for (const auto preset : presets) {
            if (version != cubey::projects::terrain::TerrainSourceVersion::V1 &&
                preset != cubey::projects::terrain::TerrainPreset::Mountain) {
                continue;
            }
            for (const auto weathering : weathering_modes) {
                const auto parameters =
                    cubey::projects::terrain::resolve_terrain_source_parameters({
                        .seed = 0xf123'4567'89ab'cdefULL,
                        .preset = preset,
                        .version = version,
                        .weathering = weathering,
                        .weathering_strength = 0.73F,
                    });
                TerrainSourceParityInput input_data{};
                input_data.parameters =
                    cubey::projects::terrain::terrain_source_gpu_parameters(parameters);
                for (std::size_t index = 0; index < queries.size(); ++index) {
                    input_data.queries[index] = {queries[index].world_xz.x,
                                                 queries[index].world_xz.y,
                                                 queries[index].footprint_m, 0.0F};
                }
                input.upload(&input_data, sizeof(input_data));

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
                barrier.buffer = output.handle();
                barrier.offset = 0;
                barrier.size = output.size();
                recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          VK_PIPELINE_STAGE_TRANSFER_BIT, 0, {}, {&barrier, 1}, {});
                const VkBufferCopy copy{
                    .srcOffset = 0,
                    .dstOffset = 0,
                    .size = output.size(),
                };
                vkCmdCopyBuffer(commands.command_buffer(), output.handle(), readback.handle(), 1,
                                &copy);
                commands.submit_and_wait();

                TerrainSourceParityOutput actual{};
                readback.download(&actual, sizeof(actual));
                for (std::size_t index = 0; index < queries.size(); ++index) {
                    const auto expected =
                        cubey::projects::terrain::sample_terrain(parameters, queries[index]);
                    const auto& first = actual.values[index * 2U];
                    const auto& second = actual.values[index * 2U + 1U];
                    require_near(first[0], expected.base_height_m, 0.1F, "base height");
                    require_near(first[1], expected.height_m, 0.1F, "final height");
                    if (version != cubey::projects::terrain::TerrainSourceVersion::V3 ||
                        weathering == cubey::projects::terrain::TerrainWeatheringMode::Off) {
                        require_near(first[2], expected.gradient_xz.x, 0.02F, "x gradient");
                        require_near(first[3], expected.gradient_xz.y, 0.02F, "z gradient");
                    }
                    require_near(second[0], expected.weathering_delta_m, 0.1F, "weathering delta");
                }
            }
        }
    }
}

} // namespace

int main() {
    try {
        run_parity_test();
        return 0;
    } catch (const std::exception& error) {
        if (is_vulkan_unavailable_error(error.what())) {
            std::cerr << "Skipping terrain GPU parity test: " << error.what() << '\n';
            return kSkip;
        }
        std::cerr << "terrain GPU parity test failed: " << error.what() << '\n';
        return 1;
    }
}
