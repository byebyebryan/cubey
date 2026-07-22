#include <cubey/engine/engine.h>
#include <cubey/engine/project_gpu_services.h>
#include <cubey/engine/renderer_service.h>
#include <cubey/scene/renderable_manager.h>
#include <cubey/scene/transform_3d.h>

#include <vulkan/vulkan.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

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
                .half_extent = {1.0F, 1.0F, 1.0F},
            },
    };
}

cubey::vulkan::Device* fake_device() {
    return reinterpret_cast<cubey::vulkan::Device*>(0x55);
}

cubey::vulkan::SubmissionCoordinator fake_submission() {
    return cubey::vulkan::SubmissionCoordinator(
        reinterpret_cast<VkQueue>(0x56),
        [](VkQueue, const cubey::vulkan::QueueSubmitInfo&, const char*) {},
        [](VkQueue, const char*) {});
}

cubey::ForwardPbrRenderer3DConfig valid_forward_pbr_renderer_config() {
    return {
        .pbr_vertex_shader = "pbr.vert.spv",
        .pbr_fragment_shader = "pbr.frag.spv",
        .skybox_vertex_shader = "skybox.vert.spv",
        .skybox_fragment_shader = "skybox.frag.spv",
        .post_vertex_shader = "post.vert.spv",
        .post_fragment_shader = "post.frag.spv",
        .shadow_depth_vertex_shader = "shadow.vert.spv",
        .shadow_depth_fragment_shader = "shadow.frag.spv",
    };
}

} // namespace

void test_engine_exposes_project_runtime_services() {
    cubey::Engine engine(cubey::EngineConfig{.worker_count = 1});
    cubey::ProjectContext context = engine.project_context();

    auto job = context.jobs().submit([] { return 33; });
    require(job.get() == 33, "engine context should expose a working job system");

    const cubey::UploadTicket upload = context.upload_queue().enqueue({
        .label = "engine upload",
        .bytes = {1, 2, 3},
    });
    require(upload.id == 1, "engine context should expose upload queue state");

    const cubey::ProjectFrame& frame = engine.frame_for_timing({
        .delta_seconds = 0.016,
        .elapsed_seconds = 0.25,
        .frame_index = 2,
    });
    require(frame.frame_index == 2 && frame.elapsed_seconds == 0.25,
            "engine should expose host timing through its project frame");
}

void test_engine_attaches_gpu_services_to_project_context() {
    cubey::Engine engine;
    cubey::ProjectContext detached_context = engine.project_context();
    require(!engine.has_gpu(), "engine should start without attached GPU services");
    require(!detached_context.has_gpu(),
            "engine project context should start without GPU services");
    require_throws([&detached_context] { static_cast<void>(detached_context.gpu()); },
                   "detached engine project context should reject GPU access");

    cubey::vulkan::SubmissionCoordinator submission = fake_submission();
    cubey::vulkan::GpuRuntime runtime({
        .device = fake_device(),
        .submission = &submission,
        .execution_mode = cubey::vulkan::GpuRuntimeExecutionMode::Inline,
    });
    engine.attach_gpu(runtime);

    cubey::ProjectContext attached_context = engine.project_context();
    require(engine.has_gpu(), "engine should report attached GPU services");
    require(attached_context.has_gpu(), "engine project context should expose GPU services");
    require(&attached_context.gpu() == &engine.gpu(),
            "engine project context should reference engine GPU services");

    engine.detach_gpu();
    cubey::ProjectContext detached_again = engine.project_context();
    require(!engine.has_gpu(), "engine should report detached GPU services");
    require(!detached_again.has_gpu(),
            "engine project context should lose GPU services after detach");
    require_throws([&detached_again] { static_cast<void>(detached_again.gpu()); },
                   "detached-again engine project context should reject GPU access");
}

void test_engine_reuses_project_frame_for_same_timing() {
    cubey::Engine engine;

    const cubey::FrameTiming timing{
        .delta_seconds = 0.016,
        .elapsed_seconds = 1.0,
        .frame_index = 10,
    };
    const cubey::ProjectFrame& first = engine.frame_for_timing(timing);
    const cubey::ProjectFrame& repeated = engine.frame_for_timing(timing);
    require(&first == &repeated, "engine should reuse runtime frame for the same host timing");

    const cubey::ProjectFrame& next = engine.frame_for_timing({
        .delta_seconds = 0.02,
        .elapsed_seconds = 1.02,
        .frame_index = 11,
    });
    require(next.frame_index == 11 && next.elapsed_seconds == 1.02,
            "engine should update its runtime frame for new host timing");
}

