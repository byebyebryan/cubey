#include <cubey/scene/scene.h>
#include <cubey/scene/transform_3d.h>
#include <cubey/scene/view_3d.h>

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

cubey::Renderable3D renderable_for(cubey::render::MeshHandle mesh,
                                   cubey::render::MaterialHandle material,
                                   cubey::Bounds3D bounds = cubey::Bounds3D{
                                       .center = {0.0F, 0.0F, 0.0F},
                                       .half_extent = {0.5F, 0.5F, 0.5F},
                                   }) {
    return cubey::Renderable3D{
        .primitives =
            {
                cubey::RenderablePrimitive3D{
                    .mesh = mesh,
                    .material = material,
                },
            },
        .local_bounds = bounds,
    };
}

bool contains_entity(const std::vector<cubey::scene::RenderDrawPacket3D>& packets,
                     cubey::Entity entity) {
    for (const cubey::scene::RenderDrawPacket3D& packet : packets) {
        if (packet.entity == entity) {
            return true;
        }
    }
    return false;
}

} // namespace

void test_environment_3d_defaults_to_low_linear_ambient() {
    const cubey::scene::Environment3D environment;
    require_close(environment.ambient_color.x, 0.045F,
                  "default 3D ambient red should be a low linear radiance");
    require_close(environment.ambient_color.y, 0.045F,
                  "default 3D ambient green should be a low linear radiance");
    require_close(environment.ambient_color.z, 0.045F,
                  "default 3D ambient blue should be a low linear radiance");
    require_close(environment.ambient_intensity, 1.0F,
                  "default 3D ambient intensity should stay neutral");
}

void test_render_view_3d_builds_frame_plan_with_environment_draws_and_lights() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("cube");
    const cubey::render::MaterialHandle material = registry.create_material("material");

    cubey::Scene scene(&registry);
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity camera_entity = setup.entities().create();
    const cubey::Entity renderable_entity = setup.entities().create();
    const cubey::Entity light_entity = setup.entities().create();
    setup.transforms3d().create(camera_entity, cubey::Transform3D{});
    setup.cameras3d().create(camera_entity, cubey::Camera3D{});
    setup.transforms3d().create(renderable_entity, cubey::Transform3D{
                                                       .translation = {1.0F, 2.0F, -6.0F},
                                                       .scale = {2.0F, 3.0F, 4.0F},
                                                   });
    setup.renderables3d().create(renderable_entity,
                                 renderable_for(mesh, material,
                                                cubey::Bounds3D{
                                                    .center = {0.0F, 0.0F, 0.0F},
                                                    .half_extent = {1.0F, 1.0F, 1.0F},
                                                }));
    setup.lights3d().create(light_entity, cubey::directional_light_3d({0.0F, -2.0F, 0.0F}));
    setup.commit();

    cubey::SceneReadView scene_view = scene.read();
    const cubey::scene::View3D view{
        .camera_entity = camera_entity,
        .width = 640,
        .height = 320,
        .environment =
            cubey::scene::Environment3D{
                .ambient_color = {0.1F, 0.2F, 0.3F},
                .ambient_intensity = 2.0F,
            },
    };
    const cubey::scene::RenderFramePlan3D plan =
        cubey::scene::build_render_frame_plan_3d(view, scene_view, registry);

    require(plan.camera_entity == camera_entity, "frame plan should carry camera entity");
    require(plan.draw_packets.size() == 1, "frame plan should carry visible draw packets");
    require(plan.light_packets.size() == 1, "frame plan should carry scene light packets");
    require_close(plan.environment.ambient_color.y, 0.2F, "frame plan should carry ambient color");
    require_close(plan.environment.ambient_intensity, 2.0F,
                  "frame plan should carry ambient intensity");
    require_close(plan.draw_packets[0].world_bounds.center.x, 1.0F,
                  "draw packet should carry world bounds center x");
    require_close(plan.draw_packets[0].world_bounds.center.z, -6.0F,
                  "draw packet should carry world bounds center z");
    require_close(plan.draw_packets[0].world_bounds.half_extent.x, 2.0F,
                  "draw packet should carry scaled world bounds half extent x");
    require_close(plan.draw_packets[0].world_bounds.half_extent.y, 3.0F,
                  "draw packet should carry scaled world bounds half extent y");
    require_close(plan.draw_packets[0].world_bounds.half_extent.z, 4.0F,
                  "draw packet should carry scaled world bounds half extent z");
}

