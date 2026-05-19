#include <cubey/vulkan/gpu_timestamps.h>

#include <cubey/vulkan/vk_check.h>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::vulkan {

GpuTimestampProfiler::GpuTimestampProfiler(const Device& device, std::uint32_t frame_slot_count,
                                           std::uint32_t max_pass_count)
    : device_(device.handle()), timestamp_period_nanoseconds_(device.timestamp_period_nanoseconds()),
      max_pass_count_(max_pass_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("GPU timestamp profiler requires at least one frame slot");
    }
    if (max_pass_count_ == 0) {
        throw std::runtime_error("GPU timestamp profiler requires at least one pass");
    }
    if (!device.supports_timestamp_queries()) {
        return;
    }

    slots_.resize(frame_slot_count);
    query_pools_.reserve(frame_slot_count);
    for (std::uint32_t i = 0; i < frame_slot_count; ++i) {
        auto info = vk_struct<VkQueryPoolCreateInfo>(VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = max_pass_count_ * 2U;
        VkQueryPool pool = VK_NULL_HANDLE;
        check(vkCreateQueryPool(device_, &info, nullptr, &pool), "vkCreateQueryPool timestamps");
        query_pools_.push_back(pool);
    }
}

GpuTimestampProfiler::~GpuTimestampProfiler() {
    destroy();
}

GpuTimestampProfiler::GpuTimestampProfiler(GpuTimestampProfiler&& other) noexcept {
    *this = std::move(other);
}

GpuTimestampProfiler& GpuTimestampProfiler::operator=(GpuTimestampProfiler&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    destroy();
    device_ = std::exchange(other.device_, VK_NULL_HANDLE);
    timestamp_period_nanoseconds_ = std::exchange(other.timestamp_period_nanoseconds_, 0.0F);
    max_pass_count_ = std::exchange(other.max_pass_count_, 0);
    query_pools_ = std::move(other.query_pools_);
    slots_ = std::move(other.slots_);
    latest_timings_ = std::move(other.latest_timings_);
    return *this;
}

void GpuTimestampProfiler::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    for (VkQueryPool pool : query_pools_) {
        if (pool != VK_NULL_HANDLE) {
            vkDestroyQueryPool(device_, pool, nullptr);
        }
    }
    query_pools_.clear();
}

void GpuTimestampProfiler::validate_slot(std::uint32_t frame_slot_index) const {
    if (frame_slot_index >= query_pools_.size()) {
        throw std::runtime_error("GPU timestamp profiler frame slot is out of range");
    }
}

void GpuTimestampProfiler::begin_frame(VkCommandBuffer command_buffer,
                                       std::uint32_t frame_slot_index) {
    if (!enabled()) {
        return;
    }
    validate_slot(frame_slot_index);
    SlotState& slot = slots_.at(frame_slot_index);
    slot.labels.clear();
    slot.pass_open = false;
    slot.open_query = 0;
    vkCmdResetQueryPool(command_buffer, query_pools_.at(frame_slot_index), 0,
                        max_pass_count_ * 2U);
}

void GpuTimestampProfiler::begin_pass(VkCommandBuffer command_buffer,
                                      std::uint32_t frame_slot_index,
                                      std::string_view label) {
    if (!enabled()) {
        return;
    }
    validate_slot(frame_slot_index);
    SlotState& slot = slots_.at(frame_slot_index);
    if (slot.pass_open) {
        throw std::runtime_error("GPU timestamp profiler pass is already open");
    }
    if (slot.labels.size() >= max_pass_count_) {
        throw std::runtime_error("GPU timestamp profiler pass capacity exceeded");
    }
    const std::uint32_t query = static_cast<std::uint32_t>(slot.labels.size() * 2U);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        query_pools_.at(frame_slot_index), query);
    slot.labels.emplace_back(label);
    slot.open_query = query;
    slot.pass_open = true;
}

void GpuTimestampProfiler::end_pass(VkCommandBuffer command_buffer,
                                    std::uint32_t frame_slot_index) {
    if (!enabled()) {
        return;
    }
    validate_slot(frame_slot_index);
    SlotState& slot = slots_.at(frame_slot_index);
    if (!slot.pass_open) {
        throw std::runtime_error("GPU timestamp profiler has no open pass");
    }
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        query_pools_.at(frame_slot_index), slot.open_query + 1U);
    slot.pass_open = false;
}

void GpuTimestampProfiler::collect(std::uint32_t frame_slot_index) {
    if (!enabled()) {
        return;
    }
    validate_slot(frame_slot_index);
    const SlotState& slot = slots_.at(frame_slot_index);
    if (slot.labels.empty()) {
        latest_timings_.clear();
        return;
    }
    if (slot.pass_open) {
        throw std::runtime_error("GPU timestamp profiler cannot collect an open pass");
    }

    const std::uint32_t query_count = static_cast<std::uint32_t>(slot.labels.size() * 2U);
    std::vector<std::uint64_t> timestamps(query_count);
    const VkResult result = vkGetQueryPoolResults(
        device_, query_pools_.at(frame_slot_index), 0, query_count,
        timestamps.size() * sizeof(std::uint64_t), timestamps.data(), sizeof(std::uint64_t),
        VK_QUERY_RESULT_64_BIT);
    if (result == VK_NOT_READY) {
        return;
    }
    check(result, "vkGetQueryPoolResults timestamps");

    latest_timings_.clear();
    latest_timings_.reserve(slot.labels.size());
    for (std::size_t i = 0; i < slot.labels.size(); ++i) {
        const std::uint64_t begin = timestamps[i * 2U];
        const std::uint64_t end = timestamps[(i * 2U) + 1U];
        const std::uint64_t delta = end >= begin ? end - begin : 0;
        latest_timings_.push_back({
            .label = slot.labels[i],
            .milliseconds =
                static_cast<double>(delta) *
                (static_cast<double>(timestamp_period_nanoseconds_) / 1'000'000.0),
        });
    }
}

} // namespace cubey::vulkan
