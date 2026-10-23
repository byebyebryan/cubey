#include "sdf_workshop.h"
#include <iostream>
#include <stdexcept>
namespace sdf = cubey::projects::sdf;
using namespace cubey;
void require(bool v) {
    if (!v)
        throw std::runtime_error("SDF workshop contract failed");
}
int main() {
    sdf::SdfConfig c;
    c.workshop = "sphere";
    for (const char* id : {"plain", "limestone", "marble", "clay", "ceramic", "bronze", "glass"}) {
        c.workshop_material = id;
        auto workshop = sdf::make_material_workshop(c);
        auto legacy = sdf::load_authored_scene(std::filesystem::path(CUBEY_SOURCE_DIR) /
                                               "projects/sdf/scenes/material-study" /
                                               (std::string("sphere-") + id + ".json"));
        require(sdf::authored_geometry_hash(workshop) == sdf::authored_geometry_hash(legacy));
        require(sdf::authored_appearance_hash(workshop) == sdf::authored_appearance_hash(legacy));
        const auto round = sdf::parse_authored_scene(sdf::serialize_authored_scene(workshop));
        require(sdf::authored_appearance_hash(round) == sdf::authored_appearance_hash(workshop));
        const auto recipe = sdf::workshop_material_recipe(c, workshop);
        require(recipe.material_from_object == math::Mat4{1});
    }
    c.workshop = "csg";
    c.workshop_material = "marble";
    const auto base = sdf::make_material_workshop(c);
    require(base.leaf_count == 4 && base.program.size() == 7);
    c.workshop_cut_material = true;
    const auto cut = sdf::make_material_workshop(c);
    require(sdf::authored_geometry_hash(base) == sdf::authored_geometry_hash(cut));
    require(sdf::authored_appearance_hash(base) != sdf::authored_appearance_hash(cut));
    c.workshop_blend_materials = false;
    const auto unblended = sdf::make_material_workshop(c);
    require(sdf::authored_geometry_hash(cut) == sdf::authored_geometry_hash(unblended));
    require(sdf::authored_appearance_hash(cut) != sdf::authored_appearance_hash(unblended));
    auto mapped = base;
    mapped.material_domains[0] = math::translation({.07F, -.03F, .02F}) * math::rotation_z(.5F) *
                                 math::scale({2, .5F, 1.3F});
    const auto round = sdf::parse_authored_scene(sdf::serialize_authored_scene(mapped));
    require(sdf::authored_geometry_hash(mapped) == sdf::authored_geometry_hash(base));
    require(sdf::authored_appearance_hash(mapped) != sdf::authored_appearance_hash(base));
    require(round.material_domains == mapped.material_domains);
    auto pattern = mapped.patterns[0];
    pattern.detail.y = 16;
    const auto domain = mapped.material_domains[0];
    const math::Vec3 point{.13F, -.07F, .2F};
    const auto sample =
        procedural::sample_material_domain(mapped.materials[0], pattern, domain, point);
    const auto height = [&](math::Vec3 p) {
        const auto q = math::Vec3{domain * math::Vec4{p, 1}} + math::Vec3{pattern.offset};
        const auto seed = static_cast<std::uint32_t>(pattern.settings.y);
        return pattern.detail.x *
               (.7F * procedural::material_noise(q * 16.0F, seed + 3001).value +
                .3F * procedural::material_noise(q * (16.0F * 2.03F), seed + 4013).value);
    };
    for (int axis = 0; axis < 3; ++axis) {
        auto a = point, b = point;
        a[axis] += .0001F;
        b[axis] -= .0001F;
        require(std::abs((height(a) - height(b)) / .0002F - sample.height_gradient[axis]) <
                .00002F);
    }
    std::cout << "Shared/legacy seven-recipe fingerprints exact; CSG appearance leaves geometry "
                 "cache identity intact; affine bump derivative verified.\n";
}
