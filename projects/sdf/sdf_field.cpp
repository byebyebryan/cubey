#include "sdf_field.h"

#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>
#include <cubey/vulkan/immediate_commands.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
constexpr std::uint32_t node_count = 19173961;
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

Field::Field(vulkan::Device& device, ProjectGpuServices& gpu, std::uint32_t axis,
             std::uint32_t batch_size, std::uint32_t frontier_limit, std::uint32_t reference_limit)
    : atlas_axis(axis), capacity(frontier_limit ? frontier_limit : axis * axis * axis),
      device_(device), gpu_(gpu), batch_size_(batch_size), reference_capacity_(reference_limit) {
    if (batch_size < 1 || batch_size > 128)
        throw std::invalid_argument("SDF batch size must be 1..128");
    if (axis != 64 && axis != 80 && axis != 92)
        throw std::invalid_argument("SDF atlas axis must be 64, 80 or 92 bricks");
    if (capacity > axis * axis * axis || reference_limit < 128 ||
        reference_limit > 16U * 1024U * 1024U)
        throw std::invalid_argument("invalid SDF frontier/reference validation limits");
    const std::uint64_t allocation = 16ULL * node_count + 8ULL * capacity +
                                     4ULL * reference_capacity_ + 128 * sizeof(GpuEdit) + 16 +
                                     2048ULL * axis * axis * axis + 16 +
                                     authored_max_instructions * sizeof(SceneInstruction);
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
        stats_.allocation_bytes += 1024ULL * axis * axis * axis;
    }
    edit_headers_ = buffer(node_count * 8ULL);
    references_ = buffer(reference_capacity_ * 4ULL);
    edits_ = buffer(128 * sizeof(GpuEdit), true);
    counters_ = buffer(16, true);
    authored_program_ = buffer(16 + authored_max_instructions * sizeof(SceneInstruction), true);
    if (stats_.allocation_bytes > 2ULL * 1024 * 1024 * 1024)
        throw std::runtime_error("SDF field exceeds 2 GiB allocation budget");
    std::vector<vulkan::DescriptorSetBindingConfig> bindings;
    for (std::uint32_t i = 0; i < 8; ++i)
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    descriptors_ =
        std::make_unique<vulkan::DescriptorSetBundle>(device, vulkan::DescriptorSetInfo(bindings));
    profiler_.emplace(device, 1, 1);
    const std::array<VkPushConstantRange, 1> push{
        {{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Parameters)}}};
    for (auto [pipeline, name] : {std::pair{&filter_pipeline_, "filter.comp.spv"},
                                  std::pair{&brick_pipeline_, "bricks.comp.spv"}})
        render::emplace_single_set_compute_pipeline_resource(
            *pipeline, device,
            render::compute_shader_file(std::filesystem::path(CUBEY_SDF_SHADER_DIR) / name),
            descriptors_->layout(), push);
    for (auto [pipeline, name] : {std::pair{&authored_filter_, "authored_filter.comp.spv"},
                                  std::pair{&authored_bricks_, "authored_bricks.comp.spv"}})
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
    writes.storage_buffer(set, 10, authored_program_->handle(), authored_program_->size());
    writes.update(device_);
}

void Field::dispatch(bool bricks, std::uint32_t level, std::uint32_t count, std::uint32_t input,
                     std::uint32_t output) {
    update_descriptors(input, output);
    const auto& pipeline = authored_dispatch_ ? (bricks ? *authored_bricks_ : *authored_filter_)
                                              : (bricks ? *brick_pipeline_ : *filter_pipeline_);
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
             profiler_->begin_frame(command, 0);
             profiler_->begin_pass(command, 0, bricks ? "sdf.bricks" : "sdf.filter");
             recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
             recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0,
                                          descriptors_->set());
             recorder.push_constants(
                 pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                 Parameters{level, count, capacity, reference_capacity_, atlas_axis});
             recorder.dispatch(std::min(count, 1024U), (count + 1023U) / 1024U, 1);
             profiler_->end_pass(command, 0);
             barrier(command);
             commands.submit_and_wait();
         }}));
    profiler_->collect(0);
    for (const auto& timing : profiler_->latest_timings())
        (bricks ? stats_.brick_gpu_ms : stats_.filter_gpu_ms) += timing.milliseconds;
}

