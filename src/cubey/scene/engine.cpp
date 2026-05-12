#include <cubey/scene/engine.h>

#include <cubey/runtime/project_gpu_services.h>

#include <algorithm>
#include <stdexcept>

namespace cubey {

Engine::Engine(EngineConfig config) : runtime_(config.worker_count) {}

Scene& Engine::create_scene() {
    scenes_.push_back(std::make_unique<Scene>(&render_resources_));
    return *scenes_.back();
}

void Engine::destroy_scene(Scene& scene) {
    const auto position = std::find_if(scenes_.begin(), scenes_.end(),
                                       [&scene](const std::unique_ptr<Scene>& owned_scene) {
                                           return owned_scene.get() == &scene;
                                       });
    if (position == scenes_.end()) {
        throw std::runtime_error("engine does not own scene");
    }
    scenes_.erase(position);
}

ProjectContext Engine::project_context() {
    return runtime_.context();
}

const ProjectFrame& Engine::frame_for_timing(const FrameTiming& timing) {
    return runtime_.frame_for_timing(timing);
}

std::size_t Engine::retire_deferred_destruction() {
    return runtime_.retire_deferred_destruction();
}

void Engine::attach_gpu(vulkan::GpuRuntime& gpu) {
    runtime_.attach_gpu(gpu);
}

void Engine::detach_gpu() {
    runtime_.detach_gpu();
}

bool Engine::has_gpu() const noexcept {
    return runtime_.has_gpu();
}

ProjectGpuServices& Engine::gpu() const {
    return runtime_.gpu();
}

} // namespace cubey
