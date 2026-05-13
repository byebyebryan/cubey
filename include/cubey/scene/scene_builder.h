#pragma once

#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/renderable_manager.h>
#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>

namespace cubey::scene {

struct RenderableEntity3DConfig {
    Transform3D transform{};
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    Bounds3D local_bounds{};
    bool visible = true;
    bool cast_shadows = true;
    bool receive_shadows = true;
};

[[nodiscard]] inline Entity create_renderable_entity_3d(SceneTransaction& transaction,
                                                        const RenderableEntity3DConfig& config) {
    Entity entity = transaction.entities().create();
    transaction.transforms3d().create(entity, config.transform);
    transaction.renderables3d().create(entity, Renderable3D{
                                                   .primitives =
                                                       {
                                                           RenderablePrimitive3D{
                                                               .mesh = config.mesh,
                                                               .material = config.material,
                                                           },
                                                       },
                                                   .local_bounds = config.local_bounds,
                                                   .visible = config.visible,
                                                   .cast_shadows = config.cast_shadows,
                                                   .receive_shadows = config.receive_shadows,
                                               });
    return entity;
}

[[nodiscard]] inline Entity create_camera_entity_3d(SceneTransaction& transaction,
                                                    const Transform3D& transform,
                                                    const Camera3D& camera = Camera3D{}) {
    Entity entity = transaction.entities().create();
    transaction.transforms3d().create(entity, transform);
    transaction.cameras3d().create(entity, camera);
    return entity;
}

[[nodiscard]] inline Entity create_directional_light_entity_3d(SceneTransaction& transaction,
                                                               const Light3D& light) {
    Entity entity = transaction.entities().create();
    transaction.lights3d().create(entity, light);
    return entity;
}

} // namespace cubey::scene
