#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <stdexcept>

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

} // namespace

void test_render_graph_imports_color_and_depth_targets() {
    cubey::render::RenderGraphBuilder graph;
    const cubey::render::ColorTargetView color = cubey::render::color_target_view(
        {640, 360}, VK_FORMAT_R8G8B8A8_UNORM, image(0x01), view(0x02));
    const cubey::render::DepthTargetView depth = cubey::render::depth_target_view(
        {640, 360}, VK_FORMAT_D32_SFLOAT, image(0x03), view(0x04));

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
    const cubey::render::RenderGraphBufferHandle field =
        graph.create_buffer(buffer_desc("field"));

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
    require(compiled.buffer(field).lifetime == cubey::render::RenderGraphResourceLifetime::Transient,
            "created buffer should be transient");
    require(compiled.passes()[0].queue_domain == cubey::render::RenderGraphQueueDomain::Compute,
            "compute pass should preserve queue domain");
    require(compiled.passes()[1].buffer_accesses[0].usage ==
                cubey::render::RenderGraphBufferUsage::StorageRead,
            "storage buffer read usage should be preserved");
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
                invalid_graph.import_texture(color_texture_desc("source"), image(0x51),
                                             view(0x52));
            invalid_graph
                .add_pass("invalid copy", cubey::render::RenderGraphQueueDomain::Transfer)
                .read_texture(invalid_source);
        },
        "transfer passes should reject non-transfer texture usages");
}
