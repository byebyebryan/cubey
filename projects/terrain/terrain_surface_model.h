#pragma once

// Compatibility facade. Semantics and implementation belong to the foundation.
#include "terrain_raster_climate_source.h"
#include <cubey/terrain/terrain_surface_model.h>

namespace cubey::projects::terrain {
using cubey::terrain::kTerrainSurfaceModelFormulaVersion;
using cubey::terrain::terrain_climate_potential;
using cubey::terrain::terrain_surface_model_parameter_hash;
using cubey::terrain::terrain_surface_weights;
using cubey::terrain::TerrainClimatePotential;
using cubey::terrain::TerrainSurfaceInputs;
using cubey::terrain::TerrainSurfaceModel;
using cubey::terrain::TerrainSurfaceWeights;
} // namespace cubey::projects::terrain
