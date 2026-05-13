#include "pbr_furnace_scene.h"

#include <cmath>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_close(float actual, float expected, const char* message) {
    if (std::fabs(actual - expected) > 0.0001F) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    const auto materials = cubey::projects::pbr_furnace::pbr_furnace_material_grid();

    require(materials.size() == cubey::projects::pbr_furnace::kPbrFurnaceMaterialCount,
            "furnace material grid should expose every validation material");
    require(materials.front().row == 0 && materials.front().column == 0,
            "first furnace material should be row 0 column 0");
    require_close(materials.front().metallic, 1.0F,
                  "first furnace row should validate white metals");
    require_close(materials.front().roughness, 0.04F,
                  "furnace roughness ramp should start at the shader minimum");

    const auto& last_metal = materials[cubey::projects::pbr_furnace::kPbrFurnaceColumnCount - 1U];
    require_close(last_metal.metallic, 1.0F, "first row should stay metallic");
    require_close(last_metal.roughness, 1.0F, "roughness ramp should end at full roughness");

    const auto& first_dielectric = materials[cubey::projects::pbr_furnace::kPbrFurnaceColumnCount];
    require(first_dielectric.row == 1 && first_dielectric.column == 0,
            "second furnace row should start after the metallic row");
    require_close(first_dielectric.metallic, 0.0F,
                  "second furnace row should validate white dielectrics");
    require_close(first_dielectric.roughness, 0.04F,
                  "dielectric row should use the same roughness ramp");

    for (std::size_t index = 1; index < materials.size(); ++index) {
        if (materials[index].row == materials[index - 1U].row) {
            require(materials[index].position.x > materials[index - 1U].position.x,
                    "furnace columns should move left to right");
        }
    }
    return 0;
}
