// Acceleration structure / pipeline / SBT flow adapted from Khronos Vulkan-Samples
// ray_tracing_basic, 56a72bd1c43268781a75d73a520e4458b6cac766.
// Copyright (c) 2019-2026, Sascha Willems. SPDX-License-Identifier: Apache-2.0
#include "raytracing_renderer.h"
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
    const math::Vec3 target{0, c.target_y, 0};
    const auto eye = target + math::Vec3{std::cos(c.pitch) * std::sin(c.yaw), std::sin(c.pitch),
                                         std::cos(c.pitch) * std::cos(c.yaw)} *
                                  c.distance;
    const auto inverse_view = glm::inverse(glm::lookAt(eye, target, math::Vec3{0, 1, 0}));
    const math::Vec3 sun{std::cos(c.sun_elevation) * std::sin(c.sun_azimuth),
                         std::sin(c.sun_elevation),
                         std::cos(c.sun_elevation) * std::cos(c.sun_azimuth)};
    const std::array<std::string_view, 6> views{"final", "geometric-normal", "shading-normal",
                                                "depth", "instance-id",      "barycentric"};
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
                   bool collect_metrics)
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
        if (fault == "scene")
            throw std::runtime_error("owned RT creation fault: scene");
        auto start = Clock::now();
        create_pipeline();
        pipeline_ms_ = elapsed(start);
        if (fault == "pipeline")
            throw std::runtime_error("owned RT creation fault: pipeline");
        trace_timer_.emplace(device, slots, 1);
        whole_timer_.emplace(device, slots, 1);
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
        shading.push_back(
            {i.transform, glm::transpose(glm::inverse(i.transform)), offsets.at(i.mesh), i.color});
        triangle_count_ += offsets.at(i.mesh).w;
    }
    instances_ = upload(shading.data(), shading.size() * sizeof(GpuInstance),
                        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    geometry_bytes_ = vertices_->size() + indices_->size() + instances_->size();
    accelerations_.reserve(scene.meshes.size() + 1);
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
               std::vector<VkAccelerationStructureInstanceKHR> data;
               for (std::size_t n = 0; n < scene.instances.size(); ++n) {
                   const auto& i = scene.instances[n];
                   VkAccelerationStructureInstanceKHR instance{};
                   for (int r = 0; r < 3; ++r)
                       for (int c = 0; c < 4; ++c)
                           instance.transform.matrix[r][c] = i.transform[c][r];
                   instance.instanceCustomIndex = static_cast<std::uint32_t>(n);
                   instance.mask = 255;
                   instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
                   instance.accelerationStructureReference = accelerations_.at(i.mesh).address;
                   data.push_back(instance);
               }
               vulkan::Buffer instance_data(
                   device_, {data.size() * sizeof(data[0]),
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
               as_build_ms_ = elapsed(build_start);
           })
        .get();
    scene_ms_ = elapsed(start);
}
void Renderer::create_pipeline() {
    const VkShaderStageFlags stages = VK_SHADER_STAGE_RAYGEN_BIT_KHR |
                                      VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
                                      VK_SHADER_STAGE_MISS_BIT_KHR;
    const std::array<vulkan::DescriptorSetBindingConfig, 7> bindings{
        {{0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, stages},
         {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR},
         {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stages},
         {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages},
         {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages},
         {5, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stages},
         {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_RAYGEN_BIT_KHR}}};
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
        vulkan::DescriptorWriteBatch writes;
        writes
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
                 device_, vulkan::BufferConfig{
                              diagnostic_ ? std::uint64_t(extent.width) * extent.height * 64 : 64,
                              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host_memory})});
        if (fault)
            throw std::runtime_error("owned RT creation fault: target");
    }
    const std::array<render::ShaderStageFile, 2> shaders{
        render::vertex_shader_file(shader("forward_pbr_post.vert.spv")),
        render::fragment_shader_file(shader("forward_pbr_post.frag.spv"))};
    post_.create_pipeline(device_, {extent, format, shaders});
    std::uint64_t next_bytes = 0;
    for (const auto& target : next)
        next_bytes += std::uint64_t(extent.width) * extent.height * 8 + target.queries->size();
    requested_peak_ = std::max(requested_peak_, requested_bytes() + next_bytes);
    targets_ = std::move(next);
    extent_ = extent;
    output_format_ = format;
    ++target_generations_;
    for (std::uint32_t i = 0; i < slots_; ++i)
        vulkan::DescriptorWriteBatch{}
            .storage_image(descriptors_->set(i), 1, targets_[i].color->view())
            .storage_buffer(descriptors_->set(i), 6, targets_[i].queries->handle(),
                            targets_[i].queries->size())
            .update(device_);
}
void Renderer::collect(std::uint32_t slot) {
    if (!recorded_[slot])
        return;
    for (auto* timer : {&*trace_timer_, &*whole_timer_}) {
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
    auto camera = camera_uniform(c, extent_);
    camera.options.z = diagnostic_ ? 1.F : 0.F;
    uniforms_.at(slot.index)->upload(&camera, sizeof(camera));
    post_.upload(slot, render::hdr_post_uniforms(output.format, c.exposure,
                                                 c.view == "final" ? render::PbrTonemap::Aces
                                                                   : render::PbrTonemap::Linear));
    vulkan::CommandRecorder recorder(command);
    if (present)
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
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
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, pipeline_);
    recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, layout_, 0,
                                 descriptors_->set(slot.index));
    rt_.trace(command, &raygen_, &miss_, &hit_, &callable_, extent_.width, extent_.height, 1);
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
    return bytes;
}
nlohmann::json Renderer::metrics() const {
    const auto memory = device_.device_memory_budget();
    return {
        {"format", "cubey.raytracing.metrics"},
        {"version", 1},
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
                                 "driver allocation padding and pipeline/descriptor internals"},
        {"target_bytes", std::uint64_t(extent_.width) * extent_.height * 8 * slots_},
        {"observed_device_local_usage_bytes",
         memory.available ? nlohmann::json(memory.device_local_usage) : nlohmann::json(nullptr)},
        {"memory_usage_scope", "VK_EXT_memory_budget estimate of current-process device-local heap "
                               "allocations at shutdown; not a peak"},
        {"query_bytes",
         diagnostic_ ? std::uint64_t(extent_.width) * extent_.height * 64 * slots_ : 64 * slots_},
        {"uniform_bytes", sizeof(CameraUniform) * slots_},
        {"target_generations", target_generations_},
        {"as_builds", accelerations_.size()},
        {"dropped_gpu_samples", dropped_samples_},
        {"gpu_samples", timing_}};
}
void Renderer::destroy() noexcept {
    executor_.clear();
    targets_.clear();
    post_.destroy();
    trace_timer_.reset();
    whole_timer_.reset();
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
