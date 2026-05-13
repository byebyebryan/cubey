#pragma once

#include <cubey/render/material.h>
#include <cubey/render/material_instance.h>
#include <cubey/render/mesh.h>
#include <cubey/render/pipeline_resource.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_table.h>
#include <cubey/scene/render_plan.h>
#include <cubey/vulkan/command_recorder.h>

#include <optional>
#include <span>
#include <stdexcept>

namespace cubey::scene {

struct RenderPacketFilter3D {
    std::optional<render::MaterialPassKind> material_pass{};
    std::optional<render::MaterialBlendMode> blend_mode{};
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
    if (filter.blend_mode.has_value() && packet.material_info.blend != filter.blend_mode.value()) {
        return false;
    }
    return true;
}

struct PipelineDrawPackets3DInfo {
    const render::GraphicsPipelineResource* pipeline = nullptr;
    const render::MaterialInstance* material = nullptr;
    const render::MaterialResourceTable<render::MaterialInstance>* material_instances = nullptr;
    std::optional<render::FrameSlot> frame_slot{};
    RenderPacketFilter3D filter{};
};

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

template <typename RecordPacketCallback>
void record_pipeline_draw_packets_3d(const cubey::vulkan::CommandRecorder& recorder,
                                     std::span<const RenderDrawPacket3D> packets,
                                     const render::MeshResourceTable<render::Mesh>& meshes,
                                     const PipelineDrawPackets3DInfo& info,
                                     RecordPacketCallback&& record_packet) {
    if (info.pipeline == nullptr) {
        throw std::runtime_error("3D pipeline draw packet recording requires a pipeline");
    }
    if (info.material != nullptr && info.material_instances != nullptr) {
        throw std::runtime_error("3D pipeline draw packet recording has ambiguous material source");
    }
    recorder.bind_pipeline(VK_PIPELINE_BIND_POINT_GRAPHICS, info.pipeline->pipeline());
    if (info.material != nullptr) {
        if (info.frame_slot.has_value()) {
            render::bind_material_instance(recorder, *info.pipeline, *info.material,
                                           info.frame_slot.value());
        } else {
            render::bind_material_instance(recorder, *info.pipeline, *info.material);
        }
    }
    record_draw_packets_3d(
        recorder, packets, meshes, info.filter,
        [&](const cubey::vulkan::CommandRecorder& packet_recorder,
            const RenderDrawPacket3D& packet) {
            if (info.material_instances != nullptr) {
                const render::MaterialInstance& packet_material =
                    info.material_instances->at(packet.material);
                if (info.frame_slot.has_value()) {
                    render::bind_material_instance(packet_recorder, *info.pipeline, packet_material,
                                                   info.frame_slot.value());
                } else {
                    render::bind_material_instance(packet_recorder, *info.pipeline,
                                                   packet_material);
                }
            }
            record_packet(packet_recorder, packet);
        });
}

} // namespace cubey::scene
