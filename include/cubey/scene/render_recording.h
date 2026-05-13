#pragma once

#include <cubey/render/material.h>
#include <cubey/render/mesh.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/render_plan.h>
#include <cubey/vulkan/command_recorder.h>

#include <optional>
#include <span>

namespace cubey::scene {

struct RenderPacketFilter3D {
    std::optional<render::MaterialPassKind> material_pass{};
    bool require_shadow_caster = false;
};

[[nodiscard]] inline bool render_packet_matches_filter(const RenderDrawPacket3D& packet,
                                                       RenderPacketFilter3D filter) {
    if (filter.require_shadow_caster && !packet.cast_shadows) {
        return false;
    }
    if (filter.material_pass.has_value() &&
        !render::material_supports_pass(packet.material_info, filter.material_pass.value())) {
        return false;
    }
    return true;
}

template <typename RecordPacketCallback>
void record_draw_packets_3d(const cubey::vulkan::CommandRecorder& recorder,
                            std::span<const RenderDrawPacket3D> packets,
                            const render::MeshResourceTable<render::Mesh>& meshes,
                            RenderPacketFilter3D filter, RecordPacketCallback&& record_packet) {
    for (const RenderDrawPacket3D& packet : packets) {
        if (!render_packet_matches_filter(packet, filter)) {
            continue;
        }
        record_packet(recorder, packet);
        const render::DrawItem draw_item =
            render::resolve_draw_item(render_item_from_packet(packet), meshes);
        render::record_draw_item(recorder.handle(), draw_item);
    }
}

} // namespace cubey::scene
