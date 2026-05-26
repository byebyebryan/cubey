#include <cubey/render/render_graph.h>

#include <cubey/vulkan/command_recorder.h>

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
                                                           const CompiledRenderGraph& graph) {
    validate_slot(slot);
    std::optional<RenderGraphResourceSet>& resources = slots_[static_cast<std::size_t>(slot.index)];
    if (resources.has_value() && resources->compatible(graph)) {
        resources->reset(graph);
        return resources.value();
    }
    resources.emplace(graph);
    return resources.value();
}

RenderGraphResourceSet& RenderGraphFrameResources::emplace(FrameSlot slot,
                                                           const cubey::vulkan::Device& device,
                                                           const CompiledRenderGraph& graph) {
    validate_slot(slot);
    std::optional<RenderGraphResourceSet>& resources = slots_[static_cast<std::size_t>(slot.index)];
    if (resources.has_value() && resources->compatible(graph)) {
        resources->reset(graph);
        return resources.value();
    }
    resources.emplace(device, graph);
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

    RenderGraphResourceSet& resources = resources_.emplace(info.frame_slot, *info.device, graph);
    if (prepare) {
        prepare(resources);
    }

    const cubey::vulkan::CommandRecorder recorder(info.command_buffer);
    switch (info.command_buffer_mode) {
    case RenderGraphCommandBufferMode::BeginAndEnd:
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        graph.execute(resources, recorder, info.profiler, info.frame_slot.index);
        recorder.end(info.label != nullptr ? info.label : "vkEndCommandBuffer render graph");
        return;
    case RenderGraphCommandBufferMode::AlreadyRecording:
        graph.execute(resources, recorder, info.profiler, info.frame_slot.index);
        return;
    }
    throw std::runtime_error("render graph command buffer mode is invalid");
}

} // namespace cubey::render
