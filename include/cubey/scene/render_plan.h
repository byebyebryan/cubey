#pragma once

#include <cubey/core/math.h>
#include <cubey/render/render_item.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_registry.h>
#include <cubey/scene/entity.h>
#include <cubey/scene/renderable_manager.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

namespace cubey::scene {

struct RenderDrawPacket3D {
    Entity entity{};
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    render::MaterialInfo material_info{};
    math::Mat4 world_affine_matrix{1.0F};
    Bounds3D local_bounds{};
    Bounds3D world_bounds{};
    bool cast_shadows = true;
    bool receive_shadows = true;
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

namespace detail {

template <typename EnumT> [[nodiscard]] constexpr auto enum_sort_key(EnumT value) noexcept {
    return static_cast<std::underlying_type_t<EnumT>>(value);
}

[[nodiscard]] inline auto draw_packet_sort_key(const RenderDrawPacket3D& packet) {
    return std::tuple{
        enum_sort_key(packet.material_info.domain),
        enum_sort_key(packet.material_info.alpha_mode),
        packet.material_info.sort_key,
        packet.material.index,
        packet.material.generation,
        packet.mesh.index,
        packet.mesh.generation,
        packet.entity.index,
        packet.entity.generation,
    };
}

[[nodiscard]] inline float transparent_sort_depth(const RenderDrawPacket3D& packet,
                                                  const math::Mat4& view_matrix) {
    return (view_matrix * math::Vec4{packet.world_bounds.center, 1.0F}).z;
}

[[nodiscard]] inline bool draw_packet_sort_less_for_view(const RenderDrawPacket3D& lhs,
                                                         const RenderDrawPacket3D& rhs,
                                                         const math::Mat4& view_matrix) {
    if (lhs.material_info.alpha_mode == render::MaterialAlphaMode::Blend &&
        rhs.material_info.alpha_mode == render::MaterialAlphaMode::Blend) {
        const float lhs_depth = transparent_sort_depth(lhs, view_matrix);
        const float rhs_depth = transparent_sort_depth(rhs, view_matrix);
        if (lhs_depth != rhs_depth) {
            return lhs_depth < rhs_depth;
        }
    }
    return draw_packet_sort_key(lhs) < draw_packet_sort_key(rhs);
}

} // namespace detail

[[nodiscard]] inline render::RenderItem render_item_from_packet(const RenderDrawPacket3D& packet) {
    return render::RenderItem{
        .mesh = packet.mesh,
        .material = packet.material,
        .instance_count = packet.instance_count,
        .first_index = packet.first_index,
        .vertex_offset = packet.vertex_offset,
        .first_instance = packet.first_instance,
    };
}

[[nodiscard]] inline std::vector<RenderDrawPacket3D>
build_render_draw_packets_3d_unsorted(std::span<const RenderablePacket3D> packets,
                                      const render::RenderResourceRegistry& resources) {
    std::vector<RenderDrawPacket3D> result;
    result.reserve(packets.size());

    for (const RenderablePacket3D& packet : packets) {
        static_cast<void>(resources.mesh_info(packet.mesh));
        const render::MaterialInfo material_info = resources.material_info(packet.material);
        result.push_back(RenderDrawPacket3D{
            .entity = packet.entity,
            .mesh = packet.mesh,
            .material = packet.material,
            .material_info = material_info,
            .world_affine_matrix = packet.world_affine_matrix,
            .local_bounds = packet.local_bounds,
            .world_bounds = packet.world_bounds,
            .cast_shadows = packet.cast_shadows,
            .receive_shadows = packet.receive_shadows,
            .instance_count = packet.instance_count,
            .first_index = packet.first_index,
            .vertex_offset = packet.vertex_offset,
            .first_instance = packet.first_instance,
        });
    }

    return result;
}

[[nodiscard]] inline std::vector<RenderDrawPacket3D>
build_render_draw_packets_3d(std::span<const RenderablePacket3D> packets,
                             const render::RenderResourceRegistry& resources) {
    std::vector<RenderDrawPacket3D> result =
        build_render_draw_packets_3d_unsorted(packets, resources);
    std::stable_sort(result.begin(), result.end(),
                     [](const RenderDrawPacket3D& lhs, const RenderDrawPacket3D& rhs) {
                         return detail::draw_packet_sort_key(lhs) <
                                detail::draw_packet_sort_key(rhs);
                     });
    return result;
}

[[nodiscard]] inline std::vector<RenderDrawPacket3D>
build_render_draw_packets_3d(std::span<const RenderablePacket3D> packets,
                             const render::RenderResourceRegistry& resources,
                             const math::Mat4& view_matrix) {
    std::vector<RenderDrawPacket3D> result =
        build_render_draw_packets_3d_unsorted(packets, resources);
    std::stable_sort(result.begin(), result.end(),
                     [&view_matrix](const RenderDrawPacket3D& lhs, const RenderDrawPacket3D& rhs) {
                         return detail::draw_packet_sort_less_for_view(lhs, rhs, view_matrix);
                     });
    return result;
}

} // namespace cubey::scene
