#include "pbr_furnace_app_internal.h"

#include <cubey/render/primitive_mesh.h>
#include <cubey/render/texture.h>

#include <vulkan/vulkan.h>

#include <glm/geometric.hpp>

#include <cstddef>
#include <cstring>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#ifndef CUBEY_PBR_FURNACE_SHADER_DIR
#error "CUBEY_PBR_FURNACE_SHADER_DIR must be defined by the pbr_furnace CMake target"
#endif

namespace cubey::projects::pbr_furnace {

using cubey::host::FrameStatsSample;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PBR_FURNACE_SHADER_DIR) / filename;
}

void append_rgba32f(std::vector<std::uint8_t>& bytes, std::array<float, 4> rgba) {
    const std::size_t offset = bytes.size();
    bytes.resize(offset + sizeof(float) * rgba.size());
    std::memcpy(bytes.data() + offset, rgba.data(), sizeof(float) * rgba.size());
}

std::vector<std::uint8_t> white_cube_bytes(std::uint32_t extent, std::uint32_t mip_levels) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(cubey::render::texture_cube_byte_size(
        extent, mip_levels, cubey::render::texture_format_byte_size(kIblFormat)));
    for (std::uint32_t mip = 0; mip < mip_levels; ++mip) {
        const std::uint32_t mip_extent = cubey::render::texture_cube_mip_extent(extent, mip);
        const std::size_t texel_count =
            static_cast<std::size_t>(mip_extent) * static_cast<std::size_t>(mip_extent) * 6U;
        for (std::size_t texel = 0; texel < texel_count; ++texel) {
            append_rgba32f(bytes, {1.0F, 1.0F, 1.0F, 1.0F});
        }
    }
    return bytes;
}

WhitePbrEnvironment create_white_pbr_environment(const cubey::vulkan::Device& device,
                                                 cubey::vulkan::GpuRuntime& gpu) {
    const cubey::render::GeneratedPbrEnvironmentConfig brdf_config{
        .irradiance_extent = 1,
        .prefiltered_extent = 1,
        .prefiltered_mip_levels = 1,
        .brdf_lut_extent = 128,
        .intensity = 1.0F,
    };
    const cubey::render::GeneratedPbrEnvironmentData brdf_data =
        cubey::render::generate_pbr_environment_data(brdf_config);
    const cubey::vulkan::SamplerConfig sampler{
        .min_filter = VK_FILTER_LINEAR,
        .mag_filter = VK_FILTER_LINEAR,
        .address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipmap_mode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .max_lod = static_cast<float>(kIblPrefilteredMipLevels - 1U),
    };
    std::vector<std::uint8_t> irradiance_bytes = white_cube_bytes(16, 1);
    std::vector<std::uint8_t> prefiltered_bytes =
        white_cube_bytes(kIblPrefilteredExtent, kIblPrefilteredMipLevels);
    cubey::render::TextureCube irradiance = cubey::render::create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = 16,
            .mip_levels = 1,
            .format = kIblFormat,
            .bytes =
                std::span<const std::uint8_t>{irradiance_bytes.data(), irradiance_bytes.size()},
            .create_sampler = true,
            .sampler = sampler,
        });
    cubey::render::TextureCube prefiltered = cubey::render::create_uploaded_texture_cube(
        device, gpu,
        {
            .extent = kIblPrefilteredExtent,
            .mip_levels = kIblPrefilteredMipLevels,
            .format = kIblFormat,
            .bytes =
                std::span<const std::uint8_t>{prefiltered_bytes.data(), prefiltered_bytes.size()},
            .create_sampler = true,
            .sampler = sampler,
        });
    cubey::render::Texture2D brdf_lut = cubey::render::create_uploaded_texture_2d(
        device, gpu,
        {
            .extent = {brdf_config.brdf_lut_extent, brdf_config.brdf_lut_extent},
            .format = kIblFormat,
            .bytes = std::span<const std::uint8_t>{brdf_data.brdf_lut_rgba32f.data(),
                                                   brdf_data.brdf_lut_rgba32f.size()},
            .create_sampler = true,
            .sampler = sampler,
        });
    return {
        .irradiance_cube = std::move(irradiance),
        .prefiltered_cube = std::move(prefiltered),
        .brdf_lut = std::move(brdf_lut),
        .prefiltered_mip_levels = kIblPrefilteredMipLevels,
        .intensity = 1.0F,
    };
}

cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> make_pbr_sphere_mesh() {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormalUv> sphere =
        cubey::render::make_uv_sphere_position_color_normal_uv_mesh({
            .radius = kSphereRadius,
            .latitude_segments = 24,
            .longitude_segments = 48,
        });

    cubey::render::PrimitiveMeshData<cubey::render::PbrVertex> mesh;
    mesh.vertices.reserve(sphere.vertices.size());
    mesh.indices = sphere.indices;
    for (const cubey::render::VertexPositionColorNormalUv& vertex : sphere.vertices) {
        const cubey::math::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        cubey::math::Vec3 tangent = glm::cross(cubey::math::Vec3{0.0F, 1.0F, 0.0F}, normal);
        if (glm::length(tangent) < 0.0001F) {
            tangent = {1.0F, 0.0F, 0.0F};
        } else {
            tangent = glm::normalize(tangent);
        }
        mesh.vertices.push_back({
            .position = {vertex.position[0], vertex.position[1], vertex.position[2]},
            .normal = normal,
            .tangent = {tangent.x, tangent.y, tangent.z, 1.0F},
            .uv0 = {vertex.uv[0], vertex.uv[1]},
        });
    }
    return mesh;
}

PbrFurnaceApp::PbrFurnaceApp(PbrFurnaceConfig config) : config_(std::move(config)) {
    orbit_controller_.set_home_distance(kCameraDistance);
}

int PbrFurnaceApp::run() {
    if (config_.common.headless) {
        return run_headless();
    }
    return run_windowed();
}

int PbrFurnaceApp::run_windowed() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context.device(), context.gpu(),
                                          context.frame_slot_count());
        create_forward_pass(context.device(), context.swapchain().extent(),
                            context.swapchain().format());
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext&) {
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
        update_camera_transform();
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext&,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_furnace_frame(frame.command_buffer, frame.color_target, frame.frame_slot, true);
    };
    callbacks.frame_stats_sample =
        [](cubey::host::WindowedAppContext& context,
           const FrameTiming& timing) -> std::optional<FrameStatsSample> {
        const VkExtent2D extent = context.swapchain().extent();
        return FrameStatsSample{
            .delta_seconds = timing.delta_seconds,
            .width = extent.width,
            .height = extent.height,
            .triangles = static_cast<std::uint32_t>(kPbrFurnaceMaterialCount) * 24U * 48U * 2U,
        };
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext&) { destroy_all_resources(); };

    return cubey::host::run_windowed_app(
        {
            .run_config = config_.common,
            .app_name = "pbr_furnace",
            .ready_status = "rendering PBR white furnace",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

int PbrFurnaceApp::run_headless() {
    cubey::host::HeadlessPngHostConfig host_config;
    host_config.run_config = config_.common;
    host_config.required_queue_flags = VK_QUEUE_GRAPHICS_BIT;
    host_config.output_format = VK_FORMAT_R8G8B8A8_UNORM;
    host_config.require_dynamic_rendering = true;

    cubey::host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [this](cubey::host::HeadlessPngContext& context) {
        create_global_resources_if_needed(
            context.device(), context.gpu(),
            cubey::host::headless_capture_frame_slot_count(config_.common));
        create_forward_pass(context.device(), context.render_target().extent,
                            context.render_target().format);
    };
    callbacks.record_frame = [this](cubey::host::HeadlessPngContext&,
                                    const cubey::host::HeadlessCaptureFrame& frame,
                                    VkCommandBuffer command_buffer,
                                    const cubey::host::HeadlessRenderTarget& target) {
        record_furnace_frame(command_buffer, target, frame.frame_slot, false);
    };
    callbacks.shutdown = [this](cubey::host::HeadlessPngContext&) { destroy_all_resources(); };

    cubey::host::HeadlessPngHost host(std::move(host_config), std::move(callbacks));
    return host.run();
}

int run_pbr_furnace(const PbrFurnaceConfig& config) {
    PbrFurnaceApp app(config);
    return app.run();
}

} // namespace cubey::projects::pbr_furnace
