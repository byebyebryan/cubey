// Acceleration structure / pipeline / SBT flow adapted from Khronos Vulkan-Samples
// ray_tracing_basic, 56a72bd1c43268781a75d73a520e4458b6cac766.
// Copyright (c) 2019-2026, Sascha Willems. SPDX-License-Identifier: Apache-2.0
#include "raytracing_renderer.h"
#include "raytracing_environment.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <cubey/render/pass.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/shader_bytecode.h>
#include <cubey/vulkan/shader_module.h>
#include <cubey/vulkan/vk_check.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <limits>

namespace cubey::projects::raytracing {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::filesystem::path shader(const char* name) {
    return std::filesystem::path(CUBEY_RAYTRACING_SHADER_DIR) / name;
}
constexpr auto address_usage = VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
constexpr auto geometry_usage =
    VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | address_usage |
    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
constexpr auto host_memory =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr auto hdr_format = VK_FORMAT_R16G16B16A16_SFLOAT;
template <class T> T load(const vulkan::Device& d, const char* name) {
    const auto result = reinterpret_cast<T>(vkGetDeviceProcAddr(d.handle(), name));
    if (!result)
        throw std::runtime_error(std::string("missing RT device entry point: ") + name);
    return result;
}
void build_barrier(VkCommandBuffer command) {
    auto b = cubey::vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    b.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    b.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                         VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR |
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 1, &b, 0, nullptr, 0, nullptr);
}
} // namespace
CameraUniform camera_uniform(const Config& c, VkExtent2D extent) {
    const math::Vec3 target{c.target_x, c.target_y, c.target_z};
    // The new workshop follows forward's quaternion orbit convention (+pitch
    // lowers the eye). Legacy RT/SDF orbit controls use the opposite sign.
    const float pitch = c.scene == "materials" ? -c.pitch : c.pitch;
    const auto eye = target + math::Vec3{std::cos(pitch) * std::sin(c.yaw), std::sin(pitch),
                                         std::cos(pitch) * std::cos(c.yaw)} *
                                  c.distance;
    const auto inverse_view = glm::inverse(glm::lookAt(eye, target, math::Vec3{0, 1, 0}));
    const math::Vec3 sun{std::cos(c.sun_elevation) * std::sin(c.sun_azimuth),
                         std::sin(c.sun_elevation),
                         std::cos(c.sun_elevation) * std::cos(c.sun_azimuth)};
    const std::array<std::string_view, 18> views{
        "final",       "geometric-normal", "shading-normal", "depth",       "instance-id",
        "barycentric", "secondary-id",     "path-class",     "path-events", "transport-limit",
        "environment", "base-color",       "roughness",      "metallic",    "footprint",
        "diffuse",     "specular",         "sheen"};
    return {inverse_view,
            {eye, float(extent.width) / float(extent.height)},
            {sun, 0},
            {c.intensity, c.ambient, std::tan(glm::radians(c.fov * .5F)), 0},
            {c.red, c.green, c.blue, c.roughness},
            {c.shadows ? 1.F : 0.F,
             float(std::find(views.begin(), views.end(), c.view) - views.begin()), 0, 0}};
}
Renderer::Renderer(vulkan::Device& device, vulkan::GpuRuntime& gpu, const Scene& scene,
                   std::uint32_t slots, std::string_view fault, bool diagnostic,
                   bool collect_metrics, const Config* config)
    : device_(device), slots_(slots), executor_(slots), recorded_(slots), diagnostic_(diagnostic),
      collect_metrics_(collect_metrics) {
    if (!device.ray_tracing_enabled() || !slots || slots > 8)
        throw std::invalid_argument("RT renderer requires an enabled device and 1..8 slots");
    try {
        rt_ = {
            load<PFN_vkCreateAccelerationStructureKHR>(device, "vkCreateAccelerationStructureKHR"),
            load<PFN_vkDestroyAccelerationStructureKHR>(device,
                                                        "vkDestroyAccelerationStructureKHR"),
            load<PFN_vkGetAccelerationStructureBuildSizesKHR>(
                device, "vkGetAccelerationStructureBuildSizesKHR"),
            load<PFN_vkCmdBuildAccelerationStructuresKHR>(device,
                                                          "vkCmdBuildAccelerationStructuresKHR"),
            load<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
                device, "vkGetAccelerationStructureDeviceAddressKHR"),
            load<PFN_vkCreateRayTracingPipelinesKHR>(device, "vkCreateRayTracingPipelinesKHR"),
            load<PFN_vkGetRayTracingShaderGroupHandlesKHR>(device,
                                                           "vkGetRayTracingShaderGroupHandlesKHR"),
            load<PFN_vkCmdTraceRaysKHR>(device, "vkCmdTraceRaysKHR")};
        auto p = cubey::vulkan::vk_struct<VkPhysicalDeviceProperties2>(
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
        p.pNext = &properties_;
        properties_.pNext = &as_properties_;
        vkGetPhysicalDeviceProperties2(device.physical_device(), &p);
        create_scene(gpu, scene);
        if (scene.workshop) {
            workshop_routes_ = scene.routes;
            workshop_volumes_ = workshop_volumes(scene);
            for (const auto& mesh : scene.meshes)
                geometry_admission_.push_back(mesh_admission_identity(mesh));
            for (const auto& route : scene.routes) {
                std::uint64_t triangles = 0;
                for (std::size_t i = route.first; i < route.first + route.count; ++i)
                    triangles += scene.meshes.at(scene.instances.at(i).mesh).indices.size() / 3;
                route_identity_[route.name] = {{"first_instance", route.first},
                                               {"instances", route.count},
                                               {"triangles", triangles}};
            }
        }
        for (const auto& instance : scene.instances) {
            if (scene.workshop)
                break;
            if (scene.materials.at(instance.material).optics.x != 2)
                continue;
            if (!glass_planes_.empty())
                throw std::invalid_argument("RT optics admits one glass volume");
            const auto& mesh = scene.meshes.at(instance.mesh);
            for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
                const auto a =
                    math::Vec3(instance.transform * mesh.vertices[mesh.indices[i]].position);
                const auto b =
                    math::Vec3(instance.transform * mesh.vertices[mesh.indices[i + 1]].position);
                const auto c =
                    math::Vec3(instance.transform * mesh.vertices[mesh.indices[i + 2]].position);
                const auto normal = glm::normalize(glm::cross(b - a, c - a));
                glass_planes_.push_back({normal, -glm::dot(normal, a)});
            }
        }
        if (fault == "scene")
            throw std::runtime_error("owned RT creation fault: scene");
        const Config fallback_config;
        const auto& settings = config ? *config : fallback_config;
        if (scene.workshop)
            active_route_ = workshop_route(settings);
        dump_mode_ = dump_mode(settings);
        dump_stride_ = dump_stride(settings);
        material_values_ = scene.materials;
        workshop_ = scene.workshop;
        environment_ = std::make_unique<Environment>(device, gpu, settings, slots);
        auto start = Clock::now();
        create_pipeline();
        pipeline_ms_ = elapsed(start);
        if (fault == "pipeline")
            throw std::runtime_error("owned RT creation fault: pipeline");
        trace_timer_.emplace(device, slots, 1);
        whole_timer_.emplace(device, slots, 1);
        environment_timer_.emplace(device, slots, 1);
        total_timer_.emplace(device, slots, 1);
        post_.create_materials(device, {slots});
    } catch (...) {
        destroy();
        throw;
    }
}
Renderer::~Renderer() {
    destroy();
}
void Renderer::create_scene(vulkan::GpuRuntime& gpu, const Scene& scene) {
    auto start = Clock::now();
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<glm::uvec4> offsets;
    for (const auto& m : scene.meshes) {
        offsets.push_back(
            {vertices.size(), indices.size(), m.vertices.size(), m.indices.size() / 3});
        vertices.insert(vertices.end(), m.vertices.begin(), m.vertices.end());
        indices.insert(indices.end(), m.indices.begin(), m.indices.end());
    }
    auto upload = [&](const void* data, std::size_t size, VkBufferUsageFlags usage) {
        return std::make_unique<vulkan::Buffer>(
            vulkan::upload_device_buffer(gpu, data, size, usage));
    };
    vertices_ = upload(vertices.data(), vertices.size() * sizeof(Vertex), geometry_usage);
    indices_ = upload(indices.data(), indices.size() * 4, geometry_usage);
    std::vector<GpuInstance> shading;
    for (const auto& i : scene.instances) {
        auto geometry = offsets.at(i.mesh);
        if (i.material >= scene.materials.size())
            throw std::invalid_argument("invalid RT material index");
        geometry.z = i.material;
        shading.push_back(
            {i.transform, glm::transpose(glm::inverse(i.transform)), geometry, i.color});
        triangle_count_ += offsets.at(i.mesh).w;
    }
    instances_ = upload(shading.data(), shading.size() * sizeof(GpuInstance),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    geometry_bytes_ = vertices_->size() + indices_->size() + instances_->size();
    accelerations_.reserve(scene.meshes.size() + std::max(std::size_t(1), scene.routes.size()));
    gpu.submit(
           "raytracing.build-static-scene",
           [&](vulkan::GpuOwnerContext& context) {
               vulkan::GpuTimestampProfiler build_timer(device_, 1, 1);
               std::uint64_t transient_instance_bytes = 0;
               auto build_one = [&](VkAccelerationStructureTypeKHR type,
                                    VkAccelerationStructureGeometryKHR geometry,
                                    std::uint32_t count) {
                   auto info =
                       cubey::vulkan::vk_struct<VkAccelerationStructureBuildGeometryInfoKHR>(
                           VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR);
                   info.type = type;
                   info.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
                   info.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
                   info.geometryCount = 1;
                   info.pGeometries = &geometry;
                   auto sizes = cubey::vulkan::vk_struct<VkAccelerationStructureBuildSizesInfoKHR>(
                       VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR);
                   rt_.build_sizes(device_.handle(),
                                   VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &info, &count,
                                   &sizes);
                   Acceleration a;
                   a.storage = std::make_unique<vulkan::Buffer>(
                       device_,
                       vulkan::BufferConfig{sizes.accelerationStructureSize,
                                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                                address_usage,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
                   auto create = cubey::vulkan::vk_struct<VkAccelerationStructureCreateInfoKHR>(
                       VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR);
                   create.type = type;
                   create.size = sizes.accelerationStructureSize;
                   create.buffer = a.storage->handle();
                   vulkan::check(rt_.create_as(device_.handle(), &create, nullptr, &a.handle),
                                 "vkCreateAccelerationStructureKHR");
                   accelerations_.push_back(std::move(a));
                   auto& installed = accelerations_.back();
                   auto address =
                       cubey::vulkan::vk_struct<VkAccelerationStructureDeviceAddressInfoKHR>(
                           VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR);
                   address.accelerationStructure = installed.handle;
                   installed.address = rt_.as_address(device_.handle(), &address);
                   as_bytes_ += sizes.accelerationStructureSize;
                   const auto alignment =
                       as_properties_.minAccelerationStructureScratchOffsetAlignment;
                   vulkan::Buffer scratch(device_,
                                          {sizes.buildScratchSize + alignment,
                                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | address_usage,
                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT});
                   scratch_peak_ = std::max(scratch_peak_, std::uint64_t(scratch.size()));
                   requested_peak_ = std::max(requested_peak_, requested_bytes() + scratch.size() +
                                                                   transient_instance_bytes);
                   info.dstAccelerationStructure = installed.handle;
                   info.scratchData.deviceAddress =
                       align_address(scratch.device_address(), alignment);
                   VkAccelerationStructureBuildRangeInfoKHR range{};
                   range.primitiveCount = count;
                   const auto* range_ptr = &range;
                   vulkan::ImmediateCommands commands(context);
                   build_timer.begin_frame(commands.command_buffer(), 0);
                   build_timer.begin_pass(commands.command_buffer(), 0, "raytracing.as-build");
                   rt_.build(commands.command_buffer(), 1, &info, &range_ptr);
                   build_barrier(commands.command_buffer());
                   build_timer.end_pass(commands.command_buffer(), 0);
                   commands.submit_and_wait();
                   build_timer.collect(0);
                   for (const auto& sample : build_timer.latest_timings())
                       as_gpu_ms_ += sample.milliseconds;
               };
               auto build_start = Clock::now();
               for (const auto& o : offsets) {
                   auto geometry = cubey::vulkan::vk_struct<VkAccelerationStructureGeometryKHR>(
                       VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
                   geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
                   geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
                   auto& t = geometry.geometry.triangles;
                   t.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
                   t.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
                   t.vertexData.deviceAddress = vertices_->device_address() + o.x * sizeof(Vertex);
                   t.vertexStride = sizeof(Vertex);
                   t.maxVertex = o.z - 1;
                   t.indexType = VK_INDEX_TYPE_UINT32;
                   t.indexData.deviceAddress = indices_->device_address() + o.y * 4;
                   build_one(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, o.w);
               }
               auto routes = scene.routes;
               if (routes.empty())
                   routes.push_back(
                       {"legacy", 0, static_cast<std::uint32_t>(scene.instances.size())});
               for (const auto& route : routes) {
                   std::vector<VkAccelerationStructureInstanceKHR> data;
                   for (std::size_t n = route.first; n < route.first + route.count; ++n) {
                       const auto& i = scene.instances[n];
                       VkAccelerationStructureInstanceKHR instance{};
                       for (int r = 0; r < 3; ++r)
                           for (int c = 0; c < 4; ++c)
                               instance.transform.matrix[r][c] = i.transform[c][r];
                       instance.instanceCustomIndex = static_cast<std::uint32_t>(n);
                       instance.mask = scene.workshop
                                           ? 255
                                           : (scene.materials.at(i.material).optics.x == 2 ? 2 : 1);
                       instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                       instance.accelerationStructureReference = accelerations_.at(i.mesh).address;
                       data.push_back(instance);
                   }
                   vulkan::Buffer instance_data(
                       device_,
                       {data.size() * sizeof(data[0]),
                        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                            address_usage,
                        host_memory});
                   instance_data.upload(data.data(), instance_data.size());
                   transient_instance_bytes = instance_data.size();
                   auto geometry = cubey::vulkan::vk_struct<VkAccelerationStructureGeometryKHR>(
                       VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR);
                   geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
                   geometry.geometry.instances.sType =
                       VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
                   geometry.geometry.instances.data.deviceAddress = instance_data.device_address();
                   build_one(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, geometry,
                             static_cast<std::uint32_t>(data.size()));
                   routes_.emplace_back(route.name, accelerations_.size() - 1);
               }
               as_build_ms_ = elapsed(build_start);
           })
        .get();
    scene_ms_ = elapsed(start);
}
void Renderer::create_pipeline() {
    const VkShaderStageFlags stages = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                      VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                      VK_SHADER_STAGE_MISS_BIT_KHR;
    const std::array<vulkan::DescriptorSetBindingConfig, 18> bindings{
        {{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, stages},
         {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stages},
         {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages},
         {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages},
         {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages},
         {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {9, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {10, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {11, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {12, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {13, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {14, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {15, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {16, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {17, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_RAYGEN_BIT_KHR}}};
    descriptors_ = std::make_unique<vulkan::DescriptorSetArray>(
        device_, vulkan::DescriptorSetInfo(bindings, slots_));
    auto layout = cubey::vulkan::vk_struct<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    const auto descriptor_layout = descriptors_->layout();
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &descriptor_layout;
    vulkan::check(vkCreatePipelineLayout(device_.handle(), &layout, nullptr, &layout_),
                  "RT pipeline layout");
    std::vector<std::unique_ptr<vulkan::ShaderModule>> modules;
    std::array<VkPipelineShaderStageCreateInfo, 4> shader_stages{};
    const std::array<const char*, 4> names{"trace.rgen.spv", "primary.rmiss.spv",
                                           "shadow.rmiss.spv", "primary.rchit.spv"};
    const std::array<VkShaderStageFlagBits, 4> flags{
        VK_SHADER_STAGE_RAYGEN_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR, VK_SHADER_STAGE_MISS_BIT_KHR,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR};
    for (std::size_t i = 0; i < 4; ++i) {
        modules.push_back(std::make_unique<vulkan::ShaderModule>(
            device_, vulkan::read_spirv_file(shader(names[i]))));
        shader_stages[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                            nullptr,
                            0,
                            flags[i],
                            modules.back()->handle(),
                            "main",
                            nullptr};
    }
    std::array<VkRayTracingShaderGroupCreateInfoKHR, 4> groups{};
    for (std::uint32_t i = 0; i < 4; ++i) {
        auto& g = groups[i];
        g.sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        g.type = i == 3 ? VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR
                        : VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
        g.generalShader = i == 3 ? VK_SHADER_UNUSED_KHR : i;
        g.closestHitShader = i == 3 ? 3 : VK_SHADER_UNUSED_KHR;
        g.anyHitShader = VK_SHADER_UNUSED_KHR;
        g.intersectionShader = VK_SHADER_UNUSED_KHR;
    }
    auto info = cubey::vulkan::vk_struct<VkRayTracingPipelineCreateInfoKHR>(
        VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR);
    info.stageCount = 4;
    info.pStages = shader_stages.data();
    info.groupCount = 4;
    info.pGroups = groups.data();
    info.maxPipelineRayRecursionDepth = 1;
    info.layout = layout_;
    vulkan::check(rt_.create_pipeline(device_.handle(), VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &info,
                                      nullptr, &pipeline_),
                  "vkCreateRayTracingPipelinesKHR");
    const auto stride =
        sbt_stride(properties_.shaderGroupHandleSize, properties_.shaderGroupHandleAlignment,
                   properties_.shaderGroupBaseAlignment, properties_.maxShaderGroupStride);
    sbt_ = std::make_unique<vulkan::Buffer>(
        device_, vulkan::BufferConfig{4 * stride + properties_.shaderGroupBaseAlignment,
                                      VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | address_usage,
                                      host_memory});
    const auto base = align_address(sbt_->device_address(), properties_.shaderGroupBaseAlignment);
    std::vector<std::byte> handles(4 * properties_.shaderGroupHandleSize), bytes(sbt_->size());
    vulkan::check(rt_.handles(device_.handle(), pipeline_, 0, 4, handles.size(), handles.data()),
                  "vkGetRayTracingShaderGroupHandlesKHR");
    for (std::size_t i = 0; i < 4; ++i)
        std::memcpy(bytes.data() + (base - sbt_->device_address()) + i * stride,
                    handles.data() + i * properties_.shaderGroupHandleSize,
                    properties_.shaderGroupHandleSize);
    sbt_->upload(bytes.data(), bytes.size());
    raygen_ = {base, stride, stride};
    miss_ = {base + stride, stride, 2 * stride};
    hit_ = {base + 3 * stride, stride, stride};
    for (std::uint32_t i = 0; i < slots_; ++i) {
        uniforms_.push_back(std::make_unique<vulkan::Buffer>(
            device_, vulkan::BufferConfig{sizeof(CameraUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                          host_memory}));
        environment_uniforms_.push_back(std::make_unique<vulkan::Buffer>(
            device_, vulkan::BufferConfig{sizeof(EnvironmentUniform),
                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host_memory}));
        material_buffers_.push_back(std::make_unique<vulkan::Buffer>(
            device_, vulkan::BufferConfig{material_values_.size() * sizeof(OpticalMaterial),
                                          VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_memory}));
        recipe_buffers_.push_back(std::make_unique<vulkan::Buffer>(
            device_,
            vulkan::BufferConfig{(workshop_ ? workshop_->recipes.size() : 1) * sizeof(GpuRecipe),
                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_memory}));
        workshop_uniforms_.push_back(std::make_unique<vulkan::Buffer>(
            device_, vulkan::BufferConfig{sizeof(WorkshopUniform),
                                          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, host_memory}));
        environment_->bind(device_, descriptors_->set(i), i);
        vulkan::DescriptorWriteBatch writes;
        writes
            .storage_buffer(descriptors_->set(i), 13, recipe_buffers_.back()->handle(),
                            recipe_buffers_.back()->size())
            .uniform_buffer(descriptors_->set(i), 14, workshop_uniforms_.back()->handle(),
                            sizeof(WorkshopUniform))
            .uniform_buffer(descriptors_->set(i), 11, environment_uniforms_.back()->handle(),
                            sizeof(EnvironmentUniform))
            .storage_buffer(descriptors_->set(i), 12, material_buffers_.back()->handle(),
                            material_buffers_.back()->size())
            .uniform_buffer(descriptors_->set(i), 2, uniforms_.back()->handle(),
                            sizeof(CameraUniform))
            .storage_buffer(descriptors_->set(i), 3, vertices_->handle(), vertices_->size())
            .storage_buffer(descriptors_->set(i), 4, indices_->handle(), indices_->size())
            .storage_buffer(descriptors_->set(i), 5, instances_->handle(), instances_->size())
            .update(device_);
        auto acceleration = cubey::vulkan::vk_struct<VkWriteDescriptorSetAccelerationStructureKHR>(
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
        acceleration.accelerationStructureCount = 1;
        acceleration.pAccelerationStructures = &accelerations_.back().handle;
        auto write =
            cubey::vulkan::vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.pNext = &acceleration;
        write.dstSet = descriptors_->set(i);
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
    }
}
void Renderer::resize(VkExtent2D extent, VkFormat format, bool fault) {
    if (extent.width == extent_.width && extent.height == extent_.height &&
        format == output_format_)
        return;
    if (!device_.supports_image_format_features(
            hdr_format, VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        throw std::runtime_error("RT requires RGBA16F storage and sampled images");
    if (workshop_ && !device_.supports_image_format_features(VK_FORMAT_R32G32B32A32_SFLOAT,
                                                             VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
        throw std::runtime_error("RT workshop requires RGBA32F storage history");
    std::vector<Target> next;
    for (std::uint32_t i = 0; i < slots_; ++i) {
        next.push_back(
            {std::make_unique<render::Texture2D>(
                 device_, render::Texture2DConfig{.extent = extent,
                                                  .format = hdr_format,
                                                  .usage = render::Texture2DUsage::StorageSampled,
                                                  .create_sampler = false,
                                                  .sampler = {}}),
             std::make_unique<vulkan::Buffer>(
                 device_,
                 vulkan::BufferConfig{
                     diagnostic_ ? std::uint64_t(extent.width) * extent.height * dump_stride_ : 64,
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_memory})});
        if (fault)
            throw std::runtime_error("owned RT creation fault: target");
    }
    const std::array<render::ShaderStageFile, 2> shaders{
        render::vertex_shader_file(shader("forward_pbr_post.vert.spv")),
        render::fragment_shader_file(shader("forward_pbr_post.frag.spv"))};
    post_.create_pipeline(device_, {extent, format, shaders});
    environment_->resize_background(device_, extent);
    const auto history_extent = workshop_ ? extent : VkExtent2D{1, 1};
    auto next_history = std::make_unique<vulkan::Image>(
        device_,
        vulkan::ImageConfig{.extent = {history_extent.width, history_extent.height, 1},
                            .format = VK_FORMAT_R32G32B32A32_SFLOAT,
                            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                            .aspect = VK_IMAGE_ASPECT_COLOR_BIT});
    std::uint64_t next_bytes = 0;
    for (const auto& target : next)
        next_bytes += std::uint64_t(extent.width) * extent.height * 8 + target.queries->size();
    next_bytes += std::uint64_t(history_extent.width) * history_extent.height * 16;
    requested_peak_ = std::max(requested_peak_, requested_bytes() + next_bytes);
    targets_ = std::move(next);
    history_ = std::move(next_history);
    history_initialized_ = false;
    history_identity_.clear();
    history_samples_ = 0;
    extent_ = extent;
    output_format_ = format;
    ++target_generations_;
    for (std::uint32_t i = 0; i < slots_; ++i) {
        vulkan::DescriptorWriteBatch{}
            .storage_image(descriptors_->set(i), 1, targets_[i].color->view())
            .storage_image(descriptors_->set(i), 15, history_->view())
            .storage_buffer(descriptors_->set(i), 6, targets_[i].queries->handle(),
                            targets_[i].queries->size())
            .update(device_);
        environment_->bind(device_, descriptors_->set(i), i);
    }
}
void Renderer::collect(std::uint32_t slot) {
    if (!recorded_[slot])
        return;
    for (auto* timer : {&*trace_timer_, &*whole_timer_, &*environment_timer_, &*total_timer_}) {
        timer->collect(slot);
        for (const auto& t : timer->latest_timings()) {
            if (profile_)
                profile_->record_gpu_span(*recorded_[slot], t.label, t.milliseconds);
            if (collect_metrics_) {
                if (timing_.size() < 20000)
                    timing_.push_back({{"frame", *recorded_[slot]},
                                       {"pass", t.label},
                                       {"milliseconds", t.milliseconds}});
                else
                    ++dropped_samples_;
            }
        }
    }
    recorded_[slot].reset();
}
void Renderer::render(VkCommandBuffer command, render::ColorTargetView output,
                      render::FrameSlot slot, const Config& c, bool present,
                      profiling::ProfileRecorder* profile, std::uint64_t frame) {
    collect(slot.index);
    profile_ = profile;
    if (workshop_) {
        active_route_ = workshop_route(c);
        auto route = std::find_if(routes_.begin(), routes_.end(),
                                  [&](const auto& r) { return r.first == workshop_route(c); });
        if (route == routes_.end())
            throw std::invalid_argument("unprepared workshop specimen");
        auto acceleration = vulkan::vk_struct<VkWriteDescriptorSetAccelerationStructureKHR>(
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR);
        acceleration.accelerationStructureCount = 1;
        acceleration.pAccelerationStructures = &accelerations_.at(route->second).handle;
        auto write =
            vulkan::vk_struct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        write.pNext = &acceleration;
        write.dstSet = descriptors_->set(slot.index);
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        vkUpdateDescriptorSets(device_.handle(), 1, &write, 0, nullptr);
    }
    auto camera = camera_uniform(c, extent_);
    if (workshop_)
        validate_workshop_view(workshop_volumes_, workshop_routes_, *workshop_, c,
                               math::Vec3(camera.eye));
    if (!glass_planes_.empty() &&
        std::all_of(glass_planes_.begin(), glass_planes_.end(), [&](auto p) {
            return glm::dot(p, math::Vec4(math::Vec3(camera.eye), 1)) <= 1e-6F;
        }))
        throw std::invalid_argument(
            "camera-inside-glass is unsupported; move outside the closed volume");
    vulkan::CommandRecorder recorder(command);
    if (present)
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
    total_timer_->begin_frame(command, slot.index);
    environment_timer_->begin_frame(command, slot.index);
    total_timer_->begin_pass(command, slot.index, "raytracing.total_frame");
    environment_timer_->begin_pass(command, slot.index, "raytracing.environment_update");
    auto sun = math::Vec3(camera.sun);
    const auto environment = environment_->prepare(c, command, slot, math::Vec3(camera.eye), sun);
    camera.sun = math::Vec4(sun, 0);
    environment_->record_background(command, slot, camera.inverse_view, camera.eye.w,
                                    camera.light.z);
    environment_->bind(device_, descriptors_->set(slot.index), slot.index);
    environment_uniforms_.at(slot.index)->upload(&environment, sizeof(environment));
    auto materials = material_values_;
    for (auto& material : materials) {
        if (material.optics.w > 0) {
            material.base_roughness = {c.red, c.green, c.blue, c.roughness};
            if (c.scene == "reflection-dragon-solo") {
                material.optics.x = c.subject_material == "mirror" ? 1.F : 0.F;
                material.reflectance = {c.red, c.green, c.blue, 1};
            }
        }
        if (material.optics.x == 2 && !workshop_) {
            material.optics.y = c.ior;
            material.optics.z = c.attenuation_distance;
            material.attenuation = {c.attenuation_red, c.attenuation_green, c.attenuation_blue, 1};
        }
    }
    material_buffers_.at(slot.index)
        ->upload(materials.data(), materials.size() * sizeof(OpticalMaterial));
    environment_timer_->end_pass(command, slot.index);
    camera.options.z = diagnostic_ ? float(dump_mode_) : 0.F;
    const bool contract_probe =
        c.material_contract || c.bsdf_contract || c.optics_contract || c.dielectric_contract;
    camera.options.w = c.dielectric_contract ? 4.F
                       : c.material_contract
                           ? 3.F
                           : (c.bsdf_contract ? 2.F : (c.optics_contract ? 1.F : 0.F));
    WorkshopUniform workshop_uniform;
    if (workshop_) {
        const auto records = workshop_->gpu_records();
        recipe_buffers_.at(slot.index)->upload(records.data(), records.size() * sizeof(GpuRecipe));
        workshop_uniform.options = {c.optical_pair != "none"  ? 3U
                                    : c.material == "gallery" ? 2U
                                                              : 1U,
                                    workshop_->selected, c.pattern ? 1U : 0U, c.bump ? 1U : 0U};
        const std::array<std::string_view, 4> shapes{"sphere", "block", "swatch", "cut-block"};
        workshop_uniform.sampling = {
            c.seed, c.samples,
            c.optical_pair != "none" ? 4U
            : c.material == "gallery"
                ? 0U
                : static_cast<std::uint32_t>(std::find(shapes.begin(), shapes.end(), c.specimen) -
                                             shapes.begin()),
            0};
        workshop_uniform.solar = {
            environment_->solar_radius(), c.workshop_lighting == "sun" ? 0.F : 1.F,
            c.workshop_lighting == "sky" || c.workshop_lighting == "furnace" ? 0.F : 1.F,
            c.workshop_lighting == "furnace" ? 1.F : 0.F};
    }
    workshop_uniforms_.at(slot.index)->upload(&workshop_uniform, sizeof(workshop_uniform));
    uniforms_.at(slot.index)->upload(&camera, sizeof(camera));
    post_.upload(slot, render::hdr_post_uniforms(output.format, c.exposure,
                                                 c.view == "final" ? render::PbrTonemap::Aces
                                                                   : render::PbrTonemap::Linear));
    whole_timer_->begin_frame(command, slot.index);
    trace_timer_->begin_frame(command, slot.index);
    whole_timer_->begin_pass(command, slot.index, "raytracing.whole_scene");
    auto& target = targets_.at(slot.index);
    auto barrier =
        cubey::vulkan::vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    barrier.oldLayout =
        target.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcAccessMask = target.initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = target.color->handle();
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command,
                         target.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                            : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0, nullptr, 1,
                         &barrier);
    trace_timer_->begin_pass(command, slot.index, "raytracing.trace");
    std::vector<std::byte> identity;
    auto append_identity = [&](const auto& value) {
        const auto* begin = reinterpret_cast<const std::byte*>(&value);
        identity.insert(identity.end(), begin, begin + sizeof(value));
    };
    append_identity(camera);
    append_identity(environment);
    append_identity(workshop_uniform.options);
    append_identity(workshop_uniform.solar);
    append_identity(c.seed);
    if (workshop_)
        append_identity(environment_->generation());
    if (workshop_)
        for (char ch : c.specimen)
            append_identity(ch);
    if (workshop_)
        append_identity(workshop_->revision);
    const bool reset_history =
        !history_initialized_ || (workshop_ && identity != history_identity_);
    if (reset_history) {
        history_samples_ = 0;
        history_identity_ = std::move(identity);
        if (workshop_)
            ++history_epochs_;
    }
    auto history_barrier =
        vulkan::vk_struct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    history_barrier.oldLayout =
        history_initialized_ ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_UNDEFINED;
    history_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    history_barrier.srcAccessMask =
        history_initialized_ ? VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT : 0;
    history_barrier.dstAccessMask = reset_history
                                        ? VK_ACCESS_TRANSFER_WRITE_BIT
                                        : VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    history_barrier.srcQueueFamilyIndex = history_barrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    history_barrier.image = history_->handle();
    history_barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(command,
                         history_initialized_ ? VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR
                                              : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         reset_history ? VK_PIPELINE_STAGE_TRANSFER_BIT
                                       : VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                         0, 0, nullptr, 0, nullptr, 1, &history_barrier);
    if (reset_history) {
        const VkClearColorValue zero{};
        vkCmdClearColorImage(command, history_->handle(), VK_IMAGE_LAYOUT_GENERAL, &zero, 1,
                             &history_barrier.subresourceRange);
        history_barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        history_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        history_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 0, nullptr, 0,
                             nullptr, 1, &history_barrier);
    }
    history_initialized_ = true;
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout_, 0,
                                 descriptors_->set(slot.index));
    const auto samples_before = history_samples_;
    const auto iterations =
        workshop_ && !contract_probe && c.common.headless && !c.common.headless_render_only
            ? std::max(1U, c.samples > history_samples_ ? c.samples - history_samples_ : 0U)
            : 1U;
    for (std::uint32_t sample = 0; sample < iterations; ++sample) {
        if (sample > 0) {
            auto dependency = vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
            dependency.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                                 VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR, 0, 1, &dependency, 0,
                                 nullptr, 0, nullptr);
        }
        rt_.trace(command, &raygen_, &miss_, &hit_, &callable_, extent_.width, extent_.height, 1);
        if (workshop_ && !contract_probe && history_samples_ < c.samples)
            ++history_samples_;
    }
    if (workshop_ && collect_metrics_ && sampling_frames_.size() < 20000)
        sampling_frames_.push_back(
            {{"frame", frame},
             {"epoch", history_epochs_},
             {"samples_before", samples_before},
             {"samples_after", history_samples_},
             {"new_samples", history_samples_ - samples_before},
             {"recorded_dispatches", iterations},
             {"material_revision", workshop_->revision},
             {"selected_material", workshop_->catalog.at(workshop_->selected).id},
             {"specimen", c.specimen},
             {"resident_requested_bytes", requested_bytes()},
             {"environment_generation", environment_->generation()},
             {"exposure", c.exposure},
             {"sample_target", c.samples}});
    if (diagnostic_) {
        auto memory = vulkan::vk_struct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        memory.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        memory.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
    }
    trace_timer_->end_pass(command, slot.index);
    render::RenderGraphBuilder builder;
    const auto scene = builder.import_texture(
        render::hdr_scene_color_texture_desc("raytracing.hdr", extent_, hdr_format),
        target.color->handle(), target.color->view(),
        render::RenderGraphTextureState{VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
                                        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR});
    const auto out = builder.import_color_target(
        "raytracing.output", output,
        present ? render::RenderGraphTextureState{VK_IMAGE_LAYOUT_UNDEFINED, 0,
                                                  VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT}
                : render::render_graph_color_attachment_texture_state(),
        present ? render::render_graph_present_texture_state()
                : render::render_graph_color_attachment_texture_state());
    builder.add_pass("raytracing.post", render::RenderGraphQueueDomain::Graphics)
        .read_texture(scene)
        .write_color(out)
        .execute([&](const render::RenderGraphExecutionContext& context) {
            post_.record_pass(context.recorder(), render::resolved_color_target_view(context, out),
                              slot);
        });
    const auto graph = builder.compile();
    executor_.record(
        {.device = &device_,
         .command_buffer = command,
         .frame_slot = slot,
         .command_buffer_mode = render::RenderGraphCommandBufferMode::AlreadyRecording},
        graph, [&](const render::RenderGraphResourceSet& resources) {
            post_.update_scene_color_descriptor(device_, slot, graph, resources, scene);
        });
    whole_timer_->end_pass(command, slot.index);
    total_timer_->end_pass(command, slot.index);
    recorded_[slot.index] = frame;
    target.initialized = true;
    last_slot_ = slot.index;
    if (present)
        recorder.end("vkEndCommandBuffer raytracing");
}
void Renderer::finish() {
    for (std::uint32_t i = 0; i < slots_; ++i)
        collect(i);
    profile_ = nullptr;
}
void Renderer::save_queries(const std::filesystem::path& path) const {
    if (!diagnostic_ || !last_slot_)
        throw std::runtime_error("no completed RT geometry queries");
    const auto& buffer = *targets_.at(*last_slot_).queries;
    std::vector<std::byte> data(buffer.size());
    buffer.download(data.data(), data.size());
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out)
        throw std::runtime_error("cannot write RT query data");
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}
std::uint64_t Renderer::requested_bytes() const {
    std::uint64_t bytes = 0;
    for (auto* buffer : {vertices_.get(), indices_.get(), instances_.get(), sbt_.get()})
        if (buffer)
            bytes += buffer->size();
    for (const auto& acceleration : accelerations_)
        bytes += acceleration.storage->size();
    for (const auto& uniform : uniforms_)
        bytes += uniform->size();
    for (const auto& target : targets_)
        bytes += std::uint64_t(extent_.width) * extent_.height * 8 + target.queries->size();
    for (const auto& buffer : material_buffers_)
        bytes += buffer->size();
    for (const auto& buffer : environment_uniforms_)
        bytes += buffer->size();
    for (const auto& buffer : recipe_buffers_)
        bytes += buffer->size();
    for (const auto& buffer : workshop_uniforms_)
        bytes += buffer->size();
    if (history_)
        bytes += std::uint64_t(history_->extent().width) * history_->extent().height * 16;
    return bytes;
}
nlohmann::json Renderer::metrics() const {
    auto supported = nlohmann::json::array();
    if (workshop_)
        for (const auto& entry : workshop_->catalog)
            supported.push_back(entry.id);
    const auto memory = device_.device_memory_budget();
    return {
        {"format", "cubey.raytracing.metrics"},
        {"workshop_enabled", bool(workshop_)},
        {"workshop_sampling",
         {{"primary_samples", history_samples_},
          {"rough_glass_secondary_trials", 10},
          {"fresnel_split_depths", 2},
          {"budget_scope", "all branches and trials of one primary sample"}}},
        {"workshop_supported_material_ids", supported},
        {"history_samples", history_samples_},
        {"history_epochs", history_epochs_},
        {"extent", {extent_.width, extent_.height}},
        {"sampling_frames", sampling_frames_},
        {"workshop_materials", workshop_ ? workshop_->identity() : nlohmann::json::array()},
        {"geometry_admission", geometry_admission_},
        {"scene_routes", route_identity_},
        {"active_route", active_route_},
        {"active_triangles", workshop_ ? route_identity_.at(active_route_).at("triangles")
                                       : nlohmann::json(triangle_count_)},
        {"version", workshop_ ? 3 : 2},
        {"workshop_optics_dump_statistics",
         {{"scope", "path optics-output only; standalone contract probes use their own layouts"},
          {"1.w", "last sample trunk boundary events"},
          {"2.w", "terminated branches sum"},
          {"3.w", "rejected continuations sum"},
          {"10.w", "total queries sum"},
          {"11.rgb", "discarded throughput sum"},
          {"11.w", "status bitwise OR"},
          {"14.z", "root conditional trials sum"},
          {"14.w", "primary samples with transport status"},
          {"15.x", "radiance queries sum"},
          {"15.y", "maximum depth"},
          {"15.z", "primary samples sum"},
          {"15.w", "visibility queries sum"}}},
        {"environment_setup_ms", environment_ ? environment_->setup_ms() : 0},
        {"environment_probe_build_gpu_ms", environment_ ? environment_->probe_gpu_ms() : 0},
        {"environment_pipeline_setup_ms", environment_ ? environment_->pipeline_ms() : 0},
        {"environment_fallback_setup_ms", environment_ ? environment_->fallback_ms() : 0},
        {"environment_generation", environment_ ? environment_->generation() : 0},
        {"material_bytes", material_values_.size() * sizeof(OpticalMaterial) * slots_},
        {"environment_uniform_bytes", sizeof(EnvironmentUniform) * slots_},
        {"environment_texture_allocation_bytes",
         environment_ ? environment_->texture_allocation_bytes(device_) : 0},
        {"device", device_.device_name()},
        {"triangles", triangle_count_},
        {"scene_upload_and_build_ms", scene_ms_},
        {"as_build_submission_wall_ms", as_build_ms_},
        {"as_build_gpu_ms", as_gpu_ms_},
        {"pipeline_setup_ms", pipeline_ms_},
        {"geometry_bytes", geometry_bytes_},
        {"as_bytes", as_bytes_},
        {"sbt_bytes", sbt_ ? sbt_->size() : 0},
        {"scratch_peak_bytes", scratch_peak_},
        {"resident_project_requested_bytes", requested_bytes()},
        {"peak_simultaneous_project_requested_bytes", requested_peak_},
        {"project_memory_scope", "requested RT buffers/images including build scratch, TLAS inputs "
                                 "and resize overlap; excludes host targets, upload staging, "
                                 "driver allocation padding, shared environment textures/uniform "
                                 "internals and pipeline/descriptor internals"},
        {"target_bytes", std::uint64_t(extent_.width) * extent_.height * 8 * slots_},
        {"workshop_history_bytes",
         workshop_ && history_ ? std::uint64_t(extent_.width) * extent_.height * 16 : 0},
        {"workshop_recipe_bytes",
         workshop_ ? workshop_->recipes.size() * sizeof(GpuRecipe) * slots_ : 0},
        {"workshop_uniform_bytes", sizeof(WorkshopUniform) * slots_},
        {"observed_device_local_usage_bytes",
         memory.available ? nlohmann::json(memory.device_local_usage) : nlohmann::json(nullptr)},
        {"memory_usage_scope", "VK_EXT_memory_budget estimate of current-process device-local heap "
                               "allocations at shutdown; not a peak"},
        {"query_bytes", diagnostic_
                            ? std::uint64_t(extent_.width) * extent_.height * dump_stride_ * slots_
                            : 64 * slots_},
        {"uniform_bytes", sizeof(CameraUniform) * slots_},
        {"target_generations", target_generations_},
        {"as_builds", accelerations_.size()},
        {"dropped_gpu_samples", dropped_samples_},
        {"gpu_samples", timing_}};
}
void Renderer::destroy() noexcept {
    executor_.clear();
    targets_.clear();
    history_.reset();
    post_.destroy();
    trace_timer_.reset();
    whole_timer_.reset();
    environment_timer_.reset();
    total_timer_.reset();
    environment_.reset();
    environment_uniforms_.clear();
    material_buffers_.clear();
    recipe_buffers_.clear();
    workshop_uniforms_.clear();
    if (pipeline_)
        vkDestroyPipeline(device_.handle(), pipeline_, nullptr);
    if (layout_)
        vkDestroyPipelineLayout(device_.handle(), layout_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    layout_ = VK_NULL_HANDLE;
    descriptors_.reset();
    sbt_.reset();
    uniforms_.clear();
    for (auto& a : accelerations_)
        if (a.handle && rt_.destroy_as)
            rt_.destroy_as(device_.handle(), a.handle, nullptr);
    accelerations_.clear();
    instances_.reset();
    indices_.reset();
    vertices_.reset();
}
} // namespace cubey::projects::raytracing
