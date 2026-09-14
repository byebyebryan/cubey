#include <cubey/render/render_graph.h>

#include <cubey/vulkan/command_recorder.h>

#include <chrono>
#include <stdexcept>

namespace cubey::render {

RenderGraphFrameResources::RenderGraphFrameResources(std::uint32_t frame_slot_count) {
    resize(frame_slot_count);
}

void RenderGraphFrameResources::resize(std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("render graph frame resources require at least one frame slot");
    }
    slots_.clear();
    slots_.resize(frame_slot_count);
}

void RenderGraphFrameResources::clear() {
    slots_.clear();
}

std::uint32_t RenderGraphFrameResources::frame_slot_count() const {
    return static_cast<std::uint32_t>(slots_.size());
}

RenderGraphResourceSet& RenderGraphFrameResources::emplace(FrameSlot slot,
                                                           const CompiledRenderGraph& graph,
                                                           RenderGraphFrameSlotAction* action) {
    validate_slot(slot);
    std::optional<RenderGraphResourceSet>& resources = slots_[static_cast<std::size_t>(slot.index)];
    if (resources.has_value() && resources->compatible(graph)) {
        resources->reset_compatible();
        if (action != nullptr) {
            *action = RenderGraphFrameSlotAction::Reused;
        }
        return resources.value();
    }
    const RenderGraphFrameSlotAction slot_action = resources.has_value()
                                                       ? RenderGraphFrameSlotAction::Replaced
                                                       : RenderGraphFrameSlotAction::Created;
    resources.emplace(graph);
    if (action != nullptr) {
        *action = slot_action;
    }
    return resources.value();
}

RenderGraphResourceSet& RenderGraphFrameResources::emplace(FrameSlot slot,
                                                           const cubey::vulkan::Device& device,
                                                           const CompiledRenderGraph& graph,
                                                           RenderGraphFrameSlotAction* action) {
    validate_slot(slot);
    std::optional<RenderGraphResourceSet>& resources = slots_[static_cast<std::size_t>(slot.index)];
    if (resources.has_value() && resources->compatible(graph)) {
        resources->reset_compatible();
        if (action != nullptr) {
            *action = RenderGraphFrameSlotAction::Reused;
        }
        return resources.value();
    }
    const RenderGraphFrameSlotAction slot_action = resources.has_value()
                                                       ? RenderGraphFrameSlotAction::Replaced
                                                       : RenderGraphFrameSlotAction::Created;
    resources.emplace(device, graph);
    if (action != nullptr) {
        *action = slot_action;
    }
    return resources.value();
}

RenderGraphResourceSet& RenderGraphFrameResources::resource_set(FrameSlot slot) {
    validate_slot(slot);
    std::optional<RenderGraphResourceSet>& resources = slots_[static_cast<std::size_t>(slot.index)];
    if (!resources.has_value()) {
        throw std::runtime_error("render graph frame resource slot has no resource set");
    }
    return resources.value();
}

const RenderGraphResourceSet& RenderGraphFrameResources::resource_set(FrameSlot slot) const {
    validate_slot(slot);
    const std::optional<RenderGraphResourceSet>& resources =
        slots_[static_cast<std::size_t>(slot.index)];
    if (!resources.has_value()) {
        throw std::runtime_error("render graph frame resource slot has no resource set");
    }
    return resources.value();
}

void RenderGraphFrameResources::validate_slot(FrameSlot slot) const {
    validate_frame_slot(slot);
    if (slot.count != frame_slot_count()) {
        throw std::runtime_error("render graph frame resource slot count does not match");
    }
}

RenderGraphFrameExecutor::RenderGraphFrameExecutor(std::uint32_t frame_slot_count)
    : resources_(frame_slot_count) {}

void RenderGraphFrameExecutor::resize(std::uint32_t frame_slot_count) {
    resources_.resize(frame_slot_count);
}

void RenderGraphFrameExecutor::clear() {
    resources_.clear();
}

std::uint32_t RenderGraphFrameExecutor::frame_slot_count() const {
    return resources_.frame_slot_count();
}

void RenderGraphFrameExecutor::record(const RenderGraphFrameRecordInfo& info,
                                      const CompiledRenderGraph& graph,
                                      RenderGraphPrepareCallback prepare) {
    if (info.device == nullptr) {
        throw std::runtime_error("render graph frame executor requires a device");
    }
    if (info.command_buffer == VK_NULL_HANDLE) {
        throw std::runtime_error("render graph frame executor requires a command buffer");
    }
    validate_frame_slot(info.frame_slot);
    if (info.metrics != nullptr) {
        *info.metrics = {};
    }

    using Clock = std::chrono::steady_clock;
    const Clock::time_point resource_prepare_start =
        info.metrics != nullptr ? Clock::now() : Clock::time_point{};
    RenderGraphFrameSlotAction slot_action = RenderGraphFrameSlotAction::Unknown;
    RenderGraphResourceSet& resources = resources_.emplace(
        info.frame_slot, *info.device, graph, info.metrics != nullptr ? &slot_action : nullptr);
    if (prepare) {
        prepare(resources);
    }
    if (info.metrics != nullptr) {
        info.metrics->slot_action = slot_action;
        info.metrics->resource_prepare_milliseconds =
            std::chrono::duration<double, std::milli>(Clock::now() - resource_prepare_start)
                .count();
    }

    const cubey::vulkan::CommandRecorder recorder(info.command_buffer);
    const Clock::time_point graph_record_start =
        info.metrics != nullptr ? Clock::now() : Clock::time_point{};
    switch (info.command_buffer_mode) {
    case RenderGraphCommandBufferMode::BeginAndEnd:
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        graph.execute(resources, recorder, info.profiler, info.frame_slot.index);
        recorder.end(info.label != nullptr ? info.label : "vkEndCommandBuffer render graph");
        break;
    case RenderGraphCommandBufferMode::AlreadyRecording:
        graph.execute(resources, recorder, info.profiler, info.frame_slot.index);
        break;
    default:
        throw std::runtime_error("render graph command buffer mode is invalid");
    }
    if (info.metrics != nullptr) {
        info.metrics->graph_record_milliseconds =
            std::chrono::duration<double, std::milli>(Clock::now() - graph_record_start).count();
    }
}

} // namespace cubey::render
