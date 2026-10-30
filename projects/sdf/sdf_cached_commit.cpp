#include "sdf_cached_commit.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <filesystem>
#include <functional>

namespace cubey::projects::sdf {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
constexpr std::uint32_t subdivided = 0x20000000U, filled = 0x10000000U, pointer = 0x00ffffffU;
constexpr std::array<std::uint32_t, 9> level_offset{0, 1, 9, 73, 585, 4681, 37449, 299593, 2396745};
void barrier(VkCommandBuffer command) {
    const VkMemoryBarrier b{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .pNext = nullptr,
                            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                            .dstAccessMask =
                                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &b, 0, nullptr, 0, nullptr);
}
} // namespace

CachedCommitStudy::CachedCommitStudy(vulkan::Device& device, ProjectGpuServices& gpu,
                                     std::uint32_t fallback_limit)
    : device_(device), gpu_(gpu), fallback_limit_(fallback_limit) {
    if (fallback_limit < 1 || fallback_limit > 2U * 1024U * 1024U)
        throw std::invalid_argument("invalid cached commit fallback capacity");
    std::vector<vulkan::DescriptorSetBindingConfig> bindings;
    for (unsigned i : {0U, 1U, 5U, 6U, 7U, 10U, 11U, 16U, 17U, 18U, 20U, 21U})
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT});
    bindings.push_back({9, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT});
    descriptors_ =
        std::make_unique<vulkan::DescriptorSetBundle>(device, vulkan::DescriptorSetInfo(bindings));
    auto host = [&](std::size_t bytes) {
        return std::make_unique<vulkan::Buffer>(
            device,
            vulkan::BufferConfig{.size = bytes,
                                 .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                          VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                 .memory_properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT});
    };
    controls_ = host(sizeof(CachedEditControls));
    counts_ = host(32);
    empty_cells_ = host(16);
    empty_refs_ = host(4);
    const std::array<std::uint32_t, 4> zero{};
    empty_cells_->upload(zero.data(), 16);
    empty_refs_->upload(zero.data(), 4);
    fallbacks_ = std::make_unique<vulkan::Buffer>(
        device, vulkan::device_local_buffer_config(fallback_limit_ * 4ULL,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    new_bricks_ = std::make_unique<vulkan::Buffer>(
        device, vulkan::device_local_buffer_config(64ULL * 64 * 64 * 4,
                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT));
    profiler_.emplace(device, 1, 1);
}

double CachedCommitStudy::prepare(CommitBuilder mode) {
    const auto start = Clock::now();
    const std::array<VkPushConstantRange, 1> push{{{VK_SHADER_STAGE_COMPUTE_BIT, 0, 20}}};
    auto create = [&](auto& resource, const char* shader) {
        if (!resource)
            render::emplace_single_set_compute_pipeline_resource(
                resource, device_,
                render::compute_shader_file(std::filesystem::path(CUBEY_SDF_SHADER_DIR) / shader),
                descriptors_->layout(), push);
    };
    if (mode == CommitBuilder::Reuse) {
        create(reuse_, "commit_reuse_bricks.comp.spv");
        create(fallback_, "commit_canonical_fallback.comp.spv");
    }
    if (mode == CommitBuilder::Pruned) {
        create(pruned_filter_, "commit_pruned_filter.comp.spv");
        create(pruned_bricks_, "commit_pruned_bricks.comp.spv");
    }
    return elapsed(start);
}

CommitBuildStats CachedCommitStudy::build(Field& dst, const Field& base, const CachedEditProgram& p,
                                          CommitBuilder mode, std::uint32_t axis) {
    const auto start = Clock::now();
    if (&dst == &base || dst.atlas_axis != 64 || base.atlas_axis != 64 || !base.authored_hash_ ||
        *base.authored_hash_ != authored_geometry_hash(p.base))
        throw std::invalid_argument("cached commit requires distinct matching canonical fields");
    // Recheck admission at the private boundary, before changing destination.
    const auto admitted = prepare_cached_edit(p.base, p.edited);
    if (std::memcmp(&admitted.controls, &p.controls, sizeof(p.controls)) != 0)
        throw std::invalid_argument("cached commit controls do not match source");
    CommitBuildStats result;
    result.admission_ms = elapsed(start);
    result.pipeline_prepare_ms = prepare(mode);
    if (mode == CommitBuilder::Full) {
        const auto old_hash = dst.authored_hash_;
        dst.authored_hash_.reset(); // The benchmark requests a real control build each time.
        try {
            dst.build(p.edited);
        } catch (...) {
            dst.authored_hash_ = old_hash;
            throw;
        }
        result.work[1] = dst.stats_.brick_count;
        result.work[4] = dst.stats_.brick_count * 512U;
        result.wall_ms = elapsed(start);
        return result;
    }
    if (mode == CommitBuilder::Pruned && axis != 4 && axis != 16)
        throw std::invalid_argument("commit pruning is bounded to exact A4/A16");
    const auto old_stats = dst.stats_;
    const auto old_committed = dst.committed_;
    const auto old_authored_committed = dst.authored_committed_;
    const auto old_authored_dispatch = dst.authored_dispatch_;
    std::unique_ptr<GpuRegionPrograms> regions;
    if (mode == CommitBuilder::Pruned) {
        const auto t = Clock::now();
        regions = std::make_unique<GpuRegionPrograms>(
            device_, p.edited, axis, region_reference_limit, false, nullptr, "off");
        result.region_prepare_ms = elapsed(t);
        result.region_bytes = regions->allocation_bytes;
        result.region_scratch_bytes = regions->scratch_peak_bytes;
        result.region_references = regions->reference_count;
    }
    if (dst.stats_.allocation_bytes + base.stats_.allocation_bytes + result.region_bytes + 132U +
            fallbacks_->size() + new_bricks_->size() >
        2ULL * 1024 * 1024 * 1024)
        throw CapacityExceeded("cached commit geometry resource budget exceeded");
    try {
        dst.committed_ = dst.displayed_;
        dst.authored_committed_ = true;
        dst.authored_dispatch_ = true;
        dst.stats_ = {};
        dst.stats_.allocation_bytes = old_stats.allocation_bytes;
        dst.stats_.build_count = old_stats.build_count;
        const auto bytes = authored_program_bytes(&p.edited);
        static_cast<void>(gpu_.submit_and_wait(
            {.label = "cached commit initialize", .work = [&](vulkan::GpuOwnerContext& owner) {
                 dst.authored_program_->upload(bytes.data(), bytes.size());
                 controls_->upload(&p.controls, sizeof(p.controls));
                 const std::array<std::uint32_t, 8> zero{};
                 counts_->upload(zero.data(), 32);
                 vulkan::ImmediateCommands commands(owner);
                 auto c = commands.command_buffer();
                 vkCmdFillBuffer(c, dst.nodes_[1U - dst.committed_]->handle(), 0, VK_WHOLE_SIZE, 0);
                 vkCmdFillBuffer(c, dst.frontiers_[0]->handle(), 0, 4, 0);
                 vkCmdFillBuffer(c, dst.counters_->handle(), 0, 16, 0);
                 barrier(c);
                 commands.submit_and_wait();
             }}));
        auto dispatch = [&](bool bricks, unsigned level, unsigned count, unsigned input) {
            vulkan::DescriptorWriteBatch writes;
            const auto set = descriptors_->set();
            const std::array<std::pair<unsigned, const vulkan::Buffer*>, 12> buffers{
                {{0, &base.nodes()},
                 {1, dst.nodes_[1U - dst.committed_].get()},
                 {5, dst.frontiers_[input].get()},
                 {6, dst.frontiers_[1U - input].get()},
                 {7, dst.counters_.get()},
                 {10, dst.authored_program_.get()},
                 {11, controls_.get()},
                 {16, regions ? regions->cells.get() : empty_cells_.get()},
                 {17, regions ? regions->references.get() : empty_refs_.get()},
                 {18, counts_.get()},
                 {20, fallbacks_.get()},
                 {21, new_bricks_.get()}}};
            for (const auto& [binding, b] : buffers)
                writes.storage_buffer(set, binding, b->handle(), b->size());
            writes.combined_image_sampler(set, 8, base.atlas().sampler().handle(),
                                          base.atlas().view(), VK_IMAGE_LAYOUT_GENERAL);
            writes.storage_image(set, 9, dst.atlases_[1U - dst.committed_]->view());
            writes.update(device_);
            const auto& pipeline = mode == CommitBuilder::Pruned
                                       ? (bricks ? *pruned_bricks_ : *pruned_filter_)
                                       : *reuse_;
            static_cast<void>(gpu_.submit_and_wait(
                {.label = bricks ? "cached commit samples" : "cached commit pruned classify",
                 .work = [&](vulkan::GpuOwnerContext& owner) {
                     vulkan::ImmediateCommands commands(owner);
                     auto c = commands.command_buffer();
                     barrier(c);
                     if (!bricks)
                         vkCmdFillBuffer(c, dst.counters_->handle(), 0, 4, 0);
                     barrier(c);
                     profiler_->begin_frame(c, 0);
                     profiler_->begin_pass(c, 0, bricks ? "commit.bricks" : "commit.filter");
                     vulkan::CommandRecorder recorder(c);
                     recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.pipeline());
                     recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(),
                                                  0, set);
                     recorder.push_constants(
                         pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         std::array<std::uint32_t, 5>{level, count, dst.capacity, fallback_limit_,
                                                      dst.atlas_axis});
                     recorder.dispatch(std::min(count, 1024U), (count + 1023U) / 1024U, 1);
                     profiler_->end_pass(c, 0);
                     barrier(c);
                     commands.submit_and_wait();
                 }}));
            profiler_->collect(0);
            for (const auto& t : profiler_->latest_timings())
                (bricks ? dst.stats_.brick_gpu_ms : dst.stats_.filter_gpu_ms) += t.milliseconds;
        };
        unsigned count = 1, input = 0;
        for (unsigned level = 2; level <= Field::depth && count; level += 2) {
            if (mode == CommitBuilder::Pruned)
                dispatch(false, level, count, input);
            else
                dst.dispatch(false, level, count, input, 1U - input);
            std::array<std::uint32_t, 4> counters{};
            dst.counters_->download(counters.data(), 16);
            if (counters[2] || counters[0] > dst.capacity)
                throw CapacityExceeded("cached commit frontier overflow");
            count = counters[0];
            input = 1U - input;
        }
        if (count)
            dispatch(true, Field::depth, count, input);
        counts_->download(result.work.data(), 32);
        if (mode == CommitBuilder::Reuse) {
            result.reuse_gpu_ms = dst.stats_.brick_gpu_ms;
            if (result.work[7]) {
                const auto discarded = elapsed(start);
                dst.stats_ = old_stats;
                dst.committed_ = old_committed;
                dst.authored_committed_ = old_authored_committed;
                dst.authored_dispatch_ = old_authored_dispatch;
                auto full = build(dst, base, p, CommitBuilder::Full, axis);
                full.full_fallback = true;
                full.work = result.work;
                full.reuse_gpu_ms = result.reuse_gpu_ms;
                full.wall_ms = discarded + full.wall_ms;
                dst.stats_.build_ms = full.wall_ms;
                return full;
            }
            result.work[2] = result.work[0] * 512U - result.work[3];
            result.work[4] = result.work[1] * 512U;
            const auto total = result.work[3] + result.work[4];
            if (total) {
                const auto groups = (total + 63U) / 64U;
                static_cast<void>(gpu_.submit_and_wait(
                    {.label = "cached commit compact canonical samples",
                     .work = [&](vulkan::GpuOwnerContext& owner) {
                         vulkan::ImmediateCommands commands(owner);
                         auto c = commands.command_buffer();
                         barrier(c);
                         profiler_->begin_frame(c, 0);
                         profiler_->begin_pass(c, 0, "commit.fallback");
                         vulkan::CommandRecorder recorder(c);
                         recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_COMPUTE,
                                                fallback_->pipeline());
                         recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                                      fallback_->layout(), 0, descriptors_->set());
                         recorder.push_constants(
                             fallback_->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                             std::array<std::uint32_t, 5>{Field::depth, total, result.work[4],
                                                          fallback_limit_, dst.atlas_axis});
                         recorder.dispatch(std::min(groups, 65535U), (groups + 65534U) / 65535U, 1);
                         profiler_->end_pass(c, 0);
                         barrier(c);
                         commands.submit_and_wait();
                     }}));
                profiler_->collect(0);
                for (const auto& t : profiler_->latest_timings())
                    result.fallback_gpu_ms += t.milliseconds;
                dst.stats_.brick_gpu_ms += result.fallback_gpu_ms;
            }
        }
        dst.committed_ = 1U - dst.committed_;
        dst.displayed_ = dst.committed_;
        dst.displayed_edit_count_ = p.edited.program.size();
        dst.committed_edits_.clear();
        dst.last_requested_.clear();
        dst.has_last_requested_ = false;
        dst.base_brick_count_ = count;
        dst.authored_hash_ = authored_geometry_hash(p.edited);
        dst.stats_.brick_count = count;
        dst.stats_.updated_bricks = count;
        dst.stats_.batch_count = 1;
        ++dst.stats_.build_count;
        dst.stats_.build_ms = elapsed(start);
        dst.stats_.other_wall_ms =
            std::max(0., dst.stats_.build_ms - dst.stats_.filter_gpu_ms - dst.stats_.brick_gpu_ms);
        result.wall_ms = elapsed(start);
        return result;
    } catch (...) {
        dst.stats_ = old_stats;
        dst.committed_ = old_committed;
        dst.authored_committed_ = old_authored_committed;
        dst.authored_dispatch_ = old_authored_dispatch;
        throw;
    }
}

