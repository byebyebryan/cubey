#include <cubey/engine/renderer_service.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

namespace cubey {

RendererService::~RendererService() = default;

PbrViewRenderer3D& RendererService::create_pbr_view_renderer_3d(PbrViewRenderer3DConfig config) {
    pbr_view_renderers_.push_back(std::make_unique<PbrViewRenderer3D>(std::move(config)));
    return *pbr_view_renderers_.back();
}

void RendererService::destroy_pbr_view_renderer_3d(PbrViewRenderer3D& renderer) {
    const auto position =
        std::find_if(pbr_view_renderers_.begin(), pbr_view_renderers_.end(),
                     [&renderer](const std::unique_ptr<PbrViewRenderer3D>& owned_renderer) {
                         return owned_renderer.get() == &renderer;
                     });
    if (position == pbr_view_renderers_.end()) {
        throw std::runtime_error("renderer service does not own PBR view renderer");
    }

    (*position)->destroy_all_resources();
    pbr_view_renderers_.erase(position);
}

void RendererService::destroy_swapchain_resources() {
    for (const std::unique_ptr<PbrViewRenderer3D>& renderer : pbr_view_renderers_) {
        renderer->destroy_swapchain_resources();
    }
}

void RendererService::destroy_all_resources() {
    for (const std::unique_ptr<PbrViewRenderer3D>& renderer : pbr_view_renderers_) {
        renderer->destroy_all_resources();
    }
    pbr_view_renderers_.clear();
}

std::size_t RendererService::renderer_count() const noexcept {
    return pbr_view_renderers_.size();
}

} // namespace cubey
