#include <cubey/engine.h>
#include <cubey/renderable_manager.h>
#include <cubey/transform_3d.h>

#include <cstdint>
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
    context.deferred_destruction().defer_after(frame.ticket, [] {});
    require(engine.retire_deferred_destruction() == 1,
            "engine should retire project deferred destruction through runtime adapter");
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
    require(first.ticket.value == repeated.ticket.value,
            "engine should reuse runtime frame for the same host timing");
    const std::uint64_t first_ticket = first.ticket.value;

    const cubey::ProjectFrame& next = engine.frame_for_timing({
        .delta_seconds = 0.02,
        .elapsed_seconds = 1.02,
        .frame_index = 11,
    });
    require(next.ticket.value == first_ticket + 1,
            "engine should issue a new runtime frame for a new host timing");
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
