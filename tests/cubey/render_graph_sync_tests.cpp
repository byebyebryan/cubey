#include "render_graph_test_helpers.h"

#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

using namespace cubey::tests::render_graph;

void test_render_graph_derives_depth_to_sampled_texture_barrier() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle shadow_depth =
        graph.import_texture(depth_texture_desc("shadow depth"), image(0x101), view(0x102));

    graph.add_pass("shadow", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_depth(shadow_depth);
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(shadow_depth);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].before_texture_barriers.empty(),
            "first in-graph writer should not need an incoming texture barrier");
    require(compiled.passes()[1].before_texture_barriers.size() == 1,
            "sampled read after depth write should derive one texture barrier");

    const cubey::render::RenderGraphTextureBarrier& barrier =
        compiled.passes()[1].before_texture_barriers[0];
    require(barrier.handle == shadow_depth, "texture barrier should identify the resource");
    require(barrier.source_pass_index == 0, "texture barrier should point at producer pass");
    require(barrier.source_usage == cubey::render::RenderGraphTextureUsage::DepthAttachment,
            "texture barrier should preserve producer usage");
    require(barrier.destination_usage == cubey::render::RenderGraphTextureUsage::SampledRead,
            "texture barrier should preserve consumer usage");
    require(barrier.source_state.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            "depth producer should transition from depth attachment layout");
    require(barrier.destination_state.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            "sampled depth consumer should transition to read-only depth layout");
    require(barrier.source_state.access_mask == (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT),
            "depth producer should expose depth attachment access");
    require(barrier.destination_state.access_mask == VK_ACCESS_SHADER_READ_BIT,
            "sampled consumer should request shader read access");
}

void test_render_graph_derives_compute_to_graphics_storage_buffer_barrier() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle field =
        graph.import_buffer(buffer_desc("field"), buffer(0x201));

    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(field);
    graph.add_pass("render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(field);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[1].before_buffer_barriers.size() == 1,
            "graphics read after compute storage write should derive one buffer barrier");
    const cubey::render::RenderGraphBufferBarrier& barrier =
        compiled.passes()[1].before_buffer_barriers[0];
    require(barrier.handle == field, "buffer barrier should identify the resource");
    require(barrier.source_pass_index == 0, "buffer barrier should point at producer pass");
    require(barrier.source_usage == cubey::render::RenderGraphBufferUsage::StorageReadWrite,
            "buffer barrier should preserve read-write producer usage");
    require(barrier.destination_usage == cubey::render::RenderGraphBufferUsage::StorageRead,
            "buffer barrier should preserve storage-read consumer usage");
    require(barrier.source_state.stage_mask == VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            "compute producer should use compute shader source stage");
    require(barrier.destination_state.stage_mask == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            "graphics storage read should use fragment shader destination stage");
    require(barrier.source_state.access_mask ==
                (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT),
            "read-write producer should expose shader read and write access");
    require(barrier.destination_state.access_mask == VK_ACCESS_SHADER_READ_BIT,
            "storage read consumer should request shader read access");
}

void test_render_graph_derives_imported_texture_acquire_and_release_barriers() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_texture(color_texture_desc("backbuffer"), image(0x601), view(0x602),
                             undefined_texture_state(), present_texture_state());

    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(backbuffer);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].before_texture_barriers.size() == 1,
            "imported initial texture state should derive one acquire barrier");
    const cubey::render::RenderGraphTextureBarrier& acquire =
        compiled.passes()[0].before_texture_barriers[0];
    require(acquire.handle == backbuffer, "acquire barrier should identify the texture");
    require(!acquire.source_usage.has_value(), "acquire barrier should not have producer usage");
    require(acquire.destination_usage == cubey::render::RenderGraphTextureUsage::ColorAttachment,
            "acquire barrier should target the first pass usage");
    require(acquire.source_state.layout == VK_IMAGE_LAYOUT_UNDEFINED,
            "acquire barrier should use imported initial layout");
    require(acquire.destination_state.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "acquire barrier should transition to color attachment layout");

    require(compiled.passes()[0].after_texture_barriers.size() == 1,
            "imported final texture state should derive one release barrier");
    const cubey::render::RenderGraphTextureBarrier& release =
        compiled.passes()[0].after_texture_barriers[0];
    require(release.handle == backbuffer, "release barrier should identify the texture");
    require(release.source_usage == cubey::render::RenderGraphTextureUsage::ColorAttachment,
            "release barrier should start from the last pass usage");
    require(!release.destination_usage.has_value(),
            "release barrier should not have consumer usage");
    require(release.source_state.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "release barrier should start from color attachment layout");
    require(release.destination_state.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "release barrier should transition to imported final layout");
}

