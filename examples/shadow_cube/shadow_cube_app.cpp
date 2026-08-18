#include "shadow_cube_app_internal.h"

#include <cubey/render/primitive_resource.h>

#include <stdexcept>
#include <utility>

namespace cubey::examples::shadow_cube {
namespace detail {

ShadowCubeApp::ShadowCubeApp(RunConfig config) : config_(std::move(config)) {
    orbit_controller_.set_home_distance(kShadowCubeCameraDistance);
}

int ShadowCubeApp::run() {
    cubey::host::WindowedAppCallbacks callbacks;
    callbacks.create_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        create_global_resources_if_needed(context);
        create_swapchain_resources(context);
    };
    callbacks.destroy_swapchain_resources = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_swapchain_resources();
    };
    callbacks.update = [this](cubey::host::WindowedAppContext& context, const FrameTiming& timing) {
        orbit_controller_.update_from_input(context.filtered_input(), timing.delta_seconds);
        update_scene_transform(timing);
    };
    callbacks.record_frame = [this](cubey::host::WindowedAppContext& context,
                                    const cubey::host::WindowedRenderFrame& frame) {
        record_shadow_frame(context, frame);
    };
    callbacks.shutdown = [this](cubey::host::WindowedAppContext& context) {
        (void)context;
        destroy_all_resources();
    };

    return cubey::host::run_windowed_app(
        {
            .run_config = cubey::host::common_run_config_from_legacy(config_),
            .app_name = "shadow_cube",
            .ready_status = "rendering directional shadow cube",
            .required_queue_flags = VK_QUEUE_GRAPHICS_BIT,
            .swapchain_image_usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .require_dynamic_rendering = true,
            .close_on_escape = true,
        },
        std::move(callbacks));
}

cubey::Scene& ShadowCubeApp::scene() {
    if (scene_ == nullptr) {
        throw std::runtime_error("shadow_cube scene is not initialized");
    }
    return *scene_;
}

void ShadowCubeApp::destroy_scene_if_needed() {
    if (scene_ == nullptr) {
        return;
    }
    engine_.destroy_scene(*scene_);
    scene_ = nullptr;
    cube_entity_ = {};
    floor_entity_ = {};
    camera_entity_ = {};
    light_camera_entity_ = {};
    light_entity_ = {};
}

void ShadowCubeApp::destroy_render_handles() {
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, cube_mesh_handle_);
    cubey::render::destroy_mesh_resource(engine_.render_resources(), meshes_, floor_mesh_handle_);
    if (engine_.render_resources().is_alive(material_handle_)) {
        engine_.render_resources().destroy_material(material_handle_);
        material_handle_ = {};
    }
}

const cubey::render::DepthTexture& ShadowCubeApp::shadow_depth() const {
    return shadow_pass().depth_texture();
}

const cubey::render::ShadowMapPass3D& ShadowCubeApp::shadow_pass() const {
    if (!shadow_pass_.has_value()) {
        throw std::runtime_error("shadow pass is not initialized");
    }
    return shadow_pass_.value();
}

const cubey::render::MaterialInstance& ShadowCubeApp::scene_material_instance() const {
    if (!scene_material_instance_.has_value()) {
        throw std::runtime_error("shadow descriptors are not initialized");
    }
    return scene_material_instance_.value();
}

const cubey::vulkan::Sampler& ShadowCubeApp::present_sampler() const {
    if (!present_sampler_.has_value()) {
        throw std::runtime_error("present sampler is not initialized");
    }
    return present_sampler_.value();
}

const cubey::render::MaterialInstance& ShadowCubeApp::present_material_instance() const {
    if (!present_material_instance_.has_value()) {
        throw std::runtime_error("present descriptors are not initialized");
    }
    return present_material_instance_.value();
}

const cubey::render::GraphicsPipelineResource& ShadowCubeApp::shadow_pipeline_resource() const {
    return shadow_pass().pipeline();
}

const cubey::render::GraphicsPipelineResource& ShadowCubeApp::scene_pipeline_resource() const {
    if (!scene_pipeline_resource_.has_value()) {
        throw std::runtime_error("scene pipeline resource is not initialized");
    }
    return scene_pipeline_resource_.value();
}

const cubey::render::GraphicsPipelineResource& ShadowCubeApp::present_pipeline_resource() const {
    if (!present_pipeline_resource_.has_value()) {
        throw std::runtime_error("present pipeline resource is not initialized");
    }
    return present_pipeline_resource_.value();
}

const cubey::vulkan::DepthAttachment& ShadowCubeApp::depth_attachment() const {
    if (!depth_attachment_.has_value()) {
        throw std::runtime_error("depth attachment is not initialized");
    }
    return depth_attachment_.value();
}

} // namespace detail

int run_shadow_cube(const RunConfig& config) {
    detail::ShadowCubeApp app(config);
    return app.run();
}

} // namespace cubey::examples::shadow_cube
