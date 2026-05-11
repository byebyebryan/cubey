#include <cubey/render/resource_table.h>

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

struct MoveOnlyResource {
    explicit MoveOnlyResource(int value_in) : value(value_in) {}

    MoveOnlyResource(const MoveOnlyResource&) = delete;
    MoveOnlyResource& operator=(const MoveOnlyResource&) = delete;
    MoveOnlyResource(MoveOnlyResource&&) noexcept = default;
    MoveOnlyResource& operator=(MoveOnlyResource&&) noexcept = default;

    int value = 0;
};

} // namespace

void test_render_resource_table_resolves_move_only_resources_by_handle() {
    cubey::render::MeshResourceTable<MoveOnlyResource> meshes;
    const cubey::render::MeshHandle mesh{.index = 1, .generation = 1};

    require(!meshes.contains(mesh), "empty resource table should not contain a handle");
    require_throws([&meshes, mesh] { (void)meshes.at(mesh); },
                   "missing resource lookup should throw");

    MoveOnlyResource& inserted = meshes.emplace(mesh, 17);

    require(inserted.value == 17, "emplace should construct move-only resource in place");
    require(meshes.contains(mesh), "resource table should contain inserted handle");
    require(meshes.at(mesh).value == 17, "resource table should resolve inserted resource");
    require_throws([&meshes, mesh] { (void)meshes.emplace(mesh, 29); },
                   "duplicate resource insert should throw");

    meshes.erase(mesh);

    require(!meshes.contains(mesh), "resource table should not contain erased handle");
    require_throws([&meshes, mesh] { (void)meshes.at(mesh); },
                   "erased resource lookup should throw");
    require_throws([&meshes, mesh] { meshes.erase(mesh); },
                   "erasing a missing resource should throw");
}
