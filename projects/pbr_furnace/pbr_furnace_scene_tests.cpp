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

    require(cubey::projects::pbr_furnace::kPbrFurnaceRowCount == 6,
            "furnace grid should sweep metallic across six rows");
    require(cubey::projects::pbr_furnace::kPbrFurnaceColumnCount == 6,
            "furnace grid should sweep roughness across six columns");
    require(materials.size() == cubey::projects::pbr_furnace::kPbrFurnaceMaterialCount,
            "furnace material grid should expose every validation material");
    require(materials.front().row == 0 && materials.front().column == 0,
            "first furnace material should be row 0 column 0");
    require_close(materials.front().metallic, 0.0F,
                  "first furnace row should start the metallic sweep at dielectric");
    require_close(materials.front().roughness, 0.04F,
                  "furnace roughness ramp should start at the shader minimum");

    const auto& last_first_row =
        materials[cubey::projects::pbr_furnace::kPbrFurnaceColumnCount - 1U];
    require_close(last_first_row.metallic, 0.0F, "first row should stay dielectric");
    require_close(last_first_row.roughness, 1.0F,
                  "roughness ramp should end at full roughness");

    const auto& last_material = materials.back();
    require(last_material.row == 5 && last_material.column == 5,
            "last furnace material should be row 5 column 5");
    require_close(last_material.metallic, 1.0F,
                  "last furnace row should end the metallic sweep at full metal");
    require_close(last_material.roughness, 1.0F,
                  "last furnace column should end the roughness sweep at full roughness");

    for (std::size_t index = 1; index < materials.size(); ++index) {
        if (materials[index].row == materials[index - 1U].row) {
            require(materials[index].position.x > materials[index - 1U].position.x,
                    "furnace columns should move left to right");
            require_close(materials[index].metallic, materials[index - 1U].metallic,
                          "metallic should be constant across each row");
            require(materials[index].roughness > materials[index - 1U].roughness,
                    "roughness should increase across each row");
        } else {
            require(materials[index].column == 0, "new furnace rows should restart at column 0");
            require(materials[index].position.y < materials[index - 1U].position.y,
                    "furnace rows should move top to bottom");
            require(materials[index].metallic > materials[index - 1U].metallic,
                    "metallic should increase down the grid");
            require_close(materials[index].roughness, 0.04F,
                          "new furnace rows should restart at minimum roughness");
        }
    }
    return 0;
}
