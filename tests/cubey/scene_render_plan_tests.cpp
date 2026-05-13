#include <cubey/scene/render_plan.h>
#include <cubey/scene/render_recording.h>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
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

} // namespace

void test_render_plan_builds_sorted_3d_draw_packets_with_material_metadata() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle first_mesh = registry.create_mesh("first mesh");
    const cubey::render::MeshHandle second_mesh = registry.create_mesh("second mesh");
    const cubey::render::MaterialHandle late_opaque =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "late opaque",
            .blend = cubey::render::MaterialBlendMode::Opaque,
            .sort_key = 20,
        });
    const cubey::render::MaterialHandle early_opaque =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "early opaque",
            .blend = cubey::render::MaterialBlendMode::Opaque,
            .sort_key = 10,
        });
    const cubey::render::MaterialHandle transparent =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "transparent",
            .blend = cubey::render::MaterialBlendMode::AlphaBlend,
            .sort_key = 0,
            .pass_mask =
                cubey::render::material_pass_mask(cubey::render::MaterialPassKind::ForwardColor),
        });

    std::vector<cubey::RenderablePacket3D> packets{
        cubey::RenderablePacket3D{
            .entity = cubey::Entity{.index = 3, .generation = 1},
            .mesh = first_mesh,
            .material = transparent,
            .local_bounds = cubey::Bounds3D{.half_extent = {3.0F, 4.0F, 5.0F}},
            .instance_count = 5,
            .first_index = 9,
            .vertex_offset = -2,
            .first_instance = 4,
        },
        cubey::RenderablePacket3D{
            .entity = cubey::Entity{.index = 2, .generation = 1},
            .mesh = second_mesh,
            .material = early_opaque,
        },
        cubey::RenderablePacket3D{
            .entity = cubey::Entity{.index = 1, .generation = 1},
            .mesh = first_mesh,
            .material = late_opaque,
        },
    };

    const std::vector<cubey::scene::RenderDrawPacket3D> draw_packets =
        cubey::scene::build_render_draw_packets_3d(packets, registry);

    require(draw_packets.size() == 3, "all renderable packets should become draw packets");
    require(draw_packets[0].material == early_opaque,
            "opaque material with lower sort key should sort first");
    require(draw_packets[1].material == late_opaque,
            "opaque material with higher sort key should sort second");
    require(draw_packets[2].material == transparent,
            "transparent material should sort after opaque materials");
    require(draw_packets[0].material_info.label == "early opaque",
            "draw packets should carry material metadata");
    require(!cubey::render::material_supports_pass(draw_packets[2].material_info,
                                                   cubey::render::MaterialPassKind::DepthOnly),
            "draw packets should carry material pass masks");
    require(cubey::render::material_supports_pass(draw_packets[2].material_info,
                                                  cubey::render::MaterialPassKind::ForwardColor),
            "draw packets should preserve included material passes");
    require(draw_packets[2].instance_count == 5, "draw packet should preserve instance count");
    require(draw_packets[2].first_index == 9, "draw packet should preserve first index");
    require(draw_packets[2].vertex_offset == -2, "draw packet should preserve vertex offset");
    require(draw_packets[2].first_instance == 4, "draw packet should preserve first instance");
    require_close(draw_packets[2].local_bounds.half_extent.z, 5.0F,
                  "draw packet should preserve bounds");
}

void test_render_plan_rejects_stale_resource_handles() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("mesh");
    const cubey::render::MaterialHandle material = registry.create_material("material");

    const std::vector<cubey::RenderablePacket3D> packets{
        cubey::RenderablePacket3D{
            .entity = cubey::Entity{.index = 1, .generation = 1},
            .mesh = mesh,
            .material = material,
        },
    };

    registry.destroy_mesh(mesh);
    require_throws(
        [&packets, &registry] {
            (void)cubey::scene::build_render_draw_packets_3d(packets, registry);
        },
        "draw planning should reject stale mesh handles");

    const cubey::render::MeshHandle live_mesh = registry.create_mesh("live mesh");
    registry.destroy_material(material);
    const std::vector<cubey::RenderablePacket3D> stale_material_packets{
        cubey::RenderablePacket3D{
            .entity = cubey::Entity{.index = 2, .generation = 1},
            .mesh = live_mesh,
            .material = material,
        },
    };
    require_throws(
        [&stale_material_packets, &registry] {
            (void)cubey::scene::build_render_draw_packets_3d(stale_material_packets, registry);
        },
        "draw planning should reject stale material handles");
}

