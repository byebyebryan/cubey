#include "sdf_cached_commit.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/immediate_commands.h>
#include <cubey/vulkan/vk_check.h>
#include <limits>

namespace cubey::projects::sdf {
namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::time_point t) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t).count();
}
std::uint64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
        .count();
}
void local_barrier(VkCommandBuffer c) {
    const VkMemoryBarrier b{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                            .pNext = nullptr,
                            .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
                            .dstAccessMask =
                                VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT};
    vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         0, 1, &b, 0, nullptr, 0, nullptr);
}
struct alignas(16) LocalControls {
    std::array<std::uint32_t, 4> header{};
    std::array<float, 4> info{};
    std::array<SceneInstruction, 8> tools{};
};
static_assert(sizeof(LocalControls) == 544);
} // namespace

struct CachedCommitStudy::LocalCommands {
    VkDevice device;
    vulkan::CommandPool pool;
    VkCommandBuffer command;
    VkFence fence = VK_NULL_HANDLE;
    explicit LocalCommands(vulkan::GpuOwnerContext& owner)
        : device(owner.device().handle()),
          pool(owner.device(), {.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT}),
          command(pool.allocate_primary()) {
        const VkFenceCreateInfo info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vulkan::check(vkCreateFence(device, &info, nullptr, &fence), "create local unit fence");
    }
    ~LocalCommands() {
        if (fence)
            vkDestroyFence(device, fence, nullptr);
    }
};
CachedCommitStudy::~CachedCommitStudy() {
    // The runtime holds the last reference until its owner cleanup barrier.
    local_commands_.reset();
    local_commands_cleanup_.request();
}
double CachedCommitStudy::prepare_submission() {
    if (!slicing_.pooled || local_commands_)
        return 0;
    const auto start = Clock::now();
    static_cast<void>(gpu_.submit_and_wait(
        {.label = "prepare local command pool", .work = [&](auto& owner) {
             auto commands = std::make_shared<LocalCommands>(owner);
             local_commands_cleanup_ = owner.runtime().register_owner_cleanup(
                 "retire local command pool", [commands](auto&) mutable { commands.reset(); });
             local_commands_ = std::move(commands);
         }}));
    return milliseconds(start);
}
void CachedCommitStudy::submit_local(vulkan::GpuOwnerContext& owner,
                                     const std::function<void(VkCommandBuffer)>& record,
                                     std::uint64_t& submit_ns, std::uint64_t& complete_ns) {
    if (!slicing_.pooled) {
        vulkan::ImmediateCommands commands(owner);
        record(commands.command_buffer());
        submit_ns = now_ns();
        commands.submit_and_wait();
    } else {
        if (!local_commands_)
            throw std::logic_error("pooled commands were not prepared");
        const auto& r = *local_commands_;
        // Every prior unit completed its fence; no command buffer is pending.
        vulkan::check(vkResetCommandPool(r.device, r.pool.handle(), 0), "reset local command pool");
        vulkan::check(vkResetFences(r.device, 1, &r.fence), "reset local unit fence");
        vulkan::begin_command_buffer(r.command, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        record(r.command);
        vulkan::end_command_buffer(r.command, "end local unit");
        submit_ns = now_ns();
        const auto ticket = owner.submission().submit(
            {.command_buffers = {r.command}, .fence = r.fence}, "submit local pooled unit");
        vulkan::check(vkWaitForFences(r.device, 1, &r.fence, VK_TRUE, UINT64_MAX),
                      "wait local unit fence");
        owner.complete_submission(ticket);
    }
    complete_ns = now_ns();
}

CommitBuildStats CachedCommitStudy::build(Field& dst, const Field& base,
                                          const CachedStrokeProgram& program, CommitBuilder mode,
                                          std::uint32_t region_axis,
                                          StrokePreparation preparation) {
    static_cast<void>(region_axis);
    const auto start = Clock::now();
    if ((mode != CommitBuilder::Local && mode != CommitBuilder::LocalDominance) || &dst == &base ||
        dst.atlas_axis != 64 || base.atlas_axis != 64 || !base.authored_hash_ ||
        *base.authored_hash_ != authored_geometry_hash(program.base))
        throw std::invalid_argument("stroke commit requires distinct matching local fields");
    if (program.controls.header[3] > 1U)
        throw std::invalid_argument("unknown stroke admission contract");
    auto admitted = prepare_cached_stroke(program.base, program.edited,
                                          program.controls.header[3] == 1U, preparation);
    if (std::memcmp(&admitted.controls, &program.controls, sizeof(program.controls)) != 0)
        throw std::invalid_argument("stroke controls differ from source");
    const double admission = milliseconds(start), pipeline = prepare(mode);
    CachedEditProgram p{std::move(admitted.base), std::move(admitted.edited), {}};
    p.controls.header = program.controls.header;
    p.controls.primitive = program.controls.tools[0];
    auto result = build_local(dst, base, p.base, p.edited, p.controls,
                              std::span(program.controls.tools).first(program.controls.header[0]),
                              mode == CommitBuilder::LocalDominance);
    result.admission_ms = admission;
    result.palette_ms = admitted.palette_ms;
    result.validation_ms = admitted.validation_ms;
    result.serialization = admitted.serialization;
    result.palette_reparse_ms = admitted.palette_reparse_ms;
    result.pipeline_prepare_ms = pipeline;
    result.wall_ms = milliseconds(start);
    dst.stats_.build_ms = result.wall_ms;
    return result;
}

CommitBuildStats CachedCommitStudy::build(Field& dst, const Field& base,
                                          const PreparedCachedStroke& prepared,
                                          const StrokeVersion& current, CommitBuilder mode) {
    const auto start = Clock::now();
    prepared.require_version(current);
    const auto& program = prepared.program();
    if ((mode != CommitBuilder::Local && mode != CommitBuilder::LocalDominance) || &dst == &base ||
        dst.atlas_axis != 64 || base.atlas_axis != 64 || !base.authored_hash_ ||
        *base.authored_hash_ != authored_geometry_hash(program.base))
        throw std::invalid_argument("prepared stroke requires distinct matching local fields");
    CachedEditControls controls{};
    controls.header = program.controls.header;
    controls.primitive = program.controls.tools[0];
    const double admission = milliseconds(start), pipeline = prepare(mode);
    auto result = build_local(dst, base, program.base, program.edited, controls,
                              std::span(program.controls.tools).first(program.controls.header[0]),
                              mode == CommitBuilder::LocalDominance);
    result.admission_ms = admission;
    result.pipeline_prepare_ms = pipeline;
    result.wall_ms = milliseconds(start);
    dst.stats_.build_ms = result.wall_ms;
    return result;
}

CommitCopyStats CachedCommitStudy::clone(Field& dst, const Field& base, std::uint32_t slab_bytes,
                                         bool publish) {
    if (&dst == &base || dst.atlas_axis != base.atlas_axis || !base.authored_hash_)
        throw std::invalid_argument("clone requires distinct matching canonical field layouts");
    const auto started = Clock::now();
    CommitCopyStats result;
    const unsigned target = 1U - dst.displayed_, side = base.atlas_axis * 8;
    const std::uint64_t plane = side * side * 2ULL;
    if (slab_bytes && slab_bytes < plane)
        throw std::invalid_argument("clone slab smaller than image plane");
    const auto image_copy = [&](VkCommandBuffer c, unsigned z, unsigned depth) {
        const VkImageCopy region{.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                 .srcOffset = {0, 0, static_cast<int>(z)},
                                 .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                                 .dstOffset = {0, 0, static_cast<int>(z)},
                                 .extent = {side, side, depth}};
        vkCmdCopyImage(c, base.atlas().handle(), VK_IMAGE_LAYOUT_GENERAL,
                       dst.atlases_[target]->handle(), VK_IMAGE_LAYOUT_GENERAL, 1, &region);
    };
    const auto node_copy = [&](VkCommandBuffer c, VkDeviceSize offset, VkDeviceSize size) {
        const VkBufferCopy region{offset, offset, size};
        vkCmdCopyBuffer(c, base.nodes().handle(), dst.nodes_[target]->handle(), 1, &region);
    };
    const auto submit = [&](auto record) {
        if (slicing_.before)
            slicing_.before({"copy"});
        const auto unit_started = Clock::now();
        std::uint64_t submit_ns = 0, complete_ns = 0;
        static_cast<void>(gpu_.submit_and_wait(
            {.label = "local clone chunk", .work = [&](vulkan::GpuOwnerContext& owner) {
                 submit_local(
                     owner,
                     [&](auto c) {
                         local_barrier(c);
                         profiler_->begin_frame(c, 0);
                         profiler_->begin_pass(c, 0, "local.copy");
                         record(c);
                         profiler_->end_pass(c, 0);
                         local_barrier(c);
                     },
                     submit_ns, complete_ns);
             }}));
        profiler_->collect(0);
        double cost = 0;
        for (const auto& t : profiler_->latest_timings())
            cost += t.milliseconds;
        result.gpu_ms += cost;
        result.max_chunk_gpu_ms = std::max(result.max_chunk_gpu_ms, cost);
        ++result.chunks;
        if (slicing_.after)
            slicing_.after(
                {"copy", 0, 0, cost, milliseconds(unit_started), submit_ns, complete_ns});
    };
    if (!slab_bytes)
        submit([&](VkCommandBuffer c) {
            image_copy(c, 0, side);
            node_copy(c, 0, base.nodes().size());
        });
    else {
        const unsigned depth = static_cast<unsigned>(slab_bytes / plane);
        for (unsigned z = 0; z < side; z += depth)
            submit([&](VkCommandBuffer c) { image_copy(c, z, std::min(depth, side - z)); });
        for (VkDeviceSize offset = 0; offset < base.nodes().size(); offset += slab_bytes)
            submit([&](VkCommandBuffer c) {
                node_copy(c, offset,
                          std::min<VkDeviceSize>(slab_bytes, base.nodes().size() - offset));
            });
    }
    result.bytes = plane * side + base.nodes().size();
    result.wall_ms = milliseconds(started);
    if (publish) {
        dst.displayed_ = dst.committed_ = target;
        dst.authored_committed_ = true;
        dst.authored_dispatch_ = true;
        dst.authored_hash_ = base.authored_hash_;
        dst.base_brick_count_ = base.stats_.brick_count;
        const auto allocation = dst.stats_.allocation_bytes;
        dst.stats_ = base.stats_;
        dst.stats_.allocation_bytes = allocation;
        dst.displayed_edit_count_ = base.displayed_edit_count_;
        const auto it = local_fields_.find(&base);
        const auto high = it != local_fields_.end() && it->second.hash == *base.authored_hash_
                              ? it->second.high_water
                              : base.stats_.brick_count;
        local_fields_[&dst] = {*base.authored_hash_, high};
    }
    return result;
}

CommitBuildStats CachedCommitStudy::build_local(Field& dst, const Field& base,
                                                const AuthoredScene& base_scene,
                                                const AuthoredScene& edited_scene,
                                                const CachedEditControls& edit_controls,
                                                std::span<const SceneInstruction> tools,
                                                bool dominance) {
    const auto start = Clock::now();
    CommitBuildStats result;
    const auto target = 1U - dst.displayed_;
    LocalControls controls;
    controls.header = edit_controls.header;
    controls.tools[0] = edit_controls.primitive;
    if (!tools.empty())
        std::copy(tools.begin(), tools.end(), controls.tools.begin());
    // Real active-brick enclosure plus both evaluation budgets and a conservative
    // R16 encoding allowance (two relative ULPs). A separate brush budget protects
    // the lower-bound evaluation. Strict native topology/raw-bit qualification is
    // required; this is never substituted for arbitrary modifier semantics.
    const float evaluation =
        std::max(authored_evaluation_guard(base_scene), authored_evaluation_guard(edited_scene));
    const float r = .866025404F / 256.F;
    const float bound = 2 * r + 2e-6F + 2 * evaluation;
    controls.info[0] = std::nextafter(bound + std::max(bound / 512.F, 0x1p-23F),
                                      std::numeric_limits<float>::infinity());
    controls.info[1] = 8 * evaluation + 32 * std::numeric_limits<float>::epsilon();
    result.locality_bound = controls.info[0];
    result.locality_guard = controls.info[1];
    if (base.stats_.allocation_bytes + dst.stats_.allocation_bytes + working_bytes() >
        2ULL * 1024 * 1024 * 1024)
        throw CapacityExceeded("local commit exceeds geometry budget");
    const auto state = local_fields_.find(&base);
    std::array<std::uint32_t, 16> counts{};
    counts[0] = state != local_fields_.end() && state->second.hash == *base.authored_hash_
                    ? state->second.high_water
                    : base.stats_.brick_count;
    counts[1] = base.stats_.brick_count;
    result.copy = clone(dst, base, slicing_.copy_slab_bytes);
    const auto program = authored_program_bytes(&edited_scene);
    if (slicing_.before)
        slicing_.before({"initialize"});
    const auto init_started = Clock::now();
    std::uint64_t init_submit_ns = 0, init_complete_ns = 0;
    static_cast<void>(gpu_.submit_and_wait(
        {.label = "local initialize", .work = [&](vulkan::GpuOwnerContext& owner) {
             local_controls_->upload(&controls, sizeof(controls));
             local_counts_->upload(counts.data(), 64);
             dst.authored_program_->upload(program.data(), program.size());
             submit_local(
                 owner,
                 [&](auto c) {
                     profiler_->begin_frame(c, 0);
                     profiler_->begin_pass(c, 0, "local.initialize");
                     vkCmdFillBuffer(c, dst.frontiers_[0]->handle(), 0, 4, 0);
                     local_barrier(c);
                     profiler_->end_pass(c, 0);
                 },
                 init_submit_ns, init_complete_ns);
         }}));
    profiler_->collect(0);
    double init_gpu = 0;
    for (const auto& t : profiler_->latest_timings())
        init_gpu += t.milliseconds;
    if (slicing_.after)
        slicing_.after({"initialize", 0, 0, init_gpu, milliseconds(init_started), init_submit_ns,
                        init_complete_ns});
    const auto dispatch = [&](unsigned kind, unsigned level, unsigned count, unsigned input,
                              unsigned new_texels = 0) {
        vulkan::DescriptorWriteBatch writes;
        const auto set = descriptors_->set();
        for (const auto& [binding, b] : std::array<std::pair<unsigned, const vulkan::Buffer*>, 10>{
                 {{0, &base.nodes()},
                  {1, dst.nodes_[target].get()},
                  {5, dst.frontiers_[input].get()},
                  {6, dst.frontiers_[1U - input].get()},
                  {7, dst.counters_.get()},
                  {10, dst.authored_program_.get()},
                  {11, local_controls_.get()},
                  {18, local_counts_.get()},
                  {20, fallbacks_.get()},
                  {21, new_bricks_.get()}}})
            writes.storage_buffer(set, binding, b->handle(), b->size());
        writes.storage_buffer(set, 22, local_entries_->handle(), local_entries_->size());
        writes.combined_image_sampler(set, 8, base.atlas().sampler().handle(), base.atlas().view(),
                                      VK_IMAGE_LAYOUT_GENERAL);
        writes.storage_image(set, 9, dst.atlases_[target]->view());
        writes.update(device_);
        const auto& pipeline = kind == 0   ? (dominance ? *local_dom_filter_ : *local_filter_)
                               : kind == 1 ? (dominance ? *local_dom_samples_ : *local_samples_)
                                           : *local_canonical_;
        const unsigned groups = kind == 2 ? (count + 63) / 64 : count,
                       stride = kind == 2 ? 65535 : 1024;
        const unsigned pipeline_index = kind == 2 ? 2 : kind + (dominance ? 3 : 0);
        double cost = 0;
        for (unsigned offset = 0; offset < groups;) {
            const unsigned chunk =
                slicing_.groups ? std::min(slicing_.groups, groups - offset) : groups;
            const std::string kind_name = kind == 0   ? "classify"
                                          : kind == 1 ? "samples"
                                                      : "canonical";
            if (slicing_.before)
                slicing_.before({kind_name, level, chunk});
            const auto unit_started = Clock::now();
            std::uint64_t submit_ns = 0, complete_ns = 0;
            static_cast<void>(gpu_.submit_and_wait(
                {.label = "local dispatch", .work = [&](vulkan::GpuOwnerContext& owner) {
                     submit_local(
                         owner,
                         [&](auto c) {
                             local_barrier(c);
                             if (kind == 0 && offset == 0)
                                 vkCmdFillBuffer(c, dst.counters_->handle(), 0, 4, 0);
                             local_barrier(c);
                             profiler_->begin_frame(c, 0);
                             profiler_->begin_pass(c, 0, "local.work");
                             vulkan::CommandRecorder recorder(c);
                             recorder.bind_pipeline(
                                 VK_PIPELINE_BIND_POINT_COMPUTE,
                                 slicing_.groups ? sliced_pipelines_[pipeline_index]->handle()
                                                 : pipeline.pipeline());
                             recorder.bind_descriptor_set(VK_PIPELINE_BIND_POINT_COMPUTE,
                                                          pipeline.layout(), 0, set);
                             recorder.push_constants(
                                 pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                 std::array<std::uint32_t, 5>{level, count,
                                                              kind == 2 ? new_texels : dst.capacity,
                                                              fallback_limit_, dst.atlas_axis});
                             if (slicing_.groups) {
                                 // A bounded unit may cover several complete dispatch rows.
                                 // Preserve the shader's absolute workgroup index mapping.
                                 unsigned at = offset, left = chunk;
                                 while (left) {
                                     const unsigned x = at % stride;
                                     const unsigned rows = x == 0 ? left / stride : 0;
                                     const unsigned width =
                                         rows ? stride : std::min(left, stride - x);
                                     const unsigned height = rows ? rows : 1;
                                     vkCmdDispatchBase(c, x, at / stride, 0, width, height, 1);
                                     at += width * height;
                                     left -= width * height;
                                 }
                             } else
                                 recorder.dispatch(std::min(groups, stride),
                                                   (groups + stride - 1) / stride, 1);
                             profiler_->end_pass(c, 0);
                             local_barrier(c);
                         },
                         submit_ns, complete_ns);
                 }}));
            profiler_->collect(0);
            double unit_gpu = 0;
            for (const auto& t : profiler_->latest_timings()) {
                if (kind == 0)
                    result.local_work[15]++; // number of classifier timing spans
                if (kind == 1)
                    result.reuse_gpu_ms += t.milliseconds;
                if (kind == 2)
                    result.fallback_gpu_ms += t.milliseconds;
                unit_gpu += t.milliseconds;
            }
            cost += unit_gpu;
            if (slicing_.after)
                slicing_.after({kind_name, level, chunk, unit_gpu, milliseconds(unit_started),
                                submit_ns, complete_ns});
            offset += chunk;
        }
        return cost;
    };
    unsigned count = 1, input = 0;
    double classify_ms = 0;
    for (unsigned level = 2; level <= 8 && count; level += 2) {
        classify_ms += dispatch(0, level, count, input);
        std::uint32_t next = 0;
        dst.counters_->download(&next, 4);
        local_counts_->download(counts.data(), 64);
        if (counts[2] || next > dst.capacity)
            throw CapacityExceeded("local frontier or slot capacity exceeded");
        count = next;
        input = 1U - input;
    }
    if (counts[3])
        dispatch(1, 8, counts[3], input);
    local_counts_->download(counts.data(), 64);
    if (counts[2])
        throw CapacityExceeded("local numerical fallback list overflow");
    const auto new_texels = counts[11] * 512U, total = new_texels + counts[10];
    if (total)
        dispatch(2, 8, total, input, new_texels);
    result.local_work = counts;
    result.work = {counts[9],  counts[4],  counts[9] * 512U - counts[10],
                   counts[10], new_texels, counts[5],
                   counts[6],  0};
    const auto allocation = dst.stats_.allocation_bytes, builds = dst.stats_.build_count;
    dst.stats_ = {};
    dst.stats_.allocation_bytes = allocation;
    dst.stats_.build_count = builds + 1;
    dst.stats_.brick_count = counts[1];
    dst.stats_.filter_gpu_ms = classify_ms;
    dst.stats_.brick_gpu_ms = result.reuse_gpu_ms + result.fallback_gpu_ms;
    dst.stats_.copied_bricks = base.stats_.brick_count - counts[5] - counts[9];
    dst.stats_.updated_bricks = counts[3];
    dst.stats_.batch_count = 1;
    dst.displayed_ = dst.committed_ = target;
    dst.authored_committed_ = true;
    dst.authored_dispatch_ = true;
    dst.authored_hash_ = authored_geometry_hash(edited_scene);
    dst.displayed_edit_count_ = edited_scene.program.size();
    dst.base_brick_count_ = counts[1];
    dst.committed_edits_.clear();
    dst.last_requested_.clear();
    dst.has_last_requested_ = false;
    local_fields_[&dst] = {*dst.authored_hash_, counts[0]};
    result.wall_ms = milliseconds(start);
    return result;
}
} // namespace cubey::projects::sdf
