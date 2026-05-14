#include "render_graph_test_helpers.h"

#include <cubey/render/render_graph.h>
#include <cubey/vulkan/command_recorder.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>
#include <vector>

using namespace cubey::tests::render_graph;

void test_render_graph_executes_callbacks_in_pass_order_and_exposes_context() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle color =
        graph.import_texture(color_texture_desc("color"), image(0x81), view(0x82));
    const cubey::render::RenderGraphBufferHandle constants =
        graph.import_buffer(buffer_desc("constants"), buffer(0x83));

    std::vector<std::string> executed_labels;
    graph.add_pass("first", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(color)
        .read_uniform_buffer(constants)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            require(context.pass_index() == 0, "first context should expose pass index");
            require(context.pass().label == "first", "context should expose compiled pass");
            require(context.texture(color).imported_image == image(0x81),
                    "context should resolve texture resources");
            require(context.buffer(constants).imported_buffer == buffer(0x83),
                    "context should resolve buffer resources");
            executed_labels.push_back(context.pass().label);
        });
    graph.add_pass("second", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(color)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            require(context.pass_index() == 1, "second context should expose pass index");
            executed_labels.push_back(context.pass().label);
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    compiled.execute();

    require(executed_labels.size() == 2, "graph execution should run both pass callbacks");
    require(executed_labels[0] == "first", "graph execution should preserve first pass order");
    require(executed_labels[1] == "second", "graph execution should preserve second pass order");
}

void test_render_graph_execute_rejects_missing_callbacks_but_compile_allows_declarations() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle color =
        graph.import_texture(color_texture_desc("color"), image(0x91), view(0x92));
    graph.add_pass("declaration only", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(color);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes().size() == 1, "declaration-only graph should still compile");
    require_throws([&compiled] { compiled.execute(); },
                   "graph execution should require execute callbacks");
}

void test_render_graph_execute_propagates_callback_exceptions() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle color =
        graph.import_texture(color_texture_desc("color"), image(0x93), view(0x94));
    graph.add_pass("failing", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(color)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {
            throw std::runtime_error("callback failure");
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    try {
        compiled.execute();
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()) == "callback failure",
                "graph execution should propagate callback exceptions");
        return;
    }
    throw std::runtime_error("graph execution should propagate callback exceptions");
}

void test_render_graph_execute_with_recorder_exposes_command_recorder() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle color =
        graph.import_texture(color_texture_desc("color"), image(0x95), view(0x96));
    const VkCommandBuffer command_buffer = reinterpret_cast<VkCommandBuffer>(0x97);
    const cubey::vulkan::CommandRecorder recorder(command_buffer);

    bool saw_recorder = false;
    graph.add_pass("record", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(color)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            require(context.recorder().handle() == command_buffer,
                    "recorder-aware execution should expose the active command recorder");
            saw_recorder = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    const cubey::render::RenderGraphResourceSet resources(compiled);
    compiled.execute(resources, recorder);

    require(saw_recorder, "recorder-aware execution should run the pass callback");
}

