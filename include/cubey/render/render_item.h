#pragma once

#include <cubey/render/frame_data.h>
#include <cubey/render/mesh.h>
#include <cubey/render/resource_handle.h>
#include <cubey/render/resource_table.h>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cubey::render {

class FrameMeshResourceTable {
  public:
    void resize(std::uint32_t frame_slot_count);
    void clear();
    [[nodiscard]] std::uint32_t frame_slot_count() const;

    void bind(FrameSlot frame_slot, MeshHandle handle, const Mesh* mesh);
    [[nodiscard]] const Mesh* find(FrameSlot frame_slot, MeshHandle handle) const;

  private:
    void validate_table_slot(FrameSlot frame_slot) const;

    std::vector<std::unordered_map<MeshHandle, const Mesh*, MeshHandleHash>> meshes_{};
};

struct MeshResolver {
    const MeshResourceTable<Mesh>* meshes = nullptr;
    const FrameMeshResourceTable* frame_meshes = nullptr;
    FrameSlot frame_slot{};
};

struct RenderItem {
    MeshHandle mesh{};
    MaterialHandle material{};
    std::uint32_t instance_count = 1;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::uint32_t first_instance = 0;
};

void validate_render_item(const RenderItem& item);

[[nodiscard]] const Mesh* resolve_mesh(const MeshResolver& resolver, MeshHandle handle);
[[nodiscard]] DrawItem resolve_draw_item(const RenderItem& item, const Mesh* mesh);
[[nodiscard]] DrawItem resolve_draw_item(const RenderItem& item,
                                         const MeshResourceTable<Mesh>& meshes);
[[nodiscard]] DrawItem resolve_draw_item(const RenderItem& item, const MeshResolver& resolver);

} // namespace cubey::render
