#include <cubey/engine/renderer_service.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cubey {

RendererService::~RendererService() = default;

ForwardPbrRenderer3D&
RendererService::create_forward_pbr_renderer_3d(ForwardPbrRenderer3DConfig config) {
    forward_pbr_renderers_.push_back(std::make_unique<ForwardPbrRenderer3D>(std::move(config)));
    return *forward_pbr_renderers_.back();
}

void RendererService::destroy_forward_pbr_renderer_3d(ForwardPbrRenderer3D& renderer) {
    const auto position =
        std::find_if(forward_pbr_renderers_.begin(), forward_pbr_renderers_.end(),
                     [&renderer](const std::unique_ptr<ForwardPbrRenderer3D>& owned_renderer) {
                         return owned_renderer.get() == &renderer;
                     });
    if (position == forward_pbr_renderers_.end()) {
        throw std::runtime_error("renderer service does not own forward PBR renderer");
    }

    (*position)->destroy_all_resources();
    forward_pbr_renderers_.erase(position);
}

void RendererService::destroy_swapchain_resources() {
    for (const std::unique_ptr<ForwardPbrRenderer3D>& renderer : forward_pbr_renderers_) {
        renderer->destroy_swapchain_resources();
    }
}

void RendererService::destroy_all_resources() {
    for (const std::unique_ptr<ForwardPbrRenderer3D>& renderer : forward_pbr_renderers_) {
        renderer->destroy_all_resources();
    }
    forward_pbr_renderers_.clear();
}

std::size_t RendererService::renderer_count() const noexcept {
    return forward_pbr_renderers_.size();
}

} // namespace cubey