void test_render_graph_recorder_access_rejects_recorderless_execution() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle color =
        graph.import_texture(color_texture_desc("color"), image(0x98), view(0x99));
    graph.add_pass("record", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(color)
        .execute([](const cubey::render::RenderGraphExecutionContext& context) {
            (void)context.recorder();
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require_throws([&compiled] { compiled.execute(); },
                   "recorder-less graph execution should reject recorder access");
}

void test_render_graph_execution_resolves_bound_transient_resources() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle transient =
        graph.create_buffer(buffer_desc("transient field"));

    bool resolved_transient = false;
    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(transient)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::render::RenderGraphResolvedBuffer& resolved =
                context.resolved_buffer(transient);
            require(resolved.buffer == buffer(0x701),
                    "execution context should resolve bound transient buffer handles");
            require(resolved.byte_size == buffer_desc("transient field").byte_size,
                    "execution context should preserve bound transient buffer size");
            resolved_transient = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    cubey::render::RenderGraphResourceSet resources(compiled);
    resources.bind_buffer(transient, cubey::render::RenderGraphResolvedBuffer{
                                         .buffer = buffer(0x701),
                                         .byte_size = buffer_desc("transient field").byte_size,
                                     });
    compiled.execute(resources);

    require(resolved_transient, "transient resource resolution test should execute");
}

void test_render_graph_resolves_color_target_view_from_bound_transient_texture() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle transient =
        graph.create_texture(color_texture_desc("scene color"));

    bool resolved_target = false;
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(transient)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::render::ColorTargetView target =
                cubey::render::resolved_color_target_view(context, transient);
            require(target.extent.width == color_texture_desc("scene color").extent.width,
                    "resolved color target should preserve graph texture width");
            require(target.extent.height == color_texture_desc("scene color").extent.height,
                    "resolved color target should preserve graph texture height");
            require(target.format == color_texture_desc("scene color").format,
                    "resolved color target should preserve graph texture format");
            require(target.image == image(0x801),
                    "resolved color target should use bound transient image");
            require(target.view == view(0x802),
                    "resolved color target should use bound transient image view");
            resolved_target = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    cubey::render::RenderGraphResourceSet resources(compiled);
    resources.bind_texture(transient, cubey::render::RenderGraphResolvedTexture{
                                          .image = image(0x801),
                                          .view = view(0x802),
                                      });
    compiled.execute(resources);

    require(resolved_target, "resolved color target view test should execute");
}

void test_render_graph_resolved_color_target_view_rejects_depth_texture() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle depth =
        graph.import_texture(depth_texture_desc("depth"), image(0x811), view(0x812));

    bool rejected_depth = false;
    graph.add_pass("depth", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_depth(depth)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            require_throws([&] { (void)cubey::render::resolved_color_target_view(context, depth); },
                           "resolved color target view should reject depth textures");
            rejected_depth = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    compiled.execute();

    require(rejected_depth, "resolved color target depth rejection test should execute");
}

void test_render_graph_resolves_sampled_color_texture_view() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle color =
        graph.create_texture(color_texture_desc("scene color"));

    bool resolved_sampled = false;
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(color)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::render::RenderGraphSampledTextureView sampled =
                cubey::render::resolved_sampled_texture_view(context, color);
            require(sampled.image == image(0x921),
                    "sampled color view should use bound transient image");
            require(sampled.view == view(0x922),
                    "sampled color view should use bound transient view");
            require(sampled.layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                    "sampled color view should use shader-read layout");
            resolved_sampled = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    cubey::render::RenderGraphResourceSet resources(compiled);
    resources.bind_texture(color, cubey::render::RenderGraphResolvedTexture{
                                      .image = image(0x921),
                                      .view = view(0x922),
                                  });
    compiled.execute(resources);

    require(resolved_sampled, "sampled color view test should execute");
}

void test_render_graph_resolves_sampled_depth_texture_view() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle depth =
        graph.import_texture(depth_texture_desc("shadow depth"), image(0x931), view(0x932));
    graph.add_pass("sample", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(depth)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    const cubey::render::RenderGraphResourceSet resources(compiled);
    const cubey::render::RenderGraphSampledTextureView sampled =
        cubey::render::resolved_sampled_texture_view(compiled, resources, depth);

    require(sampled.image == image(0x931), "sampled depth view should preserve imported image");
    require(sampled.view == view(0x932), "sampled depth view should preserve imported view");
    require(sampled.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            "sampled depth view should use depth read-only layout");
}

void test_render_graph_sampled_texture_view_rejects_unallocated_transient() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle transient =
        graph.create_texture(color_texture_desc("scene color"));

    bool rejected_unallocated = false;
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(transient)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            require_throws(
                [&] { (void)cubey::render::resolved_sampled_texture_view(context, transient); },
                "sampled transient texture view should require an allocated resource");
            rejected_unallocated = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    compiled.execute();

    require(rejected_unallocated, "sampled transient rejection test should execute");
}
