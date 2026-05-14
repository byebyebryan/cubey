#include <cubey/render/render_graph.h>

#include <cubey/vulkan/image_transitions.h>

#include <span>
#include <stdexcept>
#include <vector>

namespace cubey::render {

void record_render_graph_barriers(const cubey::vulkan::CommandRecorder& recorder,
                                  const RenderGraphExecutionContext& context,
                                  RenderGraphBarrierPhase phase) {
    const RenderGraphCompiledPass& pass = context.pass();
    const std::vector<RenderGraphTextureBarrier>& texture_barriers =
        phase == RenderGraphBarrierPhase::BeforePass ? pass.before_texture_barriers
                                                     : pass.after_texture_barriers;
    const std::vector<RenderGraphBufferBarrier>& buffer_barriers =
        phase == RenderGraphBarrierPhase::BeforePass ? pass.before_buffer_barriers
                                                     : pass.after_buffer_barriers;

    for (const RenderGraphTextureBarrier& barrier : texture_barriers) {
        const RenderGraphTextureResource& resource = context.texture(barrier.handle);
        const RenderGraphResolvedTexture resolved = context.resolved_texture(barrier.handle);
        if (resolved.image == VK_NULL_HANDLE) {
            throw std::runtime_error("render graph texture barrier requires an allocated texture");
        }
        const auto transition = cubey::vulkan::ImageLayoutTransition{
            .image = resolved.image,
            .aspect_mask = resource.desc.aspects,
            .old_layout = barrier.source_state.layout,
            .new_layout = barrier.destination_state.layout,
            .src_access_mask = barrier.source_state.access_mask,
            .dst_access_mask = barrier.destination_state.access_mask,
            .src_stage_mask = barrier.source_state.stage_mask,
            .dst_stage_mask = barrier.destination_state.stage_mask,
        };
        const VkImageMemoryBarrier image_barrier = cubey::vulkan::image_memory_barrier(transition);
        recorder.pipeline_barrier(transition.src_stage_mask, transition.dst_stage_mask, 0, {}, {},
                                  std::span<const VkImageMemoryBarrier>(&image_barrier, 1));
    }

    for (const RenderGraphBufferBarrier& barrier : buffer_barriers) {
        const RenderGraphResolvedBuffer resolved = context.resolved_buffer(barrier.handle);
        if (resolved.buffer == VK_NULL_HANDLE) {
            throw std::runtime_error("render graph buffer barrier requires an allocated buffer");
        }
        auto vk_barrier = VkBufferMemoryBarrier{};
        vk_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        vk_barrier.srcAccessMask = barrier.source_state.access_mask;
        vk_barrier.dstAccessMask = barrier.destination_state.access_mask;
        vk_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        vk_barrier.buffer = resolved.buffer;
        vk_barrier.offset = 0;
        vk_barrier.size = resolved.byte_size;
        recorder.pipeline_barrier(barrier.source_state.stage_mask,
                                  barrier.destination_state.stage_mask, 0, {},
                                  std::span<const VkBufferMemoryBarrier>(&vk_barrier, 1), {});
    }
}

} // namespace cubey::render