void test_render_plan_converts_draw_packets_to_render_items() {
    const cubey::scene::RenderDrawPacket3D packet{
        .entity = cubey::Entity{.index = 7, .generation = 1},
        .mesh = cubey::render::MeshHandle{.index = 2, .generation = 3},
        .material = cubey::render::MaterialHandle{.index = 4, .generation = 5},
        .instance_count = 6,
        .first_index = 8,
        .vertex_offset = -3,
        .first_instance = 10,
    };

    const cubey::render::RenderItem item = cubey::scene::render_item_from_packet(packet);

    require(item.mesh == packet.mesh, "render item should preserve mesh handle");
    require(item.material == packet.material, "render item should preserve material handle");
    require(item.instance_count == 6, "render item should preserve instance count");
    require(item.first_index == 8, "render item should preserve first index");
    require(item.vertex_offset == -3, "render item should preserve vertex offset");
    require(item.first_instance == 10, "render item should preserve first instance");
}

void test_render_plan_filters_draw_packets_for_recording_policy() {
    const cubey::scene::RenderDrawPacket3D depth_caster{
        .material_info =
            cubey::render::MaterialInfo{
                .label = "depth caster",
            },
        .cast_shadows = true,
    };
    const cubey::scene::RenderDrawPacket3D forward_only_non_caster{
        .material_info =
            cubey::render::MaterialInfo{
                .label = "forward only",
                .pass_mask = cubey::render::material_pass_mask(
                    cubey::render::MaterialPassKind::ForwardColor),
            },
        .cast_shadows = false,
    };
    const cubey::scene::RenderDrawPacket3D alpha_blend_packet{
        .material_info =
            cubey::render::MaterialInfo{
                .label = "alpha",
                .blend = cubey::render::MaterialBlendMode::AlphaBlend,
            },
    };

    require(cubey::scene::render_packet_matches_filter(
                depth_caster,
                {
                    .material_pass = cubey::render::MaterialPassKind::DepthOnly,
                    .require_shadow_caster = true,
                }),
            "recording filter should accept shadow-casting depth packets");
    require(!cubey::scene::render_packet_matches_filter(
                forward_only_non_caster,
                {
                    .material_pass = cubey::render::MaterialPassKind::DepthOnly,
                    .require_shadow_caster = true,
                }),
            "recording filter should reject non-casting packets without the requested pass");
    require(cubey::scene::render_packet_matches_filter(
                forward_only_non_caster,
                {.material_pass = cubey::render::MaterialPassKind::ForwardColor}),
            "recording filter should accept packets with the requested forward pass");
    require(cubey::scene::render_packet_matches_filter(
                alpha_blend_packet,
                {.blend_mode = cubey::render::MaterialBlendMode::AlphaBlend}),
            "recording filter should accept packets with the requested blend mode");
    require(!cubey::scene::render_packet_matches_filter(
                alpha_blend_packet,
                {.blend_mode = cubey::render::MaterialBlendMode::Opaque}),
            "recording filter should reject packets with a different blend mode");
}

void test_render_recording_rejects_ambiguous_material_binding_sources() {
    const cubey::vulkan::CommandRecorder recorder(reinterpret_cast<VkCommandBuffer>(0x01));
    const cubey::render::MeshResourceTable<cubey::render::Mesh> meshes;
    const std::vector<cubey::scene::RenderDrawPacket3D> packets;

    require_throws(
        [&] {
            cubey::scene::record_pipeline_draw_packets_3d(
                recorder, packets, meshes,
                {
                    .pipeline =
                        reinterpret_cast<const cubey::render::GraphicsPipelineResource*>(0x02),
                    .material = reinterpret_cast<const cubey::render::MaterialInstance*>(0x03),
                    .material_instances =
                        reinterpret_cast<const cubey::render::MaterialResourceTable<
                            cubey::render::MaterialInstance>*>(0x04),
                },
                [](const cubey::vulkan::CommandRecorder&, const cubey::scene::RenderDrawPacket3D&) {
                });
        },
        "recording should reject both fixed and per-packet material binding sources");
}
