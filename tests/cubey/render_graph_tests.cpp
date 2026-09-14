#include "render_graph_test_helpers.h"

#include <cubey/render/render_graph.h>

#include <vulkan/vulkan.h>

#include <array>
#include <stdexcept>

using namespace cubey::tests::render_graph;

namespace {

using cubey::render::RenderGraphBufferHandle;
using cubey::render::RenderGraphBufferUsage;
using cubey::render::RenderGraphPassBuilder;
using cubey::render::RenderGraphQueueDomain;
using cubey::render::RenderGraphTextureHandle;
using cubey::render::RenderGraphTextureUsage;

struct TextureUsageCase {
    RenderGraphTextureUsage usage{};
    RenderGraphQueueDomain domain{};
    RenderGraphQueueDomain alternate_domain{};
    bool has_alternate_domain = false;
    VkImageAspectFlags aspects = 0;
    VkImageAspectFlags invalid_aspects = 0;
    VkImageUsageFlags usage_flags = 0;
    bool requires_prior_write = false;
    bool shader_access = false;
};

struct BufferUsageCase {
    RenderGraphBufferUsage usage{};
    RenderGraphQueueDomain domain{};
    RenderGraphQueueDomain alternate_domain{};
    bool has_alternate_domain = false;
    VkBufferUsageFlags usage_flags = 0;
    bool requires_prior_write = false;
    bool shader_access = false;
};

constexpr std::array k_texture_usage_cases{
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::SampledRead,
        .domain = RenderGraphQueueDomain::Graphics,
        .alternate_domain = RenderGraphQueueDomain::Compute,
        .has_alternate_domain = true,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .usage_flags = VK_IMAGE_USAGE_SAMPLED_BIT,
        .requires_prior_write = true,
        .shader_access = true,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::StorageRead,
        .domain = RenderGraphQueueDomain::Compute,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .invalid_aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT,
        .requires_prior_write = true,
        .shader_access = true,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::StorageWrite,
        .domain = RenderGraphQueueDomain::Compute,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .invalid_aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT,
        .shader_access = true,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::StorageReadWrite,
        .domain = RenderGraphQueueDomain::Compute,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .invalid_aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .usage_flags = VK_IMAGE_USAGE_STORAGE_BIT,
        .shader_access = true,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::ColorAttachment,
        .domain = RenderGraphQueueDomain::Graphics,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .invalid_aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .usage_flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::ColorAttachmentReadWrite,
        .domain = RenderGraphQueueDomain::Graphics,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .invalid_aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .usage_flags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::DepthAttachment,
        .domain = RenderGraphQueueDomain::Graphics,
        .aspects = VK_IMAGE_ASPECT_DEPTH_BIT,
        .invalid_aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .usage_flags = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::TransferRead,
        .domain = RenderGraphQueueDomain::Transfer,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .usage_flags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        .requires_prior_write = true,
    },
    TextureUsageCase{
        .usage = RenderGraphTextureUsage::TransferWrite,
        .domain = RenderGraphQueueDomain::Transfer,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .aspects = VK_IMAGE_ASPECT_COLOR_BIT,
        .usage_flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    },
};

constexpr std::array k_buffer_usage_cases{
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::UniformRead,
        .domain = RenderGraphQueueDomain::Graphics,
        .alternate_domain = RenderGraphQueueDomain::Compute,
        .has_alternate_domain = true,
        .usage_flags = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .requires_prior_write = true,
        .shader_access = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::StorageRead,
        .domain = RenderGraphQueueDomain::Compute,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .requires_prior_write = true,
        .shader_access = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::StorageWrite,
        .domain = RenderGraphQueueDomain::Compute,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .shader_access = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::StorageReadWrite,
        .domain = RenderGraphQueueDomain::Compute,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .usage_flags = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .shader_access = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::VertexRead,
        .domain = RenderGraphQueueDomain::Graphics,
        .usage_flags = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .requires_prior_write = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::IndexRead,
        .domain = RenderGraphQueueDomain::Graphics,
        .usage_flags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .requires_prior_write = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::TransferRead,
        .domain = RenderGraphQueueDomain::Transfer,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .requires_prior_write = true,
    },
    BufferUsageCase{
        .usage = RenderGraphBufferUsage::TransferWrite,
        .domain = RenderGraphQueueDomain::Transfer,
        .alternate_domain = RenderGraphQueueDomain::Graphics,
        .has_alternate_domain = true,
        .usage_flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    },
};