void test_render_graph_derives_transient_texture_first_use_barrier() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle transient =
        graph.create_texture(color_texture_desc("transient color"));

    graph.add_pass("render", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(transient);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].before_texture_barriers.size() == 1,
            "transient texture first use should derive an undefined acquire barrier");
    const cubey::render::RenderGraphTextureBarrier& barrier =
        compiled.passes()[0].before_texture_barriers[0];
    require(barrier.handle == transient, "transient first-use barrier should identify texture");
    require(barrier.source_state.layout == VK_IMAGE_LAYOUT_UNDEFINED,
            "transient first-use barrier should start from undefined");
    require(barrier.destination_state.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "transient first-use barrier should transition to first usage");
    require(compiled.passes()[0].after_texture_barriers.empty(),
            "transient texture without exported final state should not derive release barrier");
}

void test_render_graph_derives_imported_buffer_acquire_and_release_barriers() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle constants =
        graph.import_buffer(buffer_desc("constants"), buffer(0x611), host_written_buffer_state(),
                            cubey::render::RenderGraphBufferState{
                                .access_mask = VK_ACCESS_TRANSFER_READ_BIT,
                                .stage_mask = VK_PIPELINE_STAGE_TRANSFER_BIT,
                            });

    graph.add_pass("read constants", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_uniform_buffer(constants);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].before_buffer_barriers.size() == 1,
            "imported initial buffer state should derive one acquire barrier");
    require(compiled.passes()[0].before_buffer_barriers[0].source_state.access_mask ==
                VK_ACCESS_HOST_WRITE_BIT,
            "buffer acquire should preserve initial access mask");
    require(compiled.passes()[0].before_buffer_barriers[0].destination_usage ==
                cubey::render::RenderGraphBufferUsage::UniformRead,
            "buffer acquire should target first usage");

    require(compiled.passes()[0].after_buffer_barriers.size() == 1,
            "imported final buffer state should derive one release barrier");
    require(compiled.passes()[0].after_buffer_barriers[0].source_usage ==
                cubey::render::RenderGraphBufferUsage::UniformRead,
            "buffer release should start from last usage");
    require(compiled.passes()[0].after_buffer_barriers[0].destination_state.access_mask ==
                VK_ACCESS_TRANSFER_READ_BIT,
            "buffer release should preserve final access mask");
}

void test_render_graph_omits_read_after_read_barriers() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle constants =
        graph.import_buffer(buffer_desc("constants"), buffer(0x301));

    graph.add_pass("first", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_uniform_buffer(constants);
    graph.add_pass("second", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_uniform_buffer(constants);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[1].before_buffer_barriers.empty(),
            "read-after-read should not derive a buffer barrier");
}

void test_render_graph_storage_read_write_initializes_transient_buffers() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle field =
        graph.create_buffer(buffer_desc("transient field"));

    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(field);
    graph.add_pass("render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(field);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].buffer_accesses[0].usage ==
                cubey::render::RenderGraphBufferUsage::StorageReadWrite,
            "read-write storage usage should be preserved");
    require(compiled.passes()[1].before_buffer_barriers.size() == 1,
            "transient read-write producer should satisfy later storage reads");
}

void test_render_graph_barrier_recording_rejects_unallocated_transient_resources() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle transient =
        graph.create_buffer(buffer_desc("transient field"));

    bool simulate_executed = false;
    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(transient)
        .execute(
            [&](const cubey::render::RenderGraphExecutionContext&) { simulate_executed = true; });

    bool render_executed = false;
    graph.add_pass("render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(transient)
        .execute(
            [&](const cubey::render::RenderGraphExecutionContext&) { render_executed = true; });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    const cubey::render::RenderGraphResourceSet resources(compiled);
    const cubey::vulkan::CommandRecorder recorder(reinterpret_cast<VkCommandBuffer>(0x501));

    require_throws([&] { compiled.execute(resources, recorder); },
                   "recorder-backed graph execution should reject unresolved transient barriers");

    require(simulate_executed, "graph should execute passes before an unresolved barrier");
    require(!render_executed, "graph should record before-pass barriers before pass callbacks");
}
