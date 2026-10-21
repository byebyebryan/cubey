#include "sdf_field.h"

#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
constexpr std::uint32_t node_count = 19173961;
constexpr std::uint32_t ref_capacity = 16U * 1024U * 1024U;
constexpr auto storage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
struct Parameters {
    std::uint32_t depth, count, capacity, ref_capacity, atlas_axis;
};
void barrier(VkCommandBuffer command) {
    const VkMemoryBarrier memory{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                                 .pNext = nullptr,
                                 .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                                 .dstAccessMask =
                                     VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &memory, 0, nullptr, 0, nullptr);
}
} // namespace

Field::Field(vulkan::Device& device, ProjectGpuServices& gpu, std::uint32_t axis)
    : atlas_axis(axis), capacity(axis * axis * axis), device_(device), gpu_(gpu) {
    if (axis != 64 && axis != 80 && axis != 92)
        throw std::invalid_argument("SDF atlas axis must be 64, 80 or 92 bricks");
    const std::uint64_t allocation = 16ULL * node_count + 8ULL * capacity + 4ULL * ref_capacity +
                                     128 * sizeof(GpuEdit) + 16 + 2048ULL * capacity;
    if (allocation > 2ULL * 1024 * 1024 * 1024)
        throw std::runtime_error("SDF field exceeds 2 GiB allocation budget");
    if (device.properties().limits.maxStorageBufferRange < node_count * 8ULL ||
        device.properties().limits.maxImageDimension3D < axis * 8 ||
        device.properties().limits.maxComputeWorkGroupInvocations < 512)
        throw std::runtime_error("SDF reference profile exceeds device resource limits");
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(device.physical_device(), VK_FORMAT_R16_SFLOAT,
                                        &properties);
    const auto required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if (!device.supports_shader_storage_image_extended_formats() ||
        (properties.optimalTilingFeatures & required) != required)
        throw std::runtime_error("SDF requires filterable R16F storage images");
    auto buffer = [&](VkDeviceSize bytes, bool host = false) {
        stats_.allocation_bytes += bytes;
        return std::make_unique<vulkan::Buffer>(
            device, vulkan::BufferConfig{.size = bytes,
                                         .usage = storage,
                                         .memory_properties = static_cast<VkMemoryPropertyFlags>(
                                             host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
                                                  : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)});
    };
    for (unsigned i = 0; i < 2; ++i) {
        nodes_[i] = buffer(node_count * 4ULL);
        frontiers_[i] = buffer(capacity * 4ULL);
        atlases_[i] = std::make_unique<render::Texture3D>(
            device, render::Texture3DConfig{
                        .extent = {atlas_axis * 8, atlas_axis * 8, atlas_axis * 8},
                        .format = VK_FORMAT_R16_SFLOAT,
                        .create_sampler = true,
                        .sampler = {.address_mode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE}});
        stats_.allocation_bytes += 1024ULL * capacity;
    }
    edit_headers_ = buffer(node_count * 8ULL);
    references_ = buffer(ref_capacity * 4ULL);
    edits_ = buffer(128 * sizeof(GpuEdit), true);
    counters_ = buffer(16, true);
    if (stats_.allocation_bytes > 2ULL * 1024 * 1024 * 1024)
        throw std::runtime_error("SDF field exceeds 2 GiB allocation budget");
    std::vector<vulkan::DescriptorSetBindingConfig> bindings;
    for (std::uint32_t i = 0; i < 8; ++i)
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT});
    descriptors_ =
        std::make_unique<vulkan::DescriptorSetBundle>(device, vulkan::DescriptorSetInfo(bindings));
    const std::array<VkPushConstantRange, 1> push{
        {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters)}}};
    for (auto [pipeline, name] : {std::pair{&filter_pipeline_, "filter.comp.spv"},
                                  std::pair{&brick_pipeline_, "bricks.comp.spv"}})
        render::emplace_single_set_compute_pipeline_resource(
            *pipeline, device,
            render::compute_shader_file(std::filesystem::path(CUBEY_SDF_SHADER_DIR) / name),
            descriptors_->layout(), push);
    static_cast<void>(gpu_.submit_and_wait(
        {.label = "sdf initialize fields", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             for (unsigned i = 0; i < 2; ++i) {
                 vkCmdFillBuffer(commands.command_buffer(), nodes_[i]->handle(), 0, VK_WHOLE_SIZE,
                                 0);
                 vulkan::transition_image_layout(
                     commands.command_buffer(),
                     vulkan::begin_storage_image_write_transition(atlases_[i]->handle()));
             }
             barrier(commands.command_buffer());
             commands.submit_and_wait();
         }}));
}

void Field::update_descriptors(std::uint32_t input, std::uint32_t output) {
    const auto working = 1U - committed_;
    const auto set = descriptors_->set();
    vulkan::DescriptorWriteBatch writes;
    const std::array<const vulkan::Buffer*, 8> buffers{
        nodes_[committed_].get(), nodes_[working].get(), edit_headers_.get(),
        references_.get(),        edits_.get(),          frontiers_[input].get(),
        frontiers_[output].get(), counters_.get()};
    for (std::uint32_t i = 0; i < buffers.size(); ++i)
        writes.storage_buffer(set, i, buffers[i]->handle(), buffers[i]->size());
    writes.combined_image_sampler(set, 8, atlases_[committed_]->sampler().handle(),
                                  atlases_[committed_]->view(), VK_IMAGE_LAYOUT_GENERAL);
    writes.storage_image(set, 9, atlases_[working]->view());
    writes.update(device_);
}

