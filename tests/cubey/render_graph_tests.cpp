#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_throws(auto&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] VkImage image(std::uintptr_t value) {
    return reinterpret_cast<VkImage>(value);
}

[[nodiscard]] VkImageView view(std::uintptr_t value) {
    return reinterpret_cast<VkImageView>(value);
}

[[nodiscard]] VkBuffer buffer(std::uintptr_t value) {
    return reinterpret_cast<VkBuffer>(value);
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc color_texture_desc(const char* label) {
    return {
        .label = label,
        .extent = {640, 360},
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureDesc depth_texture_desc(const char* label) {
    return {
        .label = label,
        .extent = {1024, 1024},
        .format = VK_FORMAT_D32_SFLOAT,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphBufferDesc buffer_desc(const char* label) {
    return {
        .label = label,
        .byte_size = 4096,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState undefined_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_UNDEFINED,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphTextureState present_texture_state() {
    return {
        .layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .access_mask = 0,
        .stage_mask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
    };
}

[[nodiscard]] cubey::render::RenderGraphBufferState host_written_buffer_state() {
    return {
        .access_mask = VK_ACCESS_HOST_WRITE_BIT,
        .stage_mask = VK_PIPELINE_STAGE_HOST_BIT,
    };
}

} // namespace

void test_render_graph_imports_color_and_depth_targets() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::ColorTargetView color = cubey::render::color_target_view(
        {640, 360}, VK_FORMAT_R8G8B8A8_UNORM, image(0x01), view(0x02));
    const cubey::render::DepthTargetView depth =
        cubey::render::depth_target_view({640, 360}, VK_FORMAT_D32_SFLOAT, image(0x03), view(0x04));

    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_color_target("backbuffer", color);
    const cubey::render::RenderGraphTextureHandle depth_target =
        graph.import_depth_target("scene depth", depth);
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(backbuffer)
        .write_depth(depth_target);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.textures().size() == 2, "graph should preserve imported targets");
    require(compiled.passes().size() == 1, "graph should preserve declared pass");
    require(compiled.texture(backbuffer).lifetime ==
                cubey::render::RenderGraphResourceLifetime::Imported,
            "color target should be imported");
    require(compiled.texture(backbuffer).desc.aspects == VK_IMAGE_ASPECT_COLOR_BIT,
            "color target should declare color aspect");
    require(compiled.texture(backbuffer).imported_image == color.image,
            "color target should preserve image handle");
    require(compiled.texture(depth_target).desc.aspects == VK_IMAGE_ASPECT_DEPTH_BIT,
            "depth target should declare depth aspect");
    require(compiled.texture(depth_target).imported_view == depth.view,
            "depth target should preserve image view");
}

void test_render_graph_creates_transient_texture_and_preserves_pass_order() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_texture(color_texture_desc("backbuffer"), image(0x11), view(0x12));
    const cubey::render::RenderGraphTextureHandle shadow_map =
        graph.create_texture(depth_texture_desc("shadow map"));

    graph.add_pass("shadow", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_depth(shadow_map);
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(shadow_map)
        .write_color(backbuffer);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.texture(shadow_map).lifetime ==
                cubey::render::RenderGraphResourceLifetime::Transient,
            "created texture should be transient");
    require(compiled.passes().size() == 2, "graph should preserve both passes");
    require(compiled.passes()[0].label == "shadow", "compile should preserve first pass label");
    require(compiled.passes()[1].label == "scene", "compile should preserve second pass label");
    require(compiled.passes()[1].texture_accesses[0].usage ==
                cubey::render::RenderGraphTextureUsage::SampledRead,
            "scene pass should preserve sampled read usage");
}

void test_render_graph_declares_shadow_map_then_scene_sample_flow() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle backbuffer =
        graph.import_texture(color_texture_desc("backbuffer"), image(0x71), view(0x72));
    const cubey::render::RenderGraphTextureHandle scene_depth =
        graph.import_texture(depth_texture_desc("scene depth"), image(0x73), view(0x74));
    const cubey::render::RenderGraphTextureHandle shadow_depth =
        graph.import_texture(depth_texture_desc("shadow depth"), image(0x75), view(0x76));

    graph.add_pass("shadow", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_depth(shadow_depth);
    graph.add_pass("scene", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(shadow_depth)
        .write_color(backbuffer)
        .write_depth(scene_depth);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.textures().size() == 3,
            "shadow graph should preserve imported frame resources");
    require(compiled.passes().size() == 2, "shadow graph should preserve both passes");
    require(compiled.passes()[0].label == "shadow", "shadow pass should stay first");
    require(compiled.passes()[0].texture_accesses[0].usage ==
                cubey::render::RenderGraphTextureUsage::DepthAttachment,
            "shadow pass should write the shadow depth target");
    require(compiled.passes()[1].label == "scene", "scene pass should stay second");
    require(compiled.passes()[1].texture_accesses[0].usage ==
                cubey::render::RenderGraphTextureUsage::SampledRead,
            "scene pass should sample the shadow depth target");
    require(compiled.passes()[1].texture_accesses[1].usage ==
                cubey::render::RenderGraphTextureUsage::ColorAttachment,
            "scene pass should write the backbuffer");
    require(compiled.passes()[1].texture_accesses[2].usage ==
                cubey::render::RenderGraphTextureUsage::DepthAttachment,
            "scene pass should write scene depth");
}

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

void test_render_graph_rejects_transient_texture_read_before_write() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle transient =
        graph.create_texture(color_texture_desc("uninitialized"));
    graph.add_pass("sample", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(transient);

    require_throws([&graph] { (void)graph.compile(); },
                   "transient texture reads should require an earlier write");
}

void test_render_graph_allows_imported_texture_read_without_prior_write() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle imported =
        graph.import_texture(color_texture_desc("history"), image(0x21), view(0x22));
    graph.add_pass("sample", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(imported);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes().size() == 1, "imported texture read should compile");
    require(compiled.passes()[0].texture_accesses[0].usage ==
                cubey::render::RenderGraphTextureUsage::SampledRead,
            "imported texture read should preserve sampled usage");
}

void test_render_graph_rejects_invalid_resource_descriptors_and_handles() {
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            (void)graph.create_texture(cubey::render::RenderGraphTextureDesc{
                .label = "",
                .extent = {1, 1},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
            });
        },
        "texture labels should be required");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            (void)graph.create_texture(cubey::render::RenderGraphTextureDesc{
                .label = "missing extent",
                .extent = {0, 1},
                .format = VK_FORMAT_R8G8B8A8_UNORM,
                .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
            });
        },
        "texture extents should be required");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            (void)graph.create_texture(cubey::render::RenderGraphTextureDesc{
                .label = "missing format",
                .extent = {1, 1},
                .format = VK_FORMAT_UNDEFINED,
                .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
            });
        },
        "texture formats should be required");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            (void)graph.create_buffer(cubey::render::RenderGraphBufferDesc{
                .label = "empty buffer",
                .byte_size = 0,
            });
        },
        "buffer byte size should be required");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            (void)graph.import_buffer(buffer_desc("null import"), VK_NULL_HANDLE);
        },
        "imported buffers should require a Vulkan buffer");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            (void)graph.add_pass("", cubey::render::RenderGraphQueueDomain::Graphics);
        },
        "pass labels should be required");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            graph.add_pass("bad handle", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_texture(cubey::render::RenderGraphTextureHandle{.index = 99});
        },
        "texture access should reject invalid handles");
}

