#include <cubey/render/render_item.h>

#include <stdexcept>

namespace cubey::render {

void FrameMeshResourceTable::resize(std::uint32_t frame_slot_count) {
    if (frame_slot_count == 0) {
        throw std::runtime_error("frame mesh table requires at least one frame slot");
    }
    meshes_.clear();
    meshes_.resize(frame_slot_count);
}

void FrameMeshResourceTable::clear() {
    meshes_.clear();
}

std::uint32_t FrameMeshResourceTable::frame_slot_count() const {
    return static_cast<std::uint32_t>(meshes_.size());
}

void FrameMeshResourceTable::bind(FrameSlot frame_slot, MeshHandle handle, const Mesh* mesh) {
    validate_table_slot(frame_slot);
    if (!handle) {
        throw std::runtime_error("frame mesh table bind requires a mesh handle");
    }
    if (mesh == nullptr) {
        throw std::runtime_error("frame mesh table bind requires a mesh");
    }
    meshes_.at(frame_slot.index).insert_or_assign(handle, mesh);
}

const Mesh* FrameMeshResourceTable::find(FrameSlot frame_slot, MeshHandle handle) const {
    validate_table_slot(frame_slot);
    if (!handle) {
        throw std::runtime_error("frame mesh table lookup requires a mesh handle");
    }
    const auto& frame_meshes = meshes_.at(frame_slot.index);
    const auto position = frame_meshes.find(handle);
    if (position == frame_meshes.end()) {
        return nullptr;
    }
    return position->second;
}

void FrameMeshResourceTable::validate_table_slot(FrameSlot frame_slot) const {
    validate_frame_slot(frame_slot);
    if (meshes_.empty()) {
        throw std::runtime_error("frame mesh table has no frame slots");
    }
    if (frame_slot.count != meshes_.size()) {
        throw std::runtime_error("frame mesh table slot count does not match table");
    }
}

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

const Mesh* resolve_mesh(const MeshResolver& resolver, MeshHandle handle) {
    if (!handle) {
        throw std::runtime_error("mesh resolver requires a mesh handle");
    }
    if (resolver.frame_meshes != nullptr) {
        if (const Mesh* mesh = resolver.frame_meshes->find(resolver.frame_slot, handle);
            mesh != nullptr) {
            return mesh;
        }
    }
    if (resolver.meshes == nullptr) {
        throw std::runtime_error("mesh resolver requires a static mesh table");
    }
    return &resolver.meshes->at(handle);
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

DrawItem resolve_draw_item(const RenderItem& item, const MeshResolver& resolver) {
    validate_render_item(item);
    return resolve_draw_item(item, resolve_mesh(resolver, item.mesh));
}

} // namespace cubey::render
