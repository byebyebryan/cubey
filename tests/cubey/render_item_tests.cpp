#include <cubey/render/render_item.h>

#include <stdexcept>

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
