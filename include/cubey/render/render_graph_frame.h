#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/render/render_graph_resources.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace cubey::vulkan {
class Device;
class GpuTimestampProfiler;
} // namespace cubey::vulkan

namespace cubey::render {

class RenderGraphFrameResources {
  public:
    RenderGraphFrameResources() = default;
    explicit RenderGraphFrameResources(std::uint32_t frame_slot_count);

    void resize(std::uint32_t frame_slot_count);
    void clear();

    [[nodiscard]] std::uint32_t frame_slot_count() const;

    RenderGraphResourceSet& emplace(FrameSlot slot, const CompiledRenderGraph& graph);
    RenderGraphResourceSet& emplace(FrameSlot slot, const cubey::vulkan::Device& device,
                                    const CompiledRenderGraph& graph);

    [[nodiscard]] RenderGraphResourceSet& resource_set(FrameSlot slot);
    [[nodiscard]] const RenderGraphResourceSet& resource_set(FrameSlot slot) const;

  private:
    void validate_slot(FrameSlot slot) const;

    std::vector<std::optional<RenderGraphResourceSet>> slots_{};
};

enum class RenderGraphCommandBufferMode : std::uint8_t {
    BeginAndEnd,
    AlreadyRecording,
};

struct RenderGraphFrameRecordInfo {
    const cubey::vulkan::Device* device = nullptr;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    FrameSlot frame_slot{};
    const char* label = "vkEndCommandBuffer render graph";
    RenderGraphCommandBufferMode command_buffer_mode = RenderGraphCommandBufferMode::BeginAndEnd;
    cubey::vulkan::GpuTimestampProfiler* profiler = nullptr;
};

using RenderGraphPrepareCallback = std::function<void(const RenderGraphResourceSet&)>;

class RenderGraphFrameExecutor {
  public:
    RenderGraphFrameExecutor() = default;
    explicit RenderGraphFrameExecutor(std::uint32_t frame_slot_count);

    void resize(std::uint32_t frame_slot_count);
    void clear();

    [[nodiscard]] std::uint32_t frame_slot_count() const;

    void record(const RenderGraphFrameRecordInfo& info, const CompiledRenderGraph& graph,
                RenderGraphPrepareCallback prepare = {});

  private:
    RenderGraphFrameResources resources_{};
};

} // namespace cubey::render
