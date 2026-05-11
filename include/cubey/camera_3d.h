#pragma once

#include <cubey/math.h>
#include <cubey/transform_3d.h>

#include <cstdint>
#include <numbers>

namespace cubey {

enum class Camera3DProjection : std::uint8_t {
    Perspective,
    Orthographic,
};

struct Camera3DConfig {
    Camera3DProjection projection = Camera3DProjection::Perspective;
    float fovy_radians = std::numbers::pi_v<float> / 3.0F;
    float orthographic_height = 2.0F;
    float near_z = 0.1F;
    float far_z = 100.0F;
};

class Camera3D {
  public:
    explicit Camera3D(Camera3DConfig config = {});

    void reset();
    void set_projection(float fovy_radians, float near_z, float far_z);
    void set_orthographic(float orthographic_height, float near_z, float far_z);

    [[nodiscard]] math::Mat4 projection_matrix(float aspect) const;
    [[nodiscard]] math::Mat4 view_matrix(const Transform3D& camera_world_transform) const;
    [[nodiscard]] math::Mat4 view_matrix(const math::Mat4& camera_world_matrix) const;
    [[nodiscard]] math::Mat4 view_projection_matrix(const Transform3D& camera_world_transform,
                                                    float aspect) const;
    [[nodiscard]] math::Mat4 view_projection_matrix(const math::Mat4& camera_world_matrix,
                                                    float aspect) const;

    [[nodiscard]] Camera3DProjection projection() const {
        return projection_;
    }
    [[nodiscard]] float fovy_radians() const {
        return fovy_radians_;
    }
    [[nodiscard]] float orthographic_height() const {
        return orthographic_height_;
    }
    [[nodiscard]] float near_z() const {
        return near_z_;
    }
    [[nodiscard]] float far_z() const {
        return far_z_;
    }

  private:
    Camera3DConfig config_;
    Camera3DProjection projection_ = Camera3DProjection::Perspective;
    float fovy_radians_ = std::numbers::pi_v<float> / 3.0F;
    float orthographic_height_ = 2.0F;
    float near_z_ = 0.1F;
    float far_z_ = 100.0F;
};

struct OrbitCameraState {
    math::Vec3 target{0.0F, 0.0F, 0.0F};
    float distance = 4.2F;
    float yaw = 0.0F;
    float pitch = 0.0F;
};

[[nodiscard]] Transform3D orbit_camera_transform(const OrbitCameraState& state);

} // namespace cubey
