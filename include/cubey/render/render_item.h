#pragma once

#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>

#include <cstdint>

namespace cubey::render {

struct RenderItem {
    MeshHandle mesh{};
    MaterialHandle material{};
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

void validate_render_item(const RenderItem& item);

[[nodiscard]] DrawItem resolve_draw_item(const RenderItem& item, const Mesh* mesh);
[[nodiscard]] DrawItem resolve_draw_item(const RenderItem& item,
                                         const MeshResourceTable<Mesh>& meshes);

} // namespace cubey::render