[[nodiscard]] cubey::render::RenderGraphTextureDesc texture_desc(VkImageAspectFlags aspects,
                                                                 const char* label) {
    return aspects == VK_IMAGE_ASPECT_DEPTH_BIT ? depth_texture_desc(label)
                                                : color_texture_desc(label);
}

void declare_texture_usage(RenderGraphPassBuilder& pass, RenderGraphTextureHandle handle,
                           RenderGraphTextureUsage usage, VkPipelineStageFlags stage_mask = 0) {
    switch (usage) {
    case RenderGraphTextureUsage::SampledRead:
        pass.read_texture(handle, stage_mask);
        return;
    case RenderGraphTextureUsage::StorageRead:
        pass.read_storage_texture(handle, stage_mask);
        return;
    case RenderGraphTextureUsage::StorageWrite:
        pass.write_storage_texture(handle, stage_mask);
        return;
    case RenderGraphTextureUsage::StorageReadWrite:
        pass.read_write_storage_texture(handle, stage_mask);
        return;
    case RenderGraphTextureUsage::ColorAttachment:
        pass.write_color(handle);
        return;
    case RenderGraphTextureUsage::ColorAttachmentReadWrite:
        pass.read_write_color(handle);
        return;
    case RenderGraphTextureUsage::DepthAttachment:
        pass.write_depth(handle);
        return;
    case RenderGraphTextureUsage::TransferRead:
        pass.transfer_read_texture(handle);
        return;
    case RenderGraphTextureUsage::TransferWrite:
        pass.transfer_write_texture(handle);
        return;
    }
    throw std::runtime_error("render graph texture usage is invalid");
}

void declare_buffer_usage(RenderGraphPassBuilder& pass, RenderGraphBufferHandle handle,
                          RenderGraphBufferUsage usage, VkPipelineStageFlags stage_mask = 0) {
    switch (usage) {
    case RenderGraphBufferUsage::UniformRead:
        pass.read_uniform_buffer(handle, stage_mask);
        return;
    case RenderGraphBufferUsage::StorageRead:
        pass.read_storage_buffer(handle, stage_mask);
        return;
    case RenderGraphBufferUsage::StorageWrite:
        pass.write_storage_buffer(handle, stage_mask);
        return;
    case RenderGraphBufferUsage::StorageReadWrite:
        pass.read_write_storage_buffer(handle, stage_mask);
        return;
    case RenderGraphBufferUsage::VertexRead:
        pass.read_vertex_buffer(handle);
        return;
    case RenderGraphBufferUsage::IndexRead:
        pass.read_index_buffer(handle);
        return;
    case RenderGraphBufferUsage::TransferRead:
        pass.transfer_read_buffer(handle);
        return;
    case RenderGraphBufferUsage::TransferWrite:
        pass.transfer_write_buffer(handle);
        return;
    }
    throw std::runtime_error("render graph buffer usage is invalid");
}

[[nodiscard]] VkPipelineStageFlags valid_shader_stage(RenderGraphQueueDomain domain) {
    return domain == RenderGraphQueueDomain::Compute ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                                     : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
}

[[nodiscard]] VkPipelineStageFlags invalid_shader_stage(RenderGraphQueueDomain domain) {
    return domain == RenderGraphQueueDomain::Compute ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
                                                     : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
}

} // namespace