void test_render_graph_rejects_attachment_usage_outside_graphics_pass() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle target =
        graph.create_texture(color_texture_desc("compute target"));

    require_throws(
        [&graph, target] {
            graph.add_pass("compute", cubey::render::RenderGraphQueueDomain::Compute)
                .write_color(target);
        },
        "compute passes should not write color attachments");
}

void test_render_graph_rejects_incompatible_same_pass_resource_access() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle target =
        graph.import_texture(color_texture_desc("target"), image(0x31), view(0x32));
    graph.add_pass("bad pass", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_texture(target)
        .write_color(target);

    require_throws([&graph] { (void)graph.compile(); },
                   "a pass should not read and write the same texture");
}

void test_render_graph_declares_compute_storage_buffer_flow() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle constants =
        graph.import_buffer(buffer_desc("constants"), buffer(0x61));
    const cubey::render::RenderGraphBufferHandle field = graph.create_buffer(buffer_desc("field"));

    graph.add_pass("inject", cubey::render::RenderGraphQueueDomain::Compute)
        .read_uniform_buffer(constants)
        .write_storage_buffer(field);
    graph.add_pass("advect", cubey::render::RenderGraphQueueDomain::Compute)
        .read_storage_buffer(field);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.buffers().size() == 2, "graph should preserve buffer resources");
    require(compiled.buffer(constants).lifetime ==
                cubey::render::RenderGraphResourceLifetime::Imported,
            "imported buffer should preserve imported lifetime");
    require(compiled.buffer(constants).imported_buffer == buffer(0x61),
            "imported buffer should preserve Vulkan buffer handle");
    require(compiled.buffer(field).lifetime ==
                cubey::render::RenderGraphResourceLifetime::Transient,
            "created buffer should be transient");
    require(compiled.passes()[0].queue_domain == cubey::render::RenderGraphQueueDomain::Compute,
            "compute pass should preserve queue domain");
    require(compiled.passes()[1].buffer_accesses[0].usage ==
                cubey::render::RenderGraphBufferUsage::StorageRead,
            "storage buffer read usage should be preserved");
}

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

