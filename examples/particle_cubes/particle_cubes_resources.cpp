#include "particle_cubes_app_internal.h"

#include "../common/forward_pass.h"

#include <cubey/render/color_space.h>
#include <cubey/render/material.h>
#include <cubey/render/primitive_mesh.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace cubey::examples::particle_cubes {
namespace {

constexpr float kGoldenAngle = 2.39996314F;

std::filesystem::path shader_path(const char* filename) {
    return std::filesystem::path(CUBEY_PARTICLE_CUBES_SHADER_DIR) / filename;
}

cubey::render::MaterialPassInfo particle_cubes_forward_pass_info() {
    const VkPushConstantRange draw_push_constant{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
        .offset = 0,
        .size = sizeof(DrawPushConstants),
    };
    return cubey::render::MaterialPassInfo{
        .label = "particle_cubes.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .push_constants = {draw_push_constant},
        .depth_test = true,
        .depth_write = true,
    };
}

[[nodiscard]] float hash01(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return static_cast<float>(value & 0x00FFFFFFU) / static_cast<float>(0x01000000U);
}

[[nodiscard]] std::vector<ParticleCubeGpu> make_initial_particle_cubes() {
    std::vector<ParticleCubeGpu> cubes;
    cubes.reserve(kParticleCubeCount);

    for (std::uint32_t i = 0; i < kParticleCubeCount; ++i) {
        const float rank =
            (static_cast<float>(i) + 0.5F) / static_cast<float>(kParticleCubeCount);
        const float angle = static_cast<float>(i) * kGoldenAngle;
        const float radius = std::sqrt(rank) * 2.15F;
        const float heat = hash01((i * 277803737U) + 1013904223U);
        const float x = std::cos(angle) * radius;
        const float z = std::sin(angle) * radius;
        const float y = (hash01((i * 747796405U) + 2891336453U) - 0.5F) * 1.4F;
        const float scale =
            kParticleCubeMinScale +
            (hash01((i * 1597334677U) + 3812015801U) * kParticleCubeScaleRange);
        const cubey::math::Vec4 color = cubey::render::srgb_to_linear_rgba({
            0.38F + (heat * 0.45F),
            0.74F - (heat * 0.22F),
            0.92F - (heat * 0.38F),
            1.0F,
        });

        cubes.push_back({
            .position_scale = {x, y, z, scale},
            .velocity_seed = {-z * 0.055F, 0.0F, x * 0.055F, heat},
            .color = {color.r, color.g, color.b, color.a},
        });
    }

    return cubes;
}

} // namespace

void ParticleCubesApp::create_global_resources_if_needed(
    cubey::host::WindowedAppContext& context) {
    if (particle_buffer_.has_value()) {
        return;
    }

    create_cube_mesh(context);
    create_particle_buffer(context);
    create_descriptor_resources(context);
    create_compute_resources(context);
}

void ParticleCubesApp::create_cube_mesh(cubey::host::WindowedAppContext& context) {
    const cubey::render::PrimitiveMeshData<cubey::render::VertexPositionColorNormal> mesh_data =
        cubey::render::make_cube_position_color_normal_mesh({
            .half_extent = 1.0F,
        });
    cube_mesh_.emplace(context.gpu(), mesh_data.mesh_config());
}

