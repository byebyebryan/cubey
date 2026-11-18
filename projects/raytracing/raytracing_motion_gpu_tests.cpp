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
#include <string>
#include <string_view>

namespace {
using namespace cubey;

struct alignas(16) MotionCase {
    math::Mat4 current_view_projection{1.0F};
    math::Mat4 previous_view_projection{1.0F};
    math::Vec4 hit_point{0.0F};
    math::Vec4 ray_direction{0.0F, 0.0F, -1.0F, 0.0F};
    math::Vec4 extent_hit{640.0F, 360.0F, 1.0F, 0.0F};
};
static_assert(sizeof(MotionCase) == 176);

struct ExpectedMotion {
    std::string_view name;
    math::Vec2 pixels;
    float tolerance = 0.001F;
};

MotionCase motion_case(math::Mat4 current, math::Mat4 previous, math::Vec4 hit_point,
                       math::Vec4 ray_direction, bool hit) {
    return {.current_view_projection = current,
            .previous_view_projection = previous,
            .hit_point = hit_point,
            .ray_direction = ray_direction,
            .extent_hit = {640.0F, 360.0F, hit ? 1.0F : 0.0F, 0.0F}};
}

void run() {
    vulkan::InstanceConfig instance_config;
    instance_config.application_name = "raytracing fixed-opaque motion vectors";
    instance_config.validation = true;
    vulkan::Instance instance(instance_config);
    vulkan::Device device(instance,
                          {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                           .require_present = false,
                           .require_dynamic_rendering = false});
    vulkan::SubmissionCoordinator submission(device);

    const math::Mat4 identity{1.0F};
    constexpr std::size_t case_count = 6;
    std::array<MotionCase, case_count> cases{};
    cases[0] = motion_case(identity, identity, {0.25F, -0.2F, 0.5F, 1.0F},
                           {0.0F, 0.0F, -1.0F, 0.0F}, true);
    // A second jittered primary on the same stationary surface changes its hit
    // point, while the producer's unjittered view matrices remain identical.
    cases[1] = motion_case(identity, identity, {0.251F, -0.198F, 0.5F, 1.0F},
                           {0.0F, 0.0F, -1.0F, 0.0F}, true);

    auto camera_right = identity;
    camera_right[3][0] = -0.2F;
    cases[2] = motion_case(camera_right, identity, {0.0F, 0.0F, 0.5F, 1.0F},
                           {0.0F, 0.0F, -1.0F, 0.0F}, true);
    auto camera_left = identity;
    camera_left[3][0] = 0.2F;
    cases[3] = motion_case(camera_left, identity, {0.0F, 0.0F, 0.5F, 1.0F},
                           {0.0F, 0.0F, -1.0F, 0.0F}, true);

    math::Mat4 sky_projection{0.0F};
    sky_projection[0][0] = 1.0F;
    sky_projection[1][1] = 1.0F;
    sky_projection[2][3] = -1.0F; // perspective w = -view-space z
    sky_projection[3][2] = 0.5F;
    auto translated_sky_projection = sky_projection;
    translated_sky_projection[3][0] = 0.2F; // camera translation in clip x
    cases[4] = motion_case(translated_sky_projection, sky_projection,
                           {0.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F, 0.0F}, false);

    constexpr float cosine = 0.8660254037844386F;
    constexpr float sine = 0.5F;
    math::Mat4 rotated_sky_projection{0.0F};
    rotated_sky_projection[0][0] = cosine;
    rotated_sky_projection[2][0] = sine;
    rotated_sky_projection[1][1] = 1.0F;
    rotated_sky_projection[0][3] = sine;
    rotated_sky_projection[2][3] = -cosine;
    rotated_sky_projection[3][2] = 0.5F;
    cases[5] = motion_case(rotated_sky_projection, sky_projection,
                           {0.0F, 0.0F, 0.0F, 0.0F}, {0.0F, 0.0F, -1.0F, 0.0F}, false);

    constexpr VkMemoryPropertyFlags host_memory = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    vulkan::Buffer input_buffer(
        device, {.size = sizeof(cases),
                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 .memory_properties = host_memory});
    input_buffer.upload(cases.data(), sizeof(cases));
    std::array<math::Vec4, case_count> empty_results{};
    vulkan::Buffer result_buffer(
        device, {.size = sizeof(empty_results),
                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 .memory_properties = host_memory});

    const std::array<vulkan::DescriptorSetBindingConfig, 2> bindings{{
        {.binding = 0,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
        {.binding = 1,
         .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
         .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
    }};
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(descriptors.set(), 0, input_buffer.handle(), input_buffer.size());
    writes.storage_buffer(descriptors.set(), 1, result_buffer.handle(), result_buffer.size());
    writes.update(device);

    const std::array layouts{descriptors.layout()};
    render::ComputePipelineResource pipeline(
        device, {.shader_stage = render::compute_shader_file(
                     std::filesystem::path{CUBEY_RAYTRACING_MOTION_TEST_SHADER_DIR} /
                     "fixed_opaque_motion_test.comp.spv"),
                 .descriptor_set_layouts = layouts});

    vulkan::ImmediateCommands commands(device, submission);
    const vulkan::CommandRecorder recorder(commands.command_buffer());
    const VkBufferMemoryBarrier input_ready{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_HOST_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = input_buffer.handle(),
        .offset = 0,
        .size = input_buffer.size(),
    };
    recorder.pipeline_barrier(VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0,
                              {}, {&input_ready, 1}, {});
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                 descriptors.set());
    recorder.dispatch(static_cast<std::uint32_t>(case_count), 1, 1);
    const VkBufferMemoryBarrier results_ready{
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = result_buffer.handle(),
        .offset = 0,
        .size = result_buffer.size(),
    };
    recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                              {}, {&results_ready, 1}, {});
    commands.submit_and_wait();

    std::array<math::Vec4, case_count> actual{};
    result_buffer.download(actual.data(), sizeof(actual));
    const std::array expected{
        ExpectedMotion{"stationary hit, jitter sample A", {0.0F, 0.0F}},
        ExpectedMotion{"stationary hit, jitter sample B", {0.0F, 0.0F}},
        ExpectedMotion{"hit with camera moving right", {64.0F, 0.0F}},
        ExpectedMotion{"hit with camera moving left", {-64.0F, 0.0F}},
        ExpectedMotion{"sky under camera translation", {0.0F, 0.0F}},
        ExpectedMotion{"sky under 30-degree camera rotation",
                       {320.0F / std::sqrt(3.0F), 0.0F}, 0.01F},
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (!std::isfinite(actual[index].x) || !std::isfinite(actual[index].y) ||
            std::abs(actual[index].x - expected[index].pixels.x) > expected[index].tolerance ||
            std::abs(actual[index].y - expected[index].pixels.y) > expected[index].tolerance ||
            actual[index].z != 0.0F || actual[index].w != 0.0F)
            throw std::runtime_error("fixed-opaque GLSL motion vector mismatch: " +
                                     std::string(expected[index].name));
    }
    std::cout << "raytracing: fixed-opaque GLSL hit/sky motion vectors pass\n";
}
} // namespace

int main() {
    try {
        run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