void Field::build(std::span<const BoxEdit> edits) {
    authored_dispatch_ = false;
    if (authored_committed_) {
        authored_hash_.reset();
        authored_committed_ = false;
        has_last_requested_ = false;
        // Force the legacy path to reset the authored field, even for an empty recipe.
        committed_edits_.assign(1, GpuEdit{});
    }
    const auto start = std::chrono::steady_clock::now();
    const auto packed = gpu_edits(edits);
    stats_.batch_count = 0;
    stats_.reference_count = 0;
    stats_.filter_gpu_ms = 0;
    stats_.brick_gpu_ms = 0;
    stats_.other_wall_ms = 0;
    stats_.copied_bricks = 0;
    stats_.updated_bricks = 0;
    stats_.build_ms = 0;
    const auto matches = [](const std::vector<GpuEdit>& a, std::span<const GpuEdit> b) {
        return a.size() <= b.size() &&
               (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(GpuEdit)) == 0);
    };
    if (has_last_requested_ && last_requested_.size() == packed.size() &&
        matches(last_requested_, packed))
        return;
    has_last_requested_ = false;
    last_requested_.clear();
    // Keep a completed base at a full batch boundary. A partial tail is a
    // displayed working field, never a new base for applying that tail again.
    if (!matches(committed_edits_, packed)) {
        static_cast<void>(gpu_.submit_and_wait(
            {.label = "sdf reset recipe", .work = [&](vulkan::GpuOwnerContext& owner) {
                 vulkan::ImmediateCommands commands(owner);
                 for (auto& nodes : nodes_)
                     vkCmdFillBuffer(commands.command_buffer(), nodes->handle(), 0, VK_WHOLE_SIZE,
                                     0);
                 barrier(commands.command_buffer());
                 commands.submit_and_wait();
             }}));
        committed_ = 0;
        committed_edits_.clear();
        base_brick_count_ = 0;
    }
    displayed_ = committed_;
    displayed_edit_count_ = committed_edits_.size();
    stats_.brick_count = base_brick_count_;
    for (std::size_t begin = committed_edits_.size(); begin < packed.size(); begin += batch_size_) {
        const auto count =
            static_cast<std::uint32_t>(std::min<std::size_t>(batch_size_, packed.size() - begin));
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
                throw CapacityExceeded(
                    "SDF field capacity exceeded: frontier=" + std::to_string(counters[0]) +
                    ", references=" + std::to_string(counters[1]) + ", capacity=" +
                    std::to_string(capacity) + ", atlas_axis=" + std::to_string(atlas_axis) +
                    ", flags=" + std::to_string(counters[2]));
            frontier_count = counters[0];
            stats_.reference_count = std::max(stats_.reference_count, counters[1]);
            input = 1U - input;
        }
        if (frontier_count > 0)
            dispatch(true, depth, frontier_count, input, 1U - input);
        std::array<std::uint32_t, 4> counters{};
        counters_->download(counters.data(), 16);
        stats_.updated_bricks += counters[3];
        stats_.copied_bricks += frontier_count - counters[3];
        // Publication follows GPU completion. Only a full batch replaces the
        // reusable base; the next prefix reconstructs its partial tail once.
        displayed_ = 1U - committed_;
        displayed_edit_count_ = begin + count;
        if (count == batch_size_) {
            committed_ = displayed_;
            committed_edits_.assign(packed.begin(),
                                    packed.begin() + static_cast<std::ptrdiff_t>(begin + count));
            base_brick_count_ = frontier_count;
        }
        stats_.brick_count = frontier_count;
        ++stats_.batch_count;
    }
    stats_.build_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    last_requested_ = packed;
    has_last_requested_ = true;
    ++stats_.build_count;
    stats_.other_wall_ms =
        std::max(0.0, stats_.build_ms - stats_.filter_gpu_ms - stats_.brick_gpu_ms);
    std::cout << "sdf: edits=" << edits.size() << " batches=" << stats_.batch_count
              << " bricks=" << stats_.brick_count << "/" << capacity
              << " references=" << stats_.reference_count
              << " allocated=" << stats_.allocation_bytes << " build_ms=" << stats_.build_ms
              << " filter_gpu_ms=" << stats_.filter_gpu_ms
              << " brick_gpu_ms=" << stats_.brick_gpu_ms
              << " other_wall_ms=" << stats_.other_wall_ms << " copied=" << stats_.copied_bricks
              << " updated=" << stats_.updated_bricks << '\n';
}
void Field::build(const AuthoredScene& scene) {
    const auto hash = authored_geometry_hash(scene);
    if (authored_hash_ && *authored_hash_ == hash)
        return;
    const auto start = std::chrono::steady_clock::now();
    const auto previous = stats_;
    // Keep the actually displayed field intact until the whole new scene is
    // complete, including when the previous view was a legacy partial batch.
    committed_ = displayed_;
    authored_committed_ = true;
    authored_dispatch_ = true;
    stats_ = {};
    stats_.allocation_bytes = previous.allocation_bytes;
    stats_.build_count = previous.build_count;
    const auto bytes = authored_program_bytes(&scene);
    try {
        static_cast<void>(gpu_.submit_and_wait(
            {.label = "sdf begin authored scene", .work = [&](vulkan::GpuOwnerContext& owner) {
                 authored_program_->upload(bytes.data(), bytes.size());
                 vulkan::ImmediateCommands commands(owner);
                 const auto command = commands.command_buffer();
                 vkCmdFillBuffer(command, nodes_[1U - committed_]->handle(), 0, VK_WHOLE_SIZE, 0);
                 vkCmdFillBuffer(command, frontiers_[0]->handle(), 0, 4, 0);
                 vkCmdFillBuffer(command, counters_->handle(), 0, 16, 0);
                 barrier(command);
                 commands.submit_and_wait();
             }}));
        std::uint32_t count = 1, input = 0;
        for (std::uint32_t level = 2; level <= depth && count > 0; level += 2) {
            dispatch(false, level, count, input, 1U - input);
            std::array<std::uint32_t, 4> counters{};
            counters_->download(counters.data(), 16);
            if (counters[2] || counters[0] > capacity)
                throw CapacityExceeded("SDF authored field capacity exceeded: frontier=" +
                                       std::to_string(counters[0]) +
                                       ", capacity=" + std::to_string(capacity));
            count = counters[0];
            input = 1U - input;
        }
        if (count > 0)
            dispatch(true, depth, count, input, 1U - input);
        committed_ = 1U - committed_;
        displayed_ = committed_;
        displayed_edit_count_ = scene.program.size();
        committed_edits_.clear();
        last_requested_.clear();
        has_last_requested_ = false;
        base_brick_count_ = count;
        authored_hash_ = hash;
        stats_.brick_count = count;
        stats_.updated_bricks = count;
        stats_.batch_count = 1;
        ++stats_.build_count;
        stats_.build_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        stats_.other_wall_ms =
            std::max(0.0, stats_.build_ms - stats_.filter_gpu_ms - stats_.brick_gpu_ms);
        std::cout << "sdf: authored instructions=" << scene.program.size()
                  << " leaves=" << scene.leaf_count << " bricks=" << count << "/" << capacity
                  << " allocated=" << stats_.allocation_bytes << " build_ms=" << stats_.build_ms
                  << " filter_gpu_ms=" << stats_.filter_gpu_ms
                  << " brick_gpu_ms=" << stats_.brick_gpu_ms << " builds=" << stats_.build_count
                  << '\n';
    } catch (...) {
        stats_ = previous;
        throw;
    }
}
} // namespace cubey::projects::sdf
