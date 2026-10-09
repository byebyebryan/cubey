#include "fluid_25d_scenic_environment.h"
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/texture.h>
#include <cubey/vulkan/buffer.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/descriptors.h>
#include <cubey/vulkan/gpu_runtime.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/instance.h>

#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace cubey;
struct Push {
    math::Vec4 camera_position_radius, radii_ground, rayleigh, mie, ozone;
    math::Vec4 sun_direction_radius, atmosphere_options;
};
math::Vec3 cube_direction(unsigned face, float u, float v) {
    const std::array directions{math::Vec3{1, -v, -u}, math::Vec3{-1, -v, u},
                                math::Vec3{u, 1, v},   math::Vec3{u, -1, -v},
                                math::Vec3{u, -v, 1},  math::Vec3{-u, -v, -1}};
    return glm::normalize(directions.at(face));
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void run() {
    vulkan::Instance instance({.application_name = "fluid25d captured daylight controls",
                               .application_version = 0,
                               .required_extensions = {},
                               .validation = true,
                               .require_validation = false});
    vulkan::Device device(instance, {.required_queue_flags = VK_QUEUE_COMPUTE_BIT,
                                     .require_present = false,
                                     .require_dynamic_rendering = false});
    vulkan::SubmissionCoordinator submission(device);
    vulkan::GpuRuntime gpu(device, submission);
    vulkan::Buffer coefficients(
        device, vulkan::device_local_buffer_config(sizeof(math::Vec4) * 10U,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    vulkan::Buffer values(device,
                          vulkan::device_local_buffer_config(sizeof(math::Vec4) * 18U,
                                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT));
    vulkan::Buffer readback(device, vulkan::readback_buffer_config(sizeof(math::Vec4) * 28U));
    const std::array<vulkan::DescriptorSetBindingConfig, 2> bindings{
        {{.binding = 0,
          .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
         {.binding = 1,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
    const std::array<vulkan::DescriptorSetBindingConfig, 2> control_bindings{
        {{.binding = 0,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT},
         {.binding = 1,
          .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .stage_flags = VK_SHADER_STAGE_COMPUTE_BIT}}};
    vulkan::DescriptorSetBundle descriptors(device, vulkan::DescriptorSetInfo(bindings));
    vulkan::DescriptorSetBundle controls(device, vulkan::DescriptorSetInfo(control_bindings));
    vulkan::DescriptorWriteBatch writes;
    writes.storage_buffer(controls.set(), 0, coefficients.handle(), coefficients.size());
    writes.storage_buffer(controls.set(), 1, values.handle(), values.size());
    writes.update(device);
    const std::array layouts{descriptors.layout()}, control_layouts{controls.layout()};
    const std::array<VkPushConstantRange, 1> ranges{
        {{.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT, .offset = 0, .size = sizeof(Push)}}};
    const std::filesystem::path shaders{CUBEY_FLUID25D_DAYLIGHT_TEST_SHADER_DIR};
    render::ComputePipelineResource pipeline(device, {.shader_stage = render::compute_shader_file(
                                                          shaders / "fluid_25d_daylight.comp.spv"),
                                                      .descriptor_set_layouts = layouts,
                                                      .push_constants = ranges});
    render::ComputePipelineResource control_pipeline(
        device, {.shader_stage =
                     render::compute_shader_file(shaders / "fluid_25d_daylight_controls.comp.spv"),
                 .descriptor_set_layouts = control_layouts});
    constexpr std::array normals{math::Vec3{1, 0, 0},  math::Vec3{-1, 0, 0}, math::Vec3{0, 1, 0},
                                 math::Vec3{0, -1, 0}, math::Vec3{0, 0, 1},  math::Vec3{0, 0, -1},
                                 math::Vec3{1, 2, 3},  math::Vec3{-3, 1, -2}};
    for (unsigned model = 0; model < 3; ++model) {
        constexpr unsigned extent = 32;
        std::vector<std::uint8_t> bytes(sizeof(math::Vec4) * 6U * extent * extent);
        for (unsigned face = 0; face < 6; ++face)
            for (unsigned y = 0; y < extent; ++y)
                for (unsigned x = 0; x < extent; ++x) {
                    const auto direction =
                        cube_direction(face, 2.0F * (float(x) + 0.5F) / extent - 1.0F,
                                       2.0F * (float(y) + 0.5F) / extent - 1.0F);
                    const math::Vec4 value{
                        model == 0 ? math::Vec3{0.35F} : math::Vec3{0.5F} + direction * 0.2F, 1};
                    std::memcpy(bytes.data() + sizeof(value) * ((face * extent + y) * extent + x),
                                &value, sizeof(value));
                }
        const auto cube = render::create_uploaded_texture_cube(
            device, gpu,
            {.extent = extent,
             .mip_levels = 1,
             .format = VK_FORMAT_R32G32B32A32_SFLOAT,
             .bytes = bytes,
             .create_sampler = true,
             .sampler = {.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}});
        vulkan::DescriptorWriteBatch cube_writes;
        cube_writes.combined_image_sampler(descriptors.set(), 0, cube.sampler().handle(),
                                           cube.view());
        cube_writes.storage_buffer(descriptors.set(), 1, coefficients.handle(),
                                   coefficients.size());
        cube_writes.update(device);
        auto config = projects::fluid::fluid_25d::fluid_25d_fixed_daylight();
        if (model < 2)
            config.rayleigh_density_scale = config.mie_density_scale = config.ozone_density_scale =
                0;
        const auto a = render::atmosphere_environment_frame_uniforms(config, {});
        const Push push{a.camera_position_radius, a.radii_ground,      a.rayleigh, a.mie, a.ozone,
                        a.sun_direction_radius,   a.atmosphere_options};
        vulkan::ImmediateCommands commands(device, submission);
        const vulkan::CommandRecorder recorder(commands.command_buffer());
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                     descriptors.set());
        recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, push);
        recorder.dispatch(1, 1, 1);
        const VkMemoryBarrier ready{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                    .pNext = nullptr,
                                    .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                    .dstAccessMask = VK_ACCESS_SHADER_READ_BIT};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, {&ready, 1}, {}, {});
        recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, control_pipeline.pipeline());
        recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, control_pipeline.layout(), 0,
                                     controls.set());
        recorder.dispatch(1, 1, 1);
        const VkMemoryBarrier done{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                   .pNext = nullptr,
                                   .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
                                   .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT};
        recorder.pipeline_barrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT, 0, {&done, 1}, {}, {});
        const VkBufferCopy c{.srcOffset = 0, .dstOffset = 0, .size = coefficients.size()};
        const VkBufferCopy v{
            .srcOffset = 0, .dstOffset = coefficients.size(), .size = values.size()};
        vkCmdCopyBuffer(commands.command_buffer(), coefficients.handle(), readback.handle(), 1, &c);
        vkCmdCopyBuffer(commands.command_buffer(), values.handle(), readback.handle(), 1, &v);
        commands.submit_and_wait();
        std::array<math::Vec4, 28> actual{};
        readback.download(actual.data(), sizeof(actual));
        for (unsigned i = 0; i < 8; ++i) {
            const auto expected =
                model == 0 ? math::Vec3{0.35F}
                           : math::Vec3{0.5F} + glm::normalize(normals[i]) * (0.2F * 2.0F / 3.0F);
            require(glm::length(math::Vec3{actual[10U + i]} - expected) < 0.001F,
                    "GPU constant/axis-colour E/pi reconstruction failed");
        }
        if (model < 2)
            require(glm::length(math::Vec3{actual[9]} - math::Vec3{22}) < 0.0001F,
                    "vacuum sun must match captured atmosphere's source intensity");
        else {
            const math::Vec3 sun{actual[9]};
            require(sun.x < 22 && sun.x > sun.y && sun.y > sun.z && sun.z > 0,
                    "daylight atmosphere attenuation must be bounded and warmer than source");
            std::cout << "daylight sun RGB: " << sun.x << ' ' << sun.y << ' ' << sun.z << '\n';
        }
        for (unsigned i = 18; i < 24; ++i)
            require(std::isfinite(actual[i].x) && actual[i].y > 0 &&
                        std::abs(glm::length(math::Vec3{actual[i]}) - 1) < 0.00001F,
                    "backdrop rays must be finite/upward/unit");
        require(glm::length(math::Vec3{actual[18]} - glm::normalize(math::Vec3{1, 0.4F, 0})) <
                    0.00001F,
                "above-horizon backdrop rays must retain identity");
        require(glm::length(math::Vec3{actual[22]} - math::Vec3{actual[23]}) < 0.0001F,
                "backdrop remap must be continuous through horizon");
        const math::Vec2 slope{0.0357142857F, -0.0214285714F};
        // Exact inverse of the control's 2x2 screen->UV matrix.
        require(glm::length(math::Vec2{actual[24]} - slope) < 0.00001F &&
                    glm::length(math::Vec2{actual[25]}) == 0,
                "receiver-plane analytic/degenerate control failed");
        require(std::abs(actual[26].x - 4) < 0.00001F && std::abs(actual[27].x - 4) < 0.00001F,
                "continuous shadow tent must retain constant unit weight");
    }
    std::cout << "PASS: captured daylight E/pi, cube axes, sun attenuation, backdrop and shadow "
                 "controls\n";
}
} // namespace
int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& e) {
        const std::string_view text{e.what()};
        if (text.find("no Vulkan physical devices found") != std::string_view::npos ||
            text.find("vkEnumeratePhysicalDevices") != std::string_view::npos)
            return 77;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
