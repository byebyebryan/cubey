#include <cubey/core/math.h>
#include <cubey/render/resource_handle.h>
#include <cubey/scene/renderable_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>

#include <cmath>
#include <functional>
#include <stdexcept>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    constexpr float kTolerance = 0.00001F;
    if (std::fabs(actual - expected) > kTolerance) {
        throw std::runtime_error(message);
    }
}

void require_throws(const std::function<void()>& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

cubey::render::MeshHandle mesh_handle(std::uint32_t index) {
    return cubey::render::MeshHandle{.index = index, .generation = 1};
}

cubey::render::MaterialHandle material_handle(std::uint32_t index) {
    return cubey::render::MaterialHandle{.index = index, .generation = 1};
}

cubey::Renderable3D renderable_for(cubey::render::MeshHandle mesh,
                                   cubey::render::MaterialHandle material) {
    return cubey::Renderable3D{
        .primitives =
            {
                cubey::RenderablePrimitive3D{
                    .mesh = mesh,
                    .material = material,
                },
            },
        .local_bounds =
            cubey::Bounds3D{
                .center = {0.0F, 0.0F, 0.0F},
                .half_extent = {1.0F, 1.0F, 1.0F},
            },
    };
}

} // namespace

void test_renderable_manager_publishes_3d_packets_from_scene_read_view() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{.translation = {1.0F, 2.0F, 3.0F}});
    setup.renderables3d().create(entity, renderable_for(mesh_handle(7), material_handle(11)));
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const cubey::RenderableInstance3D instance = view.renderables3d().instance(entity);
    const cubey::Renderable3D& renderable = view.renderables3d().renderable(instance);
    require(renderable.primitives.size() == 1, "read view should publish renderable primitives");
    require(renderable.primitives[0].mesh == mesh_handle(7),
            "read view should publish primitive mesh handle");
    require(renderable.primitives[0].material == material_handle(11),
            "read view should publish primitive material handle");

    const std::vector<cubey::RenderablePacket3D> packets =
        cubey::build_renderable_packets_3d(view.renderables3d(), view.transforms3d());
    require(packets.size() == 1, "visible renderable primitive should produce one packet");
    require(packets[0].entity == entity, "packet should carry renderable entity");
    require(packets[0].mesh == mesh_handle(7), "packet should carry mesh handle");
    require(packets[0].material == material_handle(11), "packet should carry material handle");
    require(packets[0].cast_shadows, "packet should carry cast shadow flag");
    require(packets[0].receive_shadows, "packet should carry receive shadow flag");
    require_close(packets[0].local_bounds.half_extent.x, 1.0F, "packet should carry local bounds");

    const cubey::math::Vec4 world =
        packets[0].world_affine_matrix * cubey::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    require_close(world.x, 1.0F, "packet should carry transform world matrix x");
    require_close(world.y, 2.0F, "packet should carry transform world matrix y");
    require_close(world.z, 3.0F, "packet should carry transform world matrix z");
}

void test_renderable_manager_emits_multiple_visible_primitives() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{});
    setup.renderables3d().create(entity, cubey::Renderable3D{
                                             .primitives =
                                                 {
                                                     cubey::RenderablePrimitive3D{
                                                         .mesh = mesh_handle(1),
                                                         .material = material_handle(2),
                                                         .instance_count = 3,
                                                         .first_index = 6,
                                                     },
                                                     cubey::RenderablePrimitive3D{
                                                         .mesh = mesh_handle(3),
                                                         .material = material_handle(4),
                                                         .vertex_offset = -2,
                                                         .first_instance = 5,
                                                     },
                                                 },
                                             .local_bounds =
                                                 cubey::Bounds3D{
                                                     .half_extent = {2.0F, 2.0F, 2.0F},
                                                 },
                                             .cast_shadows = false,
                                         });
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const std::vector<cubey::RenderablePacket3D> packets =
        cubey::build_renderable_packets_3d(view.renderables3d(), view.transforms3d());
    require(packets.size() == 2, "two visible primitives should produce two packets");
    require(packets[0].mesh == mesh_handle(1), "first packet should carry first primitive mesh");
    require(packets[0].material == material_handle(2),
            "first packet should carry first primitive material");
    require(packets[0].instance_count == 3, "first packet should carry primitive instance count");
    require(packets[0].first_index == 6, "first packet should carry primitive first index");
    require(!packets[0].cast_shadows, "packet should carry renderable shadow flags");
    require(packets[1].mesh == mesh_handle(3), "second packet should carry second primitive mesh");
    require(packets[1].vertex_offset == -2, "second packet should carry primitive vertex offset");
    require(packets[1].first_instance == 5, "second packet should carry primitive first instance");
}

void test_renderable_manager_skips_invisible_renderables_without_transform() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    cubey::Renderable3D renderable = renderable_for(mesh_handle(1), material_handle(1));
    renderable.visible = false;
    setup.renderables3d().create(entity, renderable);
    setup.commit();

    cubey::SceneReadView view = scene.read();
    const std::vector<cubey::RenderablePacket3D> packets =
        cubey::build_renderable_packets_3d(view.renderables3d(), view.transforms3d());
    require(packets.empty(), "invisible renderable should not require a transform or packet");
}

