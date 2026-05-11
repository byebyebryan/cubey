#include <cubey/engine.h>

#include <cstdint>
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