CommitSnapshot CachedCommitStudy::snapshot(vulkan::Device& device, ProjectGpuServices& gpu,
                                           const Field& field) {
    // Raw image copies preserve the stored R16 bits, including signed zero and
    // subnormals. No sampler decode or packHalf conversion is used for this oracle.
    CommitSnapshot result;
    std::vector<std::uint32_t> headers;
    {
        const auto raw = gpu.readback_buffer(field.nodes().handle(), field.nodes().size(),
                                             "commit topology snapshot");
        headers.resize(raw.size() / 4);
        std::memcpy(headers.data(), raw.data(), raw.size());
    }
    std::vector<std::uint32_t> pointers;
    const auto visit = [&](auto&& self, unsigned level, unsigned code) -> void {
        const auto h = headers.at(level_offset[level] + code);
        result.topology.push_back({(level << 24) | code, h & (subdivided | filled)});
        if ((h & subdivided) == 0)
            return;
        if (level == 8) {
            result.cells.push_back(code);
            pointers.push_back(h & pointer);
            return;
        }
        for (unsigned child = 0; child < 64; ++child)
            self(self, level + 2, (code << 6) | child);
    };
    for (unsigned code = 0; code < 64; ++code)
        visit(visit, 2, code);
    if (pointers.size() != field.stats().brick_count)
        throw std::runtime_error("snapshot leaf count mismatch");
    headers.clear();
    headers.shrink_to_fit();
    result.samples.resize(pointers.size() * 512ULL);
    const unsigned side = field.atlas_axis * 8U, slices = 32;
    vulkan::Buffer staging(device, vulkan::readback_buffer_config(static_cast<std::uint64_t>(side) *
                                                                  side * slices * 2));
    std::vector<std::uint16_t> chunk(static_cast<std::size_t>(side) * side * slices);
    for (unsigned z = 0; z < side; z += slices) {
        static_cast<void>(gpu.submit_and_wait(
            {.label = "raw R16 snapshot chunk", .work = [&](vulkan::GpuOwnerContext& owner) {
                 vulkan::ImmediateCommands commands(owner);
                 auto c = commands.command_buffer();
                 barrier(c);
                 VkImageMemoryBarrier b{
                     .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                     .pNext = nullptr,
                     .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                     .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
                     .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
                     .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                     .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                     .image = field.atlas().handle(),
                     .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
                 vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                      &b);
                 const VkBufferImageCopy copy{
                     .bufferOffset = 0,
                     .bufferRowLength = 0,
                     .bufferImageHeight = 0,
                     .imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                     .imageOffset = {0, 0, static_cast<int>(z)},
                     .imageExtent = {side, side, slices}};
                 vkCmdCopyImageToBuffer(c, field.atlas().handle(),
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.handle(), 1,
                                        &copy);
                 b.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                 b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
                 b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                 b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                 vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr,
                                      1, &b);
                 commands.submit_and_wait();
             }}));
        staging.download(chunk.data(), chunk.size() * 2ULL);
        for (std::size_t i = 0; i < pointers.size(); ++i) {
            const auto ptr = pointers[i], bz = (ptr / (field.atlas_axis * field.atlas_axis)) * 8U;
            if (bz < z || bz >= z + slices)
                continue;
            const auto bx = (ptr % field.atlas_axis) * 8U,
                       by = ((ptr / field.atlas_axis) % field.atlas_axis) * 8U;
            for (unsigned dz = 0; dz < 8; ++dz)
                for (unsigned dy = 0; dy < 8; ++dy)
                    std::copy_n(chunk.data() + ((bz - z + dz) * side + by + dy) * side + bx, 8,
                                result.samples.data() + i * 512 + (dz * 8 + dy) * 8);
        }
    }
    return result;
}
} // namespace cubey::projects::sdf
