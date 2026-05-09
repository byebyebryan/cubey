#pragma once

#include <cubey/math.h>

#include <numbers>

namespace cubey {

class OrbitController;

struct OrbitCamera3DConfig {
    math::Vec3 target{0.0F, 0.0F, 0.0F};
    float distance = 4.2F;
    float yaw = 0.0F;
    float pitch = 0.0F;
    float fovy_radians = std::numbers::pi_v<float> / 3.0F;
    float near_z = 0.1F;
    float far_z = 100.0F;
};

class OrbitCamera3D {
  public:
    explicit OrbitCamera3D(OrbitCamera3DConfig config = {});

    void reset();
    void set_target(math::Vec3 target);
    void set_distance(float distance);
    void set_orbit(float yaw, float pitch);
    void set_orbit(const OrbitController& controller);
    void set_projection(float fovy_radians, float near_z, float far_z);

    [[nodiscard]] math::Mat4 view_matrix() const;
    [[nodiscard]] math::Mat4 projection_matrix(float aspect) const;
    [[nodiscard]] math::Mat4 view_projection_matrix(float aspect) const;

    [[nodiscard]] math::Vec3 target() const {
        return target_;
    }
    [[nodiscard]] float distance() const {
        return distance_;
    }
    [[nodiscard]] float yaw() const {
        return yaw_;
    }
    [[nodiscard]] float pitch() const {
        return pitch_;
    }
    [[nodiscard]] float fovy_radians() const {
        return fovy_radians_;
    }
    [[nodiscard]] float near_z() const {
        return near_z_;
    }
    [[nodiscard]] float far_z() const {
        return far_z_;
    }

  private:
    OrbitCamera3DConfig config_;
    math::Vec3 target_{0.0F, 0.0F, 0.0F};
    float distance_ = 4.2F;
    float yaw_ = 0.0F;
    float pitch_ = 0.0F;
    float fovy_radians_ = std::numbers::pi_v<float> / 3.0F;
    float near_z_ = 0.1F;
    float far_z_ = 100.0F;
};

} // namespace cubey