void test_engine_creates_independent_scenes() {
    cubey::Engine engine;
    cubey::Scene& first_scene = engine.create_scene();
    cubey::Scene& second_scene = engine.create_scene();

    cubey::SceneTransaction first_setup = first_scene.begin_transaction();
    const cubey::Entity first_entity = first_setup.entities().create();
    first_setup.commit();

    require(first_scene.entities().is_alive(first_entity),
            "engine-created scene should expose usable entity storage");
    require(first_scene.epoch() == 1, "first scene should publish its own epoch");
    require(second_scene.epoch() == 0, "second scene should not observe first scene commits");
}

void test_engine_destroys_owned_scenes_and_rejects_foreign_scenes() {
    cubey::Engine engine;
    cubey::Scene& owned_scene = engine.create_scene();
    engine.destroy_scene(owned_scene);

    cubey::Scene foreign_scene;
    require_throws([&engine, &foreign_scene] { engine.destroy_scene(foreign_scene); },
                   "engine should reject scenes it does not own");
}

void test_engine_exposes_render_resource_registry() {
    cubey::Engine engine;

    const cubey::render::MeshHandle mesh = engine.render_resources().create_mesh("engine mesh");
    require(engine.render_resources().is_alive(mesh),
            "engine should expose live mesh handles through render resources");
    require(engine.render_resources().label(mesh) == "engine mesh",
            "engine render resource labels should round-trip");
}

void test_engine_created_scenes_validate_render_resource_handles() {
    cubey::Engine engine;
    const cubey::render::MeshHandle mesh = engine.render_resources().create_mesh("cube mesh");
    const cubey::render::MaterialHandle material =
        engine.render_resources().create_material("cube material");
    cubey::Scene& scene = engine.create_scene();

    cubey::SceneTransaction setup = scene.begin_transaction();
    const cubey::Entity entity = setup.entities().create();
    setup.transforms3d().create(entity, cubey::Transform3D{});
    setup.renderables3d().create(entity, renderable_for(mesh, material));
    setup.commit();

    engine.render_resources().destroy_mesh(mesh);

    cubey::SceneTransaction stale_setup = scene.begin_transaction();
    const cubey::Entity stale_entity = stale_setup.entities().create();
    stale_setup.transforms3d().create(stale_entity, cubey::Transform3D{});
    stale_setup.renderables3d().create(stale_entity, renderable_for(mesh, material));
    require_throws([&stale_setup] { stale_setup.commit(); },
                   "engine-created scenes should reject destroyed mesh handles");
}

void test_engine_exposes_renderer_service() {
    cubey::Engine engine;

    require(engine.renderers().renderer_count() == 0,
            "engine renderer service should start without renderer instances");
    require(&engine.renderers() == &std::as_const(engine).renderers(),
            "engine should expose the same renderer service through const and mutable access");
}

void test_renderer_service_owns_forward_pbr_renderer_instances() {
    cubey::Engine engine;

    cubey::ForwardPbrRenderer3D& first =
        engine.renderers().create_forward_pbr_renderer_3d(valid_forward_pbr_renderer_config());
    cubey::ForwardPbrRenderer3D& second =
        engine.renderers().create_forward_pbr_renderer_3d(valid_forward_pbr_renderer_config());

    require(engine.renderers().renderer_count() == 2,
            "renderer service should count engine-owned renderer instances");
    require(&first != &second, "renderer service should return distinct renderer instances");

    engine.renderers().destroy_forward_pbr_renderer_3d(first);
    require(engine.renderers().renderer_count() == 1,
            "renderer service should destroy one renderer at a time");

    engine.renderers().destroy_all_resources();
    require(engine.renderers().renderer_count() == 0,
            "renderer service destroy_all_resources should release renderer instances");
}

void test_renderer_service_rejects_foreign_forward_pbr_renderer() {
    cubey::Engine engine;
    cubey::ForwardPbrRenderer3D foreign(valid_forward_pbr_renderer_config());

    require_throws(
        [&engine, &foreign] { engine.renderers().destroy_forward_pbr_renderer_3d(foreign); },
        "renderer service should reject renderers it does not own");
}

void test_renderer_service_resource_lifecycle_is_safe_without_renderers() {
    cubey::Engine engine;

    engine.renderers().destroy_swapchain_resources();
    engine.renderers().destroy_all_resources();

    require(engine.renderers().renderer_count() == 0,
            "empty renderer service lifecycle calls should keep the renderer list empty");
}