void test_render_view_3d_rejects_invalid_view_or_missing_camera_transform() {
    cubey::render::RenderResourceRegistry registry;
    cubey::Scene scene(&registry);
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity camera_entity = setup.entities().create();
    setup.cameras3d().create(camera_entity, cubey::Camera3D{});
    setup.commit();

    cubey::SceneReadView scene_view = scene.read();
    require_throws(
        [&scene_view, &registry, camera_entity] {
            const cubey::scene::View3D view{
                .camera_entity = camera_entity,
                .width = 0,
                .height = 320,
            };
            (void)cubey::scene::build_render_frame_plan_3d(view, scene_view, registry);
        },
        "render view should reject zero width");

    require_throws(
        [&scene_view, &registry, camera_entity] {
            const cubey::scene::View3D view{
                .camera_entity = camera_entity,
                .width = 640,
                .height = 320,
            };
            (void)cubey::scene::build_render_frame_plan_3d(view, scene_view, registry);
        },
        "render view should require a camera transform");
}

void test_render_view_3d_frustum_culls_world_bounds_and_can_be_disabled() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("cube");
    const cubey::render::MaterialHandle material = registry.create_material("material");

    cubey::Scene scene(&registry);
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity camera_entity = setup.entities().create();
    const cubey::Entity visible_entity = setup.entities().create();
    const cubey::Entity offscreen_entity = setup.entities().create();
    const cubey::Entity uncullable_entity = setup.entities().create();
    setup.transforms3d().create(camera_entity, cubey::Transform3D{});
    setup.cameras3d().create(camera_entity, cubey::Camera3D{});
    setup.transforms3d().create(visible_entity,
                                cubey::Transform3D{.translation = {0.0F, 0.0F, -3.0F}});
    setup.renderables3d().create(visible_entity, renderable_for(mesh, material));
    setup.transforms3d().create(offscreen_entity,
                                cubey::Transform3D{.translation = {100.0F, 0.0F, -3.0F}});
    setup.renderables3d().create(offscreen_entity, renderable_for(mesh, material));
    setup.transforms3d().create(uncullable_entity,
                                cubey::Transform3D{.translation = {100.0F, 0.0F, -3.0F}});
    cubey::Renderable3D uncullable_renderable = renderable_for(mesh, material);
    uncullable_renderable.culling_enabled = false;
    setup.renderables3d().create(uncullable_entity, uncullable_renderable);
    setup.commit();

    cubey::SceneReadView scene_view = scene.read();
    const cubey::scene::View3D culling_view{
        .camera_entity = camera_entity,
        .width = 640,
        .height = 320,
        .culling_enabled = true,
    };
    const cubey::scene::RenderFramePlan3D culled_plan =
        cubey::scene::build_render_frame_plan_3d(culling_view, scene_view, registry);
    require(culled_plan.draw_packets.size() == 2,
            "culling should keep visible and uncullable renderables");
    require(contains_entity(culled_plan.draw_packets, visible_entity),
            "culling should keep visible entity");
    require(!contains_entity(culled_plan.draw_packets, offscreen_entity),
            "culling should remove offscreen entity");
    require(contains_entity(culled_plan.draw_packets, uncullable_entity),
            "culling should keep renderables that disable culling");

    cubey::scene::View3D unculled_view = culling_view;
    unculled_view.culling_enabled = false;
    const cubey::scene::RenderFramePlan3D unculled_plan =
        cubey::scene::build_render_frame_plan_3d(unculled_view, scene_view, registry);
    require(unculled_plan.draw_packets.size() == 3, "disabled culling should keep all renderables");
}

