#include <cubey/engine/atmosphere_environment_runtime.h>

#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

void test_atmosphere_environment_runtime_derives_lighting_and_scene_environment() {
    cubey::render::AtmosphereEnvironmentRuntime runtime;
    cubey::render::AtmosphereEnvironmentConfig environment;
    environment.sun_elevation_degrees = 35.0F;
    environment.sun_azimuth_degrees = 90.0F;

    runtime.set_environment(environment);

    const cubey::render::AtmosphereEnvironmentLighting& lighting = runtime.lighting();
    require(lighting.primary_light_intensity > 0.0F,
            "atmosphere runtime should derive primary light intensity");

    const cubey::scene::Environment3D scene_environment = runtime.scene_environment();
    require(scene_environment.diffuse_irradiance_sh_enabled,
            "atmosphere runtime scene environment should enable diffuse SH");
    require(scene_environment.diffuse_irradiance_sh[0] == lighting.diffuse_irradiance_sh[0],
            "atmosphere runtime scene environment should expose derived SH");
}

void test_atmosphere_environment_runtime_requires_resources_before_bindings() {
    cubey::render::AtmosphereEnvironmentRuntime runtime;

    bool threw = false;
    try {
        (void)runtime.reflection_probe();
    } catch (const std::runtime_error&) {
        threw = true;
    }

    require(threw, "atmosphere runtime should reject resource access before resources exist");
}