void Field::dispatch(bool bricks, std::uint32_t level, std::uint32_t count, std::uint32_t input,
                     std::uint32_t output) {
    update_descriptors(input, output);
    const auto& pipeline = bricks ? *brick_pipeline_ : *filter_pipeline_;
    static_cast<void>(gpu_.submit_and_wait(
        {.label = bricks ? "sdf build bricks" : "sdf filter nodes",
         .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             const auto command = commands.command_buffer();
             barrier(command);
             if (!bricks)
                 vkCmdFillBuffer(command, counters_->handle(), 0, 4, 0);
             barrier(command);
             const vulkan::CommandRecorder recorder(command);
             recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
             recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                          descriptors_->set());
             recorder.push_constants(pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     Parameters{level, count, capacity, ref_capacity, atlas_axis});
             recorder.dispatch(std::min(count, 1024U), (count + 1023U) / 1024U, 1);
             barrier(command);
             commands.submit_and_wait();
         }}));
}

void Field::build(std::span<const BoxEdit> edits) {
    const auto start = std::chrono::steady_clock::now();
    const auto packed = gpu_edits(edits);
    stats_.batch_count = 0;
    stats_.reference_count = 0;
    // Rebuild from the retained recipe, never from a partially serialized batch.
    static_cast<void>(gpu_.submit_and_wait(
        {.label = "sdf reset recipe", .work = [&](vulkan::GpuOwnerContext& owner) {
             vulkan::ImmediateCommands commands(owner);
             for (auto& nodes : nodes_)
                 vkCmdFillBuffer(commands.command_buffer(), nodes->handle(), 0, VK_WHOLE_SIZE, 0);
             barrier(commands.command_buffer());
             commands.submit_and_wait();
         }}));
    stats_.brick_count = 0;
    for (std::size_t begin = 0; begin < packed.size(); begin += 128) {
        const auto count =
            static_cast<std::uint32_t>(std::min<std::size_t>(128, packed.size() - begin));
        static_cast<void>(gpu_.submit_and_wait(
            {.label = "sdf begin edit batch", .work = [&](vulkan::GpuOwnerContext& owner) {
                 edits_->upload(packed.data() + begin, count * sizeof(GpuEdit));
                 vulkan::ImmediateCommands commands(owner);
                 const auto command = commands.command_buffer();
                 const std::uint32_t root = (begin == 0 ? 0x40000000U : 0x20000000U) | 0x80000000U;
                 const std::array<std::uint32_t, 2> header{0, count};
                 std::array<std::uint32_t, 128> refs{};
                 for (std::uint32_t i = 0; i < count; ++i)
                     refs[i] = i;
                 const std::array<std::uint32_t, 4> counters{0, count, 0, 0};
                 vkCmdUpdateBuffer(command, nodes_[1U - committed_]->handle(), 0, 4, &root);
                 vkCmdUpdateBuffer(command, edit_headers_->handle(), 0, 8, header.data());
                 vkCmdUpdateBuffer(command, references_->handle(), 0, count * 4, refs.data());
                 vkCmdUpdateBuffer(command, counters_->handle(), 0, 16, counters.data());
                 vkCmdFillBuffer(command, frontiers_[0]->handle(), 0, 4, 0);
                 barrier(command);
                 commands.submit_and_wait();
             }}));
        std::uint32_t frontier_count = 1, input = 0;
        for (std::uint32_t level = 2; level <= depth && frontier_count > 0; level += 2) {
            dispatch(false, level, frontier_count, input, 1U - input);
            std::array<std::uint32_t, 4> counters{};
            counters_->download(counters.data(), 16);
            if (counters[2] != 0 || counters[0] > capacity)
                throw std::runtime_error(
                    "SDF field capacity exceeded: frontier=" + std::to_string(counters[0]) +
                    ", references=" + std::to_string(counters[1]) +
                    ", flags=" + std::to_string(counters[2]));
            frontier_count = counters[0];
            stats_.reference_count = std::max(stats_.reference_count, counters[1]);
            input = 1U - input;
        }
        if (frontier_count > 0)
            dispatch(true, depth, frontier_count, input, 1U - input);
        // Promote only after all dispatches and their fences have completed.
        committed_ = 1U - committed_;
        stats_.brick_count = frontier_count;
        ++stats_.batch_count;
    }
    stats_.build_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "sdf: edits=" << edits.size() << " batches=" << stats_.batch_count
              << " bricks=" << stats_.brick_count << "/" << capacity
              << " references=" << stats_.reference_count
              << " allocated=" << stats_.allocation_bytes << " build_ms=" << stats_.build_ms
              << '\n';
}
} // namespace cubey::projects::sdf