void ParticleCubesApp::create_particle_buffer(cubey::host::WindowedAppContext& context) {
    const std::vector<ParticleCubeGpu> cubes = make_initial_particle_cubes();
    const VkDeviceSize byte_size =
        static_cast<VkDeviceSize>(cubes.size() * sizeof(ParticleCubeGpu));
    particle_buffer_.emplace(cubey::vulkan::upload_device_buffer(
        context.gpu(), cubes.data(), byte_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
}

void ParticleCubesApp::create_descriptor_resources(cubey::host::WindowedAppContext& context) {
    const std::array<cubey::vulkan::DescriptorSetBindingConfig, 1> bindings{{
        {
            .binding = 0,
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .stage_flags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        },
    }};
    const cubey::vulkan::DescriptorSetInfo info(bindings);
    descriptors_.emplace(context.device(), info);
    update_particle_descriptor(context);
}

void ParticleCubesApp::update_particle_descriptor(cubey::host::WindowedAppContext& context) {
    cubey::vulkan::DescriptorWriteBatch descriptor_writes;
    descriptor_writes.storage_buffer(descriptors().set(), 0, particle_buffer().handle(),
                                     particle_buffer().size());
    descriptor_writes.update(context.device());
}

void ParticleCubesApp::reset_particle_buffer(cubey::host::WindowedAppContext& context) {
    context.gpu().wait_queue_idle("vkQueueWaitIdle before particle cube reset");
    particle_buffer_.reset();
    create_particle_buffer(context);
    update_particle_descriptor(context);
    reset_cubes_requested_ = false;
}

void ParticleCubesApp::create_compute_resources(cubey::host::WindowedAppContext& context) {
    const VkPushConstantRange compute_push_constant{
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(ComputePushConstants),
    };
    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
    const std::array<VkPushConstantRange, 1> push_constants{compute_push_constant};
    compute_pipeline_resource_.emplace(
        context.device(), cubey::render::ComputePipelineResourceConfig{
                              .shader_stage = cubey::render::compute_shader_file(
                                  shader_path("particle_cubes.comp.spv")),
                              .descriptor_set_layouts = set_layouts,
                              .push_constants = push_constants,
                          });
}

void ParticleCubesApp::create_forward_pass(cubey::host::WindowedAppContext& context) {
    const std::array<VkDescriptorSetLayout, 1> set_layouts{descriptors().layout()};
    cubey::examples::common::emplace_forward_scene_pass_3d(
        forward_pass_, context.device(),
        {
            .extent = context.swapchain().extent(),
            .color_format = context.swapchain().format(),
            .vertex_shader = shader_path("particle_cubes.vert.spv"),
            .fragment_shader = shader_path("particle_cubes.frag.spv"),
            .vertex_input = cubey::render::vertex_position_color_normal_input_layout(),
            .descriptor_set_layouts = set_layouts,
            .material_pass = particle_cubes_forward_pass_info(),
            .clear =
                {
                    .color = cubey::render::color_clear_value(0.012F, 0.015F, 0.02F, 1.0F),
                    .depth = cubey::render::depth_clear_value(),
                },
        });
}

void ParticleCubesApp::destroy_swapchain_resources() {
    forward_pass_.reset();
}

void ParticleCubesApp::destroy_all_resources() {
    destroy_swapchain_resources();
    compute_pipeline_resource_.reset();
    descriptors_.reset();
    particle_buffer_.reset();
    cube_mesh_.reset();
}

const cubey::render::Mesh& ParticleCubesApp::cube_mesh() const {
    if (!cube_mesh_.has_value()) {
        throw std::runtime_error("particle cube mesh is not initialized");
    }
    return cube_mesh_.value();
}

const cubey::vulkan::DescriptorSetBundle& ParticleCubesApp::descriptors() const {
    if (!descriptors_.has_value()) {
        throw std::runtime_error("particle cube descriptors are not initialized");
    }
    return descriptors_.value();
}

const cubey::vulkan::Buffer& ParticleCubesApp::particle_buffer() const {
    if (!particle_buffer_.has_value()) {
        throw std::runtime_error("particle cube buffer is not initialized");
    }
    return particle_buffer_.value();
}

const cubey::render::ComputePipelineResource& ParticleCubesApp::compute_pipeline_resource() const {
    if (!compute_pipeline_resource_.has_value()) {
        throw std::runtime_error("particle cube compute pipeline is not initialized");
    }
    return compute_pipeline_resource_.value();
}

const cubey::render::ForwardScenePass3D& ParticleCubesApp::forward_pass() const {
    if (!forward_pass_.has_value()) {
        throw std::runtime_error("particle cube forward pass is not initialized");
    }
    return forward_pass_.value();
}

} // namespace cubey::examples::particle_cubes
