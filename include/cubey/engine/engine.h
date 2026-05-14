#pragma once

#include <cubey/core/frame_clock.h>
#include <cubey/engine/project_runtime.h>
#include <cubey/engine/renderer_service.h>
#include <cubey/render/resource_registry.h>
#include <cubey/scene/scene.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace cubey {

class ProjectGpuServices;
namespace vulkan {
class GpuRuntime;
} // namespace vulkan

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

    [[nodiscard]] render::RenderResourceRegistry& render_resources() noexcept {
        return render_resources_;
    }

    [[nodiscard]] const render::RenderResourceRegistry& render_resources() const noexcept {
        return render_resources_;
    }

    [[nodiscard]] RendererService& renderers() noexcept {
        return renderers_;
    }

    [[nodiscard]] const RendererService& renderers() const noexcept {
        return renderers_;
    }

    [[nodiscard]] ProjectContext project_context();
    [[nodiscard]] const ProjectFrame& frame_for_timing(const FrameTiming& timing);
    [[nodiscard]] std::size_t retire_deferred_destruction();
    void attach_gpu(vulkan::GpuRuntime& gpu);
    void detach_gpu();
    [[nodiscard]] bool has_gpu() const noexcept;
    [[nodiscard]] ProjectGpuServices& gpu() const;

  private:
    render::RenderResourceRegistry render_resources_{};
    RendererService renderers_{};
    ProjectRuntimeAdapter runtime_;
    std::vector<std::unique_ptr<Scene>> scenes_;
};

} // namespace cubey