void test_render_graph_texture_state_helpers_describe_common_frame_states() {
    const cubey::render::RenderGraphTextureState undefined =
        cubey::render::render_graph_undefined_texture_state();
    require(undefined.layout == VK_IMAGE_LAYOUT_UNDEFINED,
            "undefined helper should describe undefined layout");
    require(undefined.access_mask == 0, "undefined helper should not request access");
    require(undefined.stage_mask == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            "undefined helper should use top-of-pipe stage");

    const cubey::render::RenderGraphTextureState present =
        cubey::render::render_graph_present_texture_state();
    require(present.layout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            "present helper should describe swapchain present layout");
    require(present.access_mask == 0, "present helper should not request access");
    require(present.stage_mask == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            "present helper should use bottom-of-pipe stage");

    const cubey::render::RenderGraphTextureState color =
        cubey::render::render_graph_color_attachment_texture_state();
    require(color.layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            "color helper should describe color attachment layout");
    require(color.access_mask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            "color helper should request color attachment writes");
    require(color.stage_mask == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            "color helper should use color attachment output stage");

    const cubey::render::RenderGraphTextureState sampled_depth =
        cubey::render::render_graph_sampled_depth_texture_state();
    require(sampled_depth.layout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
            "sampled depth helper should describe sampled depth layout");
    require(sampled_depth.access_mask == VK_ACCESS_SHADER_READ_BIT,
            "sampled depth helper should request shader reads");
    require(sampled_depth.stage_mask == VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
            "sampled depth helper should cover graphics shader consumers");
}

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
                .extent = {1, 1, 1},
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
                .extent = {0, 1, 1},
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
                .extent = {1, 1, 1},
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

void test_render_graph_usage_traits_cover_every_usage_signature_and_dependencies() {
    const auto& texture_cases = k_texture_usage_cases;
    const auto& buffer_cases = k_buffer_usage_cases;
    for (const TextureUsageCase& usage_case : texture_cases) {
        {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphTextureHandle texture = graph.import_texture(
                texture_desc(usage_case.aspects, "imported texture"), image(0x601), view(0x602));
            RenderGraphPassBuilder pass = graph.add_pass("signature", usage_case.domain);
            declare_texture_usage(pass, texture, usage_case.usage);
            const cubey::render::CompiledRenderGraph compiled = graph.compile();
            require(compiled.resource_signature().textures[0].usage_flags == usage_case.usage_flags,
                    "texture usage should contribute its allocation flag to the signature");
        }
        {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphTextureHandle texture =
                graph.create_texture(texture_desc(usage_case.aspects, "transient texture"));
            RenderGraphPassBuilder pass = graph.add_pass("dependency", usage_case.domain);
            declare_texture_usage(pass, texture, usage_case.usage);
            if (usage_case.requires_prior_write) {
                require_throws([&graph] { (void)graph.compile(); },
                               "read-only texture usage should require a prior transient write");
            } else {
                (void)graph.compile();
            }
        }
    }

    for (const BufferUsageCase& usage_case : buffer_cases) {
        {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphBufferHandle buffer_handle =
                graph.import_buffer(buffer_desc("imported buffer"), buffer(0x603));
            RenderGraphPassBuilder pass = graph.add_pass("signature", usage_case.domain);
            declare_buffer_usage(pass, buffer_handle, usage_case.usage);
            const cubey::render::CompiledRenderGraph compiled = graph.compile();
            require(compiled.resource_signature().buffers[0].usage_flags == usage_case.usage_flags,
                    "buffer usage should contribute its allocation flag to the signature");
        }
        {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphBufferHandle buffer_handle =
                graph.create_buffer(buffer_desc("transient buffer"));
            RenderGraphPassBuilder pass = graph.add_pass("dependency", usage_case.domain);
            declare_buffer_usage(pass, buffer_handle, usage_case.usage);
            if (usage_case.requires_prior_write) {
                require_throws([&graph] { (void)graph.compile(); },
                               "read-only buffer usage should require a prior transient write");
            } else {
                (void)graph.compile();
            }
        }
    }
}