void test_render_view_3d_preserves_draw_sorting_and_stale_handle_validation() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("cube");
    const cubey::render::MaterialHandle late_material =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "late",
            .sort_key = 20,
        });
    const cubey::render::MaterialHandle early_material =
        registry.create_material(cubey::render::MaterialInfo{
            .label = "early",
            .sort_key = 10,
        });

    cubey::Scene scene(&registry);
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity camera_entity = setup.entities().create();
    const cubey::Entity late_entity = setup.entities().create();
    const cubey::Entity early_entity = setup.entities().create();
    setup.transforms3d().create(camera_entity, cubey::Transform3D{});
    setup.cameras3d().create(camera_entity, cubey::Camera3D{});
    setup.transforms3d().create(late_entity,
                                cubey::Transform3D{.translation = {-0.5F, 0.0F, -3.0F}});
    setup.renderables3d().create(late_entity, renderable_for(mesh, late_material));
    setup.transforms3d().create(early_entity,
                                cubey::Transform3D{.translation = {0.5F, 0.0F, -3.0F}});
    setup.renderables3d().create(early_entity, renderable_for(mesh, early_material));
    setup.commit();

    cubey::SceneReadView scene_view = scene.read();
    const cubey::scene::View3D view{
        .camera_entity = camera_entity,
        .width = 640,
        .height = 320,
    };
    const cubey::scene::RenderFramePlan3D plan =
        cubey::scene::build_render_frame_plan_3d(view, scene_view, registry);
    require(plan.draw_packets.size() == 2, "view plan should produce draw packets");
    require(plan.draw_packets[0].material == early_material,
            "view plan should preserve draw packet material sorting");
    require(plan.draw_packets[1].material == late_material,
            "view plan should preserve draw packet material sorting");

    registry.destroy_mesh(mesh);
    require_throws(
        [&view, &scene_view, &registry] {
            (void)cubey::scene::build_render_frame_plan_3d(view, scene_view, registry);
        },
        "view planning should reject stale mesh handles");
}

void test_render_view_3d_builds_multiple_view_plans_from_one_scene_read_view() {
    cubey::render::RenderResourceRegistry registry;
    const cubey::render::MeshHandle mesh = registry.create_mesh("cube");
    const cubey::render::MaterialHandle material = registry.create_material("material");

    cubey::Scene scene(&registry);
    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity first_camera = setup.entities().create();
    const cubey::Entity second_camera = setup.entities().create();
    const cubey::Entity renderable_entity = setup.entities().create();
    setup.transforms3d().create(first_camera, cubey::Transform3D{});
    setup.cameras3d().create(first_camera, cubey::Camera3D{});
    setup.transforms3d().create(second_camera,
                                cubey::Transform3D{.translation = {0.0F, 0.0F, 4.0F}});
    setup.cameras3d().create(second_camera, cubey::Camera3D{});
    setup.transforms3d().create(renderable_entity,
                                cubey::Transform3D{.translation = {0.0F, 0.0F, -3.0F}});
    setup.renderables3d().create(renderable_entity, renderable_for(mesh, material));
    setup.commit();

    cubey::SceneReadView scene_view = scene.read();
    const std::vector<cubey::scene::View3D> views{
        cubey::scene::View3D{
            .camera_entity = first_camera,
            .width = 640,
            .height = 320,
        },
        cubey::scene::View3D{
            .camera_entity = second_camera,
            .width = 320,
            .height = 320,
            .culling_enabled = false,
        },
    };

    const std::vector<cubey::scene::RenderFramePlan3D> plans =
        cubey::scene::build_render_frame_plans_3d(views, scene_view, registry);
    require(plans.size() == 2, "multi-view planning should build one frame plan per view");
    require(plans[0].camera_entity == first_camera,
            "multi-view planning should preserve first camera entity");
    require(plans[1].camera_entity == second_camera,
            "multi-view planning should preserve second camera entity");
    require(plans[0].draw_packets.size() == 1, "first view should include visible draw packet");
    require(plans[1].draw_packets.size() == 1, "second view should include visible draw packet");
}

void test_render_view_3d_frame_pass_plan_preserves_explicit_pass_order() {
    cubey::scene::RenderFramePlan3D shadow_plan{
        .camera_entity = cubey::Entity{.index = 1, .generation = 1},
    };
    cubey::scene::RenderFramePlan3D color_plan{
        .camera_entity = cubey::Entity{.index = 2, .generation = 1},
    };

    const cubey::scene::FrameRenderPlan3D frame_plan({
        cubey::scene::RenderPassPlan3D{
            .label = "shadow",
            .kind = cubey::scene::RenderPassKind3D::DepthOnly,
            .frame_plan = shadow_plan,
        },
        cubey::scene::RenderPassPlan3D{
            .label = "main",
            .kind = cubey::scene::RenderPassKind3D::Color,
            .frame_plan = color_plan,
        },
    });

    require(frame_plan.passes().size() == 2, "frame pass plan should keep all passes");
    require(frame_plan.passes()[0].label == "shadow",
            "frame pass plan should preserve explicit first pass");
    require(frame_plan.passes()[0].kind == cubey::scene::RenderPassKind3D::DepthOnly,
            "frame pass plan should preserve first pass kind");
    require(frame_plan.passes()[1].label == "main",
            "frame pass plan should preserve explicit second pass");
    require(frame_plan.passes()[1].frame_plan.camera_entity.index == 2,
            "frame pass plan should preserve pass frame plan data");
}
