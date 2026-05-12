#include <cubey/render/render_item.h>

#include <stdexcept>

namespace cubey::render {

void validate_render_item(const RenderItem& item) {
    if (!item.mesh) {
        throw std::runtime_error("render item requires a mesh handle");
    }
    if (!item.material) {
        throw std::runtime_error("render item requires a material handle");
    }
    if (item.instance_count == 0) {
        throw std::runtime_error("render item requires at least one instance");
    }
}

DrawItem resolve_draw_item(const RenderItem& item, const Mesh* mesh) {
    validate_render_item(item);
    if (mesh == nullptr) {
        throw std::runtime_error("render item draw resolution requires a mesh");
    }
    return DrawItem{
        .mesh = mesh,
        .instance_count = item.instance_count,
        .first_index = item.first_index,
        .vertex_offset = item.vertex_offset,
        .first_instance = item.first_instance,
    };
}

DrawItem resolve_draw_item(const RenderItem& item, const MeshResourceTable<Mesh>& meshes) {
    validate_render_item(item);
    return resolve_draw_item(item, &meshes.at(item.mesh));
}

} // namespace cubey::render
