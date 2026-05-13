#pragma once

#include <cubey/core/math.h>
#include <cubey/render/resource_handle.h>
#include <cubey/scene/camera_3d.h>
#include <cubey/scene/light_manager.h>
#include <cubey/scene/scene_builder.h>
#include <cubey/scene/transform_3d.h>

#include <cstdint>
#include <optional>

namespace cubey::examples::common {

struct CubeScene3DConfig {
    render::MeshHandle mesh{};
    render::MaterialHandle material{};
    Transform3D cube_transform{};
    Bounds3D cube_bounds{
        .center = {0.0F, 0.0F, 0.0F},
        .half_extent = {1.0F, 1.0F, 1.0F},
    };
    float camera_distance = 4.2F;
    std::optional<Light3D> directional_light{};
    std::uint32_t instance_count = 1;
    std::uint32_t first_instance = 0;
};

struct CubeScene3D {
    Entity cube{};
    Entity camera{};
    Entity light{};
};

[[nodiscard]] inline CubeScene3D create_cube_scene_3d(SceneTransaction& transaction,
                                                      const CubeScene3DConfig& config) {
    CubeScene3D scene;
    scene.cube =
        scene::create_renderable_entity_3d(transaction, scene::RenderableEntity3DConfig{
                                                            .transform = config.cube_transform,
                                                            .mesh = config.mesh,
                                                            .material = config.material,
                                                            .local_bounds = config.cube_bounds,
                                                            .instance_count = config.instance_count,
                                                            .first_instance = config.first_instance,
                                                        });
    scene.camera = scene::create_camera_entity_3d(
        transaction, orbit_camera_transform(OrbitCameraState{.distance = config.camera_distance}));
    if (config.directional_light.has_value()) {
        scene.light = scene::create_directional_light_entity_3d(transaction,
                                                                config.directional_light.value());
    }
    return scene;
}

} // namespace cubey::examples::common
