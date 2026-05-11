#pragma once

#include <cubey/frame_clock.h>
#include <cubey/project_runtime.h>
#include <cubey/scene.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace cubey {

struct EngineConfig {
    std::size_t worker_count = 0;
};

class Engine {
  public:
    explicit Engine(EngineConfig config = {});

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) = delete;
    Engine& operator=(Engine&&) = delete;

    [[nodiscard]] Scene& create_scene();
    void destroy_scene(Scene& scene);

    [[nodiscard]] ProjectContext project_context();
    [[nodiscard]] const ProjectFrame& frame_for_timing(const FrameTiming& timing);
    [[nodiscard]] std::size_t retire_deferred_destruction();

  private:
    ProjectRuntimeAdapter runtime_;
    std::vector<std::unique_ptr<Scene>> scenes_;
};

} // namespace cubey