void test_renderable_manager_updates_keep_epoch_local_snapshots() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{});
    setup.renderables3d().create(entity, renderable_for(mesh_handle(1), material_handle(1)));
    setup.commit();

    cubey::SceneReadView before = scene.read();
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.renderables3d().set_renderable(entity,
                                         renderable_for(mesh_handle(5), material_handle(6)));
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    const cubey::RenderableInstance3D before_renderable = before.renderables3d().instance(entity);
    const cubey::RenderableInstance3D after_renderable = after.renderables3d().instance(entity);
    require(before.renderables3d().renderable(before_renderable).primitives[0].mesh ==
                mesh_handle(1),
            "older read view should keep previous renderable state");
    require(after.renderables3d().renderable(after_renderable).primitives[0].mesh == mesh_handle(5),
            "new read view should publish updated renderable state");
}

void test_renderable_manager_entity_destroy_retires_attached_components() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{});
    setup.renderables3d().create(entity, renderable_for(mesh_handle(1), material_handle(1)));
    setup.commit();

    cubey::SceneReadView before = scene.read();
    cubey::SceneEditQueue edits = scene.create_edit_queue();
    edits.destroy(entity);
    scene.commit(edits);
    cubey::SceneReadView after = scene.read();

    const std::vector<cubey::RenderablePacket3D> old_packets =
        cubey::build_renderable_packets_3d(before.renderables3d(), before.transforms3d());
    require(old_packets.size() == 1,
            "older read view should keep destroyed entity renderable snapshot");
    require_throws([&after, entity] { (void)after.renderables3d().instance(entity); },
                   "new read view should not expose renderable for destroyed entity");
}

void test_renderable_manager_rejects_invalid_edits_and_missing_transforms() {
    cubey::Scene scene;
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{});
    setup.renderables3d().create(entity, renderable_for(mesh_handle(1), material_handle(1)));
    setup.commit();

    cubey::SceneEditQueue duplicate = scene.create_edit_queue();
    duplicate.renderables3d().create(entity, renderable_for(mesh_handle(2), material_handle(2)));
    require_throws([&scene, &duplicate] { scene.commit(duplicate); },
                   "renderable manager should reject duplicate components");

    cubey::SceneTransaction stale_setup = scene.begin_transaction();
    const cubey::Entity stale_entity = stale_setup.entities().create();
    stale_setup.commit();
    cubey::SceneEditQueue stale_destroy = scene.create_edit_queue();
    stale_destroy.destroy(stale_entity);
    scene.commit(stale_destroy);
    cubey::SceneEditQueue stale_create = scene.create_edit_queue();
    stale_create.renderables3d().create(stale_entity,
                                        renderable_for(mesh_handle(3), material_handle(3)));
    require_throws([&scene, &stale_create] { scene.commit(stale_create); },
                   "renderable manager should reject stale entity handles");

    cubey::SceneTransaction invalid_setup = scene.begin_transaction();
    const cubey::Entity empty_entity = invalid_setup.entities().create();
    const cubey::Entity null_mesh_entity = invalid_setup.entities().create();
    const cubey::Entity null_material_entity = invalid_setup.entities().create();
    const cubey::Entity zero_instances_entity = invalid_setup.entities().create();
    invalid_setup.commit();

    cubey::SceneEditQueue invalid_empty = scene.create_edit_queue();
    invalid_empty.renderables3d().create(empty_entity, cubey::Renderable3D{});
    require_throws([&scene, &invalid_empty] { scene.commit(invalid_empty); },
                   "renderable manager should reject empty primitive lists");

    cubey::SceneEditQueue invalid_null_mesh = scene.create_edit_queue();
    invalid_null_mesh.renderables3d().create(
        null_mesh_entity, renderable_for(cubey::render::MeshHandle{}, material_handle(4)));
    require_throws([&scene, &invalid_null_mesh] { scene.commit(invalid_null_mesh); },
                   "renderable manager should reject null mesh handles");

    cubey::SceneEditQueue invalid_null_material = scene.create_edit_queue();
    invalid_null_material.renderables3d().create(
        null_material_entity, renderable_for(mesh_handle(4), cubey::render::MaterialHandle{}));
    require_throws([&scene, &invalid_null_material] { scene.commit(invalid_null_material); },
                   "renderable manager should reject null material handles");

    cubey::Renderable3D zero_instances = renderable_for(mesh_handle(5), material_handle(5));
    zero_instances.primitives[0].instance_count = 0;
    cubey::SceneEditQueue invalid_zero_instances = scene.create_edit_queue();
    invalid_zero_instances.renderables3d().create(zero_instances_entity, zero_instances);
    require_throws([&scene, &invalid_zero_instances] { scene.commit(invalid_zero_instances); },
                   "renderable manager should reject zero instance counts");

    cubey::SceneTransaction missing_transform_setup = scene.begin_transaction();
    const cubey::Entity missing_transform = missing_transform_setup.entities().create();
    missing_transform_setup.renderables3d().create(
        missing_transform, renderable_for(mesh_handle(6), material_handle(6)));
    missing_transform_setup.commit();
    cubey::SceneReadView view = scene.read();
    require_throws(
        [&view] {
            (void)cubey::build_renderable_packets_3d(view.renderables3d(), view.transforms3d());
        },
        "visible renderable packet extraction should require a 3D transform");
}
