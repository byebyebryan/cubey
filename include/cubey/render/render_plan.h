#pragma once

#include <cubey/entity.h>
#include <cubey/math.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_registry.h>
#include <cubey/renderable_manager.h>

#include <algorithm>
#include <cstdint>
#include <span>
#include <tuple>
#include <type_traits>
#include <vector>

namespace cubey::render {

struct RenderDrawPacket3D {
    Entity entity{};
    MeshHandle mesh{};
    MaterialHandle material{};
    MaterialInfo material_info{};
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
        enum_sort_key(packet.material_info.blend),
        packet.material_info.sort_key,
        packet.material.index,
        packet.material.generation,
        packet.mesh.index,
        packet.mesh.generation,
        packet.entity.index,
        packet.entity.generation,
    };
}

} // namespace detail

[[nodiscard]] inline std::vector<RenderDrawPacket3D>
build_render_draw_packets_3d(std::span<const RenderablePacket3D> packets,
                             const RenderResourceRegistry& resources) {
    std::vector<RenderDrawPacket3D> result;
    result.reserve(packets.size());

    for (const RenderablePacket3D& packet : packets) {
        static_cast<void>(resources.mesh_info(packet.mesh));
        const MaterialInfo material_info = resources.material_info(packet.material);
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

    std::stable_sort(result.begin(), result.end(),
                     [](const RenderDrawPacket3D& lhs, const RenderDrawPacket3D& rhs) {
                         return detail::draw_packet_sort_key(lhs) <
                                detail::draw_packet_sort_key(rhs);
                     });
    return result;
}

} // namespace cubey::render