void test_render_graph_usage_traits_validate_domains_aspects_and_stages() {
    const auto& texture_cases = k_texture_usage_cases;
    const auto& buffer_cases = k_buffer_usage_cases;
    for (const TextureUsageCase& usage_case : texture_cases) {
        if (usage_case.usage != RenderGraphTextureUsage::TransferRead &&
            usage_case.usage != RenderGraphTextureUsage::TransferWrite) {
            require_throws(
                [&usage_case] {
                    cubey::render::RenderGraphBuilder graph;
                    const RenderGraphTextureHandle texture = graph.import_texture(
                        texture_desc(usage_case.aspects, "invalid domain texture"), image(0x610),
                        view(0x611));
                    const RenderGraphQueueDomain invalid_domain =
                        usage_case.shader_access ? RenderGraphQueueDomain::Transfer
                                                 : RenderGraphQueueDomain::Compute;
                    RenderGraphPassBuilder pass = graph.add_pass("invalid domain", invalid_domain);
                    declare_texture_usage(pass, texture, usage_case.usage);
                },
                "texture usage should reject incompatible queue domains");
        }
        if (usage_case.has_alternate_domain) {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphTextureHandle texture = graph.import_texture(
                texture_desc(usage_case.aspects, "alternate texture"), image(0x611), view(0x612));
            RenderGraphPassBuilder pass = graph.add_pass("alternate", usage_case.alternate_domain);
            declare_texture_usage(pass, texture, usage_case.usage);
            (void)graph.compile();
        }
        if (usage_case.invalid_aspects != 0) {
            require_throws(
                [&usage_case] {
                    cubey::render::RenderGraphBuilder graph;
                    const RenderGraphTextureHandle texture = graph.import_texture(
                        texture_desc(usage_case.invalid_aspects, "invalid texture"), image(0x613),
                        view(0x614));
                    RenderGraphPassBuilder pass =
                        graph.add_pass("invalid aspect", usage_case.domain);
                    declare_texture_usage(pass, texture, usage_case.usage);
                },
                "texture usage should reject incompatible aspects");
        }
        if (usage_case.shader_access) {
            {
                cubey::render::RenderGraphBuilder graph;
                const RenderGraphTextureHandle texture = graph.import_texture(
                    texture_desc(usage_case.aspects, "stage texture"), image(0x615), view(0x616));
                RenderGraphPassBuilder pass = graph.add_pass("stage", usage_case.domain);
                declare_texture_usage(pass, texture, usage_case.usage,
                                      valid_shader_stage(usage_case.domain));
                (void)graph.compile();
            }
            require_throws(
                [&usage_case] {
                    cubey::render::RenderGraphBuilder graph;
                    const RenderGraphTextureHandle texture = graph.import_texture(
                        texture_desc(usage_case.aspects, "invalid stage texture"), image(0x617),
                        view(0x618));
                    RenderGraphPassBuilder pass =
                        graph.add_pass("invalid stage", usage_case.domain);
                    declare_texture_usage(pass, texture, usage_case.usage,
                                          invalid_shader_stage(usage_case.domain));
                },
                "shader texture usage should reject incompatible explicit stages");
        }
    }

    for (const BufferUsageCase& usage_case : buffer_cases) {
        if (usage_case.usage != RenderGraphBufferUsage::TransferRead &&
            usage_case.usage != RenderGraphBufferUsage::TransferWrite) {
            require_throws(
                [&usage_case] {
                    cubey::render::RenderGraphBuilder graph;
                    const RenderGraphBufferHandle buffer_handle =
                        graph.import_buffer(buffer_desc("invalid domain buffer"), buffer(0x619));
                    const RenderGraphQueueDomain invalid_domain =
                        usage_case.shader_access ? RenderGraphQueueDomain::Transfer
                                                 : RenderGraphQueueDomain::Compute;
                    RenderGraphPassBuilder pass = graph.add_pass("invalid domain", invalid_domain);
                    declare_buffer_usage(pass, buffer_handle, usage_case.usage);
                },
                "buffer usage should reject incompatible queue domains");
        }
        if (usage_case.has_alternate_domain) {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphBufferHandle buffer_handle =
                graph.import_buffer(buffer_desc("alternate buffer"), buffer(0x619));
            RenderGraphPassBuilder pass = graph.add_pass("alternate", usage_case.alternate_domain);
            declare_buffer_usage(pass, buffer_handle, usage_case.usage);
            (void)graph.compile();
        }
        if (usage_case.shader_access) {
            {
                cubey::render::RenderGraphBuilder graph;
                const RenderGraphBufferHandle buffer_handle =
                    graph.import_buffer(buffer_desc("stage buffer"), buffer(0x61A));
                RenderGraphPassBuilder pass = graph.add_pass("stage", usage_case.domain);
                declare_buffer_usage(pass, buffer_handle, usage_case.usage,
                                     valid_shader_stage(usage_case.domain));
                (void)graph.compile();
            }
            require_throws(
                [&usage_case] {
                    cubey::render::RenderGraphBuilder graph;
                    const RenderGraphBufferHandle buffer_handle =
                        graph.import_buffer(buffer_desc("invalid stage buffer"), buffer(0x61B));
                    RenderGraphPassBuilder pass =
                        graph.add_pass("invalid stage", usage_case.domain);
                    declare_buffer_usage(pass, buffer_handle, usage_case.usage,
                                         invalid_shader_stage(usage_case.domain));
                },
                "shader buffer usage should reject incompatible explicit stages");
        }
    }

    for (const RenderGraphTextureUsage usage :
         {RenderGraphTextureUsage::SampledRead, RenderGraphTextureUsage::TransferRead,
          RenderGraphTextureUsage::TransferWrite}) {
        cubey::render::RenderGraphBuilder graph;
        const RenderGraphTextureHandle texture =
            graph.import_texture(depth_texture_desc("depth texture"), image(0x61C), view(0x61D));
        const RenderGraphQueueDomain domain = usage == RenderGraphTextureUsage::SampledRead
                                                  ? RenderGraphQueueDomain::Graphics
                                                  : RenderGraphQueueDomain::Transfer;
        RenderGraphPassBuilder pass = graph.add_pass("depth usage", domain);
        declare_texture_usage(pass, texture, usage);
        (void)graph.compile();
    }

    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphTextureHandle texture = graph.import_texture(
                color_texture_desc("invalid queue texture"), image(0x61E), view(0x61F));
            RenderGraphPassBuilder pass =
                graph.add_pass("invalid queue", static_cast<RenderGraphQueueDomain>(99));
            pass.write_color(texture);
        },
        "texture usage should reject invalid queue domains deterministically");
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            const RenderGraphBufferHandle buffer_handle =
                graph.import_buffer(buffer_desc("invalid queue buffer"), buffer(0x620));
            RenderGraphPassBuilder pass =
                graph.add_pass("invalid queue", static_cast<RenderGraphQueueDomain>(99));
            pass.write_storage_buffer(buffer_handle);
        },
        "buffer usage should reject invalid queue domains deterministically");

    const cubey::render::RenderGraphTextureResource texture{
        .handle = RenderGraphTextureHandle{.index = 1},
        .desc = color_texture_desc("invalid texture usage"),
    };
    const cubey::render::RenderGraphCompiledPass invalid_texture_pass{
        .texture_accesses =
            {
                cubey::render::RenderGraphTextureAccess{
                    .handle = texture.handle,
                    .usage = static_cast<RenderGraphTextureUsage>(99),
                },
            },
    };
    require_throws(
        [&texture, &invalid_texture_pass] {
            (void)cubey::render::CompiledRenderGraph({texture}, {}, {invalid_texture_pass});
        },
        "invalid texture usage values should fail while deriving resource signatures");

    const cubey::render::RenderGraphBufferResource buffer_resource{
        .handle = RenderGraphBufferHandle{.index = 1},
        .desc = buffer_desc("invalid buffer usage"),
    };
    const cubey::render::RenderGraphCompiledPass invalid_buffer_pass{
        .buffer_accesses =
            {
                cubey::render::RenderGraphBufferAccess{
                    .handle = buffer_resource.handle,
                    .usage = static_cast<RenderGraphBufferUsage>(99),
                },
            },
    };
    require_throws(
        [&buffer_resource, &invalid_buffer_pass] {
            (void)cubey::render::CompiledRenderGraph({}, {buffer_resource}, {invalid_buffer_pass});
        },
        "invalid buffer usage values should fail while deriving resource signatures");
}

void test_render_graph_rejects_shader_stage_masks_outside_pass_domain() {
    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            const cubey::render::RenderGraphBufferHandle constants =
                graph.import_buffer(buffer_desc("constants"), buffer(0x531));
            graph.add_pass("graphics", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_uniform_buffer(constants, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        },
        "graphics passes should reject compute shader stage masks");

    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            const cubey::render::RenderGraphBufferHandle field =
                graph.import_buffer(buffer_desc("field"), buffer(0x532));
            graph.add_pass("compute", cubey::render::RenderGraphQueueDomain::Compute)
                .read_storage_buffer(field, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        },
        "compute passes should reject graphics shader stage masks");

    require_throws(
        [] {
            cubey::render::RenderGraphBuilder graph;
            const cubey::render::RenderGraphTextureHandle texture =
                graph.import_texture(color_texture_desc("texture"), image(0x533), view(0x534));
            graph.add_pass("graphics", cubey::render::RenderGraphQueueDomain::Graphics)
                .read_texture(texture, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        },
        "graphics shader resource accesses should reject fixed-function stage masks");
}