void test_render_graph_preserves_material_pass_metadata() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle target =
        graph.import_texture(color_texture_desc("target"), image(0x401), view(0x402));
    cubey::render::MaterialPassInfo pass_info{
        .label = "test.forward",
        .kind = cubey::render::MaterialPassKind::ForwardColor,
        .depth_test = true,
        .depth_write = true,
    };

    graph.add_pass("forward", cubey::render::RenderGraphQueueDomain::Graphics)
        .write_color(target)
        .material_pass(pass_info);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].material_pass.has_value(),
            "graph pass should preserve material pass metadata");
    require(compiled.passes()[0].material_pass->label == "test.forward",
            "material pass metadata should preserve label");
    require(compiled.passes()[0].material_pass->depth_test,
            "material pass metadata should preserve depth state");
}

void test_render_graph_barrier_recording_rejects_unallocated_transient_resources() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphBufferHandle transient =
        graph.create_buffer(buffer_desc("transient field"));

    graph.add_pass("simulate", cubey::render::RenderGraphQueueDomain::Compute)
        .read_write_storage_buffer(transient)
        .execute([](const cubey::render::RenderGraphExecutionContext&) {});

    bool rejected_transient_barrier = false;
    graph.add_pass("render", cubey::render::RenderGraphQueueDomain::Graphics)
        .read_storage_buffer(transient)
        .execute([&](const cubey::render::RenderGraphExecutionContext& context) {
            const cubey::vulkan::CommandRecorder recorder(reinterpret_cast<VkCommandBuffer>(0x501));
            require_throws(
                [&] {
                    cubey::render::record_render_graph_barriers(
                        recorder, context, cubey::render::RenderGraphBarrierPhase::BeforePass);
                },
                "recording barriers should reject transient resources without allocations");
            rejected_transient_barrier = true;
        });

    const cubey::render::CompiledRenderGraph compiled = graph.compile();
    compiled.execute();

    require(rejected_transient_barrier, "transient barrier recording test should run");
}

void test_render_graph_transfer_pass_accepts_only_transfer_usages() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::RenderGraphTextureHandle source =
        graph.import_texture(color_texture_desc("source"), image(0x41), view(0x42));
    const cubey::render::RenderGraphBufferHandle readback =
        graph.create_buffer(buffer_desc("readback"));
    graph.add_pass("copy", cubey::render::RenderGraphQueueDomain::Transfer)
        .transfer_read_texture(source)
        .transfer_write_buffer(readback);

    const cubey::render::CompiledRenderGraph compiled = graph.compile();

    require(compiled.passes()[0].texture_accesses[0].usage ==
                cubey::render::RenderGraphTextureUsage::TransferRead,
            "transfer pass should preserve transfer texture usage");
    require(compiled.passes()[0].buffer_accesses[0].usage ==
                cubey::render::RenderGraphBufferUsage::TransferWrite,
            "transfer pass should preserve transfer buffer usage");

    require_throws(
        [] {
            cubey::render::RenderGraphBuilder invalid_graph;
            const cubey::render::RenderGraphTextureHandle invalid_source =
                invalid_graph.import_texture(color_texture_desc("source"), image(0x51), view(0x52));
            invalid_graph.add_pass("invalid copy", cubey::render::RenderGraphQueueDomain::Transfer)
                .read_texture(invalid_source);
        },
        "transfer passes should reject non-transfer texture usages");
}
