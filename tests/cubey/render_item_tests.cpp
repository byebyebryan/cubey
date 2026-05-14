#include <cubey/render/render_item.h>

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
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

void require_throws_with(auto&& action, const std::string& expected, const char* message) {
    try {
        action();
    } catch (const std::exception& error) {
        if (std::string(error.what()).find(expected) != std::string::npos) {
            return;
        }
        throw;
    }
    throw std::runtime_error(message);
}

[[nodiscard]] const cubey::render::Mesh* mesh_ptr(std::uintptr_t value) {
    return reinterpret_cast<const cubey::render::Mesh*>(value);
}

} // namespace

void test_render_item_validates_required_draw_identity() {
    const cubey::render::RenderItem valid{
        .mesh = cubey::render::MeshHandle{.index = 1, .generation = 2},
        .material = cubey::render::MaterialHandle{.index = 3, .generation = 4},
    };
    cubey::render::validate_render_item(valid);

    require_throws(
        [] {
            cubey::render::validate_render_item(cubey::render::RenderItem{
                .material = cubey::render::MaterialHandle{.index = 1, .generation = 1},
            });
        },
        "render items should require a mesh handle");
    require_throws(
        [] {
            cubey::render::validate_render_item(cubey::render::RenderItem{
                .mesh = cubey::render::MeshHandle{.index = 1, .generation = 1},
            });
        },
        "render items should require a material handle");
    require_throws(
        [] {
            cubey::render::validate_render_item(cubey::render::RenderItem{
                .mesh = cubey::render::MeshHandle{.index = 1, .generation = 1},
                .material = cubey::render::MaterialHandle{.index = 2, .generation = 1},
                .instance_count = 0,
            });
        },
        "render items should require at least one instance");
}

void test_render_item_resolves_draw_item_fields() {
    const cubey::render::RenderItem item{
        .mesh = cubey::render::MeshHandle{.index = 1, .generation = 2},
        .material = cubey::render::MaterialHandle{.index = 3, .generation = 4},
        .instance_count = 5,
        .first_index = 9,
        .vertex_offset = -2,
        .first_instance = 7,
    };
    const cubey::render::Mesh* mesh = mesh_ptr(0x100);

    const cubey::render::DrawItem draw_item = cubey::render::resolve_draw_item(item, mesh);

    require(draw_item.mesh == mesh, "draw item should preserve resolved mesh pointer");
    require(draw_item.instance_count == 5, "draw item should preserve instance count");
    require(draw_item.first_index == 9, "draw item should preserve first index");
    require(draw_item.vertex_offset == -2, "draw item should preserve vertex offset");
    require(draw_item.first_instance == 7, "draw item should preserve first instance");
    require_throws([&item] { (void)cubey::render::resolve_draw_item(item, nullptr); },
                   "draw item resolution should reject null meshes");
}

void test_frame_mesh_table_resolves_per_frame_override() {
    const cubey::render::MeshHandle handle{.index = 7, .generation = 2};
    const cubey::render::Mesh* frame_zero_mesh = mesh_ptr(0x100);
    const cubey::render::Mesh* frame_one_mesh = mesh_ptr(0x200);
    cubey::render::FrameMeshResourceTable frame_meshes;
    frame_meshes.resize(2);
    frame_meshes.bind(cubey::render::FrameSlot{.index = 0, .count = 2}, handle, frame_zero_mesh);
    frame_meshes.bind(cubey::render::FrameSlot{.index = 1, .count = 2}, handle, frame_one_mesh);

    const cubey::render::MeshResolver resolver{
        .frame_meshes = &frame_meshes,
        .frame_slot = cubey::render::FrameSlot{.index = 1, .count = 2},
    };

    require(cubey::render::resolve_mesh(resolver, handle) == frame_one_mesh,
            "mesh resolver should prefer the current frame override");
    require_throws_with(
        [&] {
            (void)frame_meshes.find(cubey::render::FrameSlot{.index = 2, .count = 2}, handle);
        },
        "frame slot index", "frame mesh table should validate frame slots");
}

void test_render_item_resolves_draw_item_from_frame_mesh_override() {
    const cubey::render::RenderItem item{
        .mesh = cubey::render::MeshHandle{.index = 4, .generation = 1},
        .material = cubey::render::MaterialHandle{.index = 3, .generation = 4},
        .instance_count = 2,
    };
    const cubey::render::Mesh* mesh = mesh_ptr(0x300);
    cubey::render::FrameMeshResourceTable frame_meshes;
    frame_meshes.resize(1);
    frame_meshes.bind(cubey::render::single_frame_slot(), item.mesh, mesh);

    const cubey::render::DrawItem draw_item = cubey::render::resolve_draw_item(
        item, cubey::render::MeshResolver{
                  .frame_meshes = &frame_meshes,
                  .frame_slot = cubey::render::single_frame_slot(),
              });

    require(draw_item.mesh == mesh, "draw item should use the frame override mesh");
    require(draw_item.instance_count == 2, "draw item should preserve packet instance count");
}
