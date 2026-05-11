#pragma once

#include <cubey/entity.h>
#include <cubey/light_manager.h>
#include <cubey/math.h>
#include <cubey/render/render_plan.h>
#include <cubey/render/resource_registry.h>
#include <cubey/renderable_manager.h>
#include <cubey/scene.h>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cubey::render {

struct Environment3D {
    math::Vec3 ambient_color{0.24F, 0.24F, 0.24F};
    float ambient_intensity = 1.0F;
};

struct View3D {
    Entity camera_entity{};
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    Environment3D environment{};
    bool culling_enabled = true;
};

struct Frustum3D {
    math::Mat4 view_projection_matrix{1.0F};
};

struct RenderFramePlan3D {
    Entity camera_entity{};
    math::Mat4 view_matrix{1.0F};
    math::Mat4 projection_matrix{1.0F};
    math::Mat4 view_projection_matrix{1.0F};
    std::vector<RenderDrawPacket3D> draw_packets{};
    std::vector<LightPacket3D> light_packets{};
    Environment3D environment{};
};

[[nodiscard]] inline Frustum3D
frustum_from_view_projection(const math::Mat4& view_projection_matrix) {
    return Frustum3D{
        .view_projection_matrix = view_projection_matrix,
    };
}

[[nodiscard]] inline std::array<math::Vec4, 8> clip_space_bounds_corners(const Frustum3D& frustum,
                                                                         const Bounds3D& bounds) {
    const math::Vec3& center = bounds.center;
    const math::Vec3& half = bounds.half_extent;
    return {
        frustum.view_projection_matrix *
            math::Vec4{center.x - half.x, center.y - half.y, center.z - half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x + half.x, center.y - half.y, center.z - half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x - half.x, center.y + half.y, center.z - half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x + half.x, center.y + half.y, center.z - half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x - half.x, center.y - half.y, center.z + half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x + half.x, center.y - half.y, center.z + half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x - half.x, center.y + half.y, center.z + half.z, 1.0F},
        frustum.view_projection_matrix *
            math::Vec4{center.x + half.x, center.y + half.y, center.z + half.z, 1.0F},
    };
}

[[nodiscard]] inline bool intersects(const Frustum3D& frustum, const Bounds3D& bounds) {
    const std::array<math::Vec4, 8> corners = clip_space_bounds_corners(frustum, bounds);

    bool outside_left = true;
    bool outside_right = true;
    bool outside_top = true;
    bool outside_bottom = true;
    bool outside_near = true;
    bool outside_far = true;

    for (const math::Vec4& corner : corners) {
        outside_left = outside_left && corner.x < -corner.w;
        outside_right = outside_right && corner.x > corner.w;
        outside_top = outside_top && corner.y < -corner.w;
        outside_bottom = outside_bottom && corner.y > corner.w;
        outside_near = outside_near && corner.z < 0.0F;
        outside_far = outside_far && corner.z > corner.w;
    }

    return !(outside_left || outside_right || outside_top || outside_bottom || outside_near ||
             outside_far);
}

[[nodiscard]] inline std::vector<RenderablePacket3D>
cull_renderable_packets_3d(std::vector<RenderablePacket3D> packets, const Frustum3D& frustum) {
    std::vector<RenderablePacket3D> visible;
    visible.reserve(packets.size());
    for (const RenderablePacket3D& packet : packets) {
        if (intersects(frustum, packet.world_bounds)) {
            visible.push_back(packet);
        }
    }
    return visible;
}

[[nodiscard]] inline RenderFramePlan3D
build_render_frame_plan_3d(const View3D& view, const SceneReadView& scene,
                           const RenderResourceRegistry& resources) {
    if (!view.camera_entity) {
        throw std::runtime_error("render view requires a camera entity");
    }
    if (view.width == 0 || view.height == 0) {
        throw std::runtime_error("render view dimensions must be positive");
    }

    const float aspect = static_cast<float>(view.width) / static_cast<float>(view.height);
    const CameraInstance3D camera = scene.cameras3d().instance(view.camera_entity);
    const math::Mat4 projection_matrix = scene.cameras3d().projection_matrix(camera, aspect);
    const math::Mat4 view_matrix = scene.cameras3d().view_matrix(camera, scene.transforms3d());
    const math::Mat4 view_projection_matrix = projection_matrix * view_matrix;

    std::vector<RenderablePacket3D> renderable_packets =
        build_renderable_packets_3d(scene.renderables3d(), scene.transforms3d());
    if (view.culling_enabled) {
        renderable_packets = cull_renderable_packets_3d(
            std::move(renderable_packets), frustum_from_view_projection(view_projection_matrix));
    }

    return RenderFramePlan3D{
        .camera_entity = view.camera_entity,
        .view_matrix = view_matrix,
        .projection_matrix = projection_matrix,
        .view_projection_matrix = view_projection_matrix,
        .draw_packets = build_render_draw_packets_3d(renderable_packets, resources),
        .light_packets = build_light_packets_3d(scene.lights3d(), scene.transforms3d()),
        .environment = view.environment,
    };
}

} // namespace cubey::render
