#pragma once

#include "planet_celestial.h"

#include <cubey/render/atmosphere_environment.h>

namespace cubey::projects::planet {

[[nodiscard]] cubey::render::AtmosphereEnvironmentConfig
planet_atmosphere_environment_config(const PlanetAtmosphereInputs& inputs);

} // namespace cubey::projects::planet
