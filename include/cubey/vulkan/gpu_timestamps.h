#pragma once

#include <cubey/vulkan/device.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::vulkan {

struct GpuPassTiming {
    std::string label;
    double milliseconds = 0.0;
};

class GpuTimestampProfiler {
  public:
    GpuTimestampProfiler(const Device& device, std::uint32_t frame_slot_count,
                         std::uint32_t max_pass_count);
    ~GpuTimestampProfiler();

    GpuTimestampProfiler(const GpuTimestampProfiler&) = delete;
    GpuTimestampProfiler& operator=(const GpuTimestampProfiler&) = delete;
    GpuTimestampProfiler(GpuTimestampProfiler&& other) noexcept;
    GpuTimestampProfiler& operator=(GpuTimestampProfiler&& other) noexcept;

    [[nodiscard]] bool enabled() const {
        return !query_pools_.empty();
    }

    void begin_frame(VkCommandBuffer command_buffer, std::uint32_t frame_slot_index);
    void begin_pass(VkCommandBuffer command_buffer, std::uint32_t frame_slot_index,
                    std::string_view label);
    void end_pass(VkCommandBuffer command_buffer, std::uint32_t frame_slot_index);
    void collect(std::uint32_t frame_slot_index);

    [[nodiscard]] const std::vector<GpuPassTiming>& latest_timings() const {
        return latest_timings_;
    }

  private:
    struct SlotState {
        std::vector<std::string> labels;
        bool pass_open = false;
        std::uint32_t open_query = 0;
    };

    void destroy();
    void validate_slot(std::uint32_t frame_slot_index) const;

    VkDevice device_ = VK_NULL_HANDLE;
    float timestamp_period_nanoseconds_ = 0.0F;
    std::uint32_t max_pass_count_ = 0;
    std::vector<VkQueryPool> query_pools_;
    std::vector<SlotState> slots_;
    std::vector<GpuPassTiming> latest_timings_;
};

} // namespace cubey::vulkan
