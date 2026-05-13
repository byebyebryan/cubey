#include <cubey/scene/scene_builder.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_scene_builder_creates_common_3d_entities() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("builder mesh");
    const cubey::render::MaterialHandle material = registry.create_material("builder material");
    cubey::Scene scene(&registry);

    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity cube = cubey::scene::create_renderable_entity_3d(
        setup, cubey::scene::RenderableEntity3DConfig{
                   .transform =
                       cubey::Transform3D{
                           .translation = {1.0F, 2.0F, 3.0F},
                       },
                   .mesh = mesh,
                   .material = material,
                   .local_bounds =
                       cubey::Bounds3D{
                           .center = {0.0F, 0.0F, 0.0F},
                           .half_extent = {1.0F, 1.0F, 1.0F},
                       },
               });
    const cubey::Entity camera =
        cubey::scene::create_camera_entity_3d(setup, cubey::Transform3D{
                                                         .translation = {0.0F, 0.0F, 5.0F},
                                                     });
    const cubey::Light3D light =
        cubey::directional_light_3d({0.0F, -1.0F, 0.0F}, {1.0F, 0.9F, 0.8F}, 2.0F);
    const cubey::Entity light_entity =
        cubey::scene::create_directional_light_entity_3d(setup, light);
    setup.commit();

    cubey::SceneReadView view = scene.read();
    require(static_cast<bool>(view.transforms3d().instance(cube)),
            "renderable helper should create a transform");
    require(static_cast<bool>(view.renderables3d().instance(cube)),
            "renderable helper should create a renderable component");
    require(static_cast<bool>(view.transforms3d().instance(camera)),
            "camera helper should create a transform");
    require(static_cast<bool>(view.cameras3d().instance(camera)),
            "camera helper should create a camera component");
    require(static_cast<bool>(view.lights3d().instance(light_entity)),
            "directional light helper should create a light component");

    const cubey::Renderable3D& renderable =
        view.renderables3d().renderable(view.renderables3d().instance(cube));
    require(renderable.primitives.size() == 1,
            "renderable helper should create one primitive by default");
    require(renderable.primitives[0].mesh == mesh, "renderable helper should preserve mesh handle");
    require(renderable.primitives[0].material == material,
            "renderable helper should preserve material handle");
    require(renderable.local_bounds.half_extent.x == 1.0F,
            "renderable helper should preserve local bounds");
}
