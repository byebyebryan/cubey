#include <cubey/asset/material_recipe.h>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>
#include <limits>
using namespace cubey;
void require(bool v) { if (!v) throw std::runtime_error("material recipe contract failed"); }
template<class F> void rejects(F f) { bool threw=false;try { f(); } catch(const std::exception&) {threw=true;} require(threw); }
int main() {
    for (const char* id:{"plain","limestone","marble","clay","ceramic","bronze","glass"}) {
        const auto r=asset::load_material_recipe(std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR)/(std::string(id)+".json"));
        const auto text=asset::serialize_material_recipe(r);
        const auto round=asset::parse_material_recipe(text);
        require(asset::serialize_material_recipe(round)==text);
        require(asset::material_recipe_fingerprint(r)==asset::material_recipe_fingerprint(round));
        require(r.material_from_object==math::Mat4{1});
        for (math::Vec3 p:{math::Vec3{0},math::Vec3{.21F,-.13F,.08F}}) {
            const auto a=procedural::sample_material(r.surface,r.pattern,p,.001F);
            const auto b=procedural::sample_material(round.surface,round.pattern,p,.001F);
            require(a.material.base_roughness==b.material.base_roughness && a.height_gradient==b.height_gradient);
        }
        auto j=nlohmann::json::parse(text);
        j["typo"]=3; rejects([&]{(void)asset::parse_material_recipe(j.dump());});
        j=nlohmann::json::parse(text); j["surface"]["optics"]["unknown"]=1;
        rejects([&]{(void)asset::parse_material_recipe(j.dump());});
        auto bad=r;bad.material_from_object[0][0]=0;rejects([&]{asset::validate_material_recipe(bad);});
        bad=r;bad.surface.base_roughness.x=std::numeric_limits<float>::quiet_NaN();rejects([&]{asset::validate_material_recipe(bad);});
        if (std::string(id)=="clay") require(r.surface.base_roughness.w==1 && r.optics.attenuation.w==.6F && r.pattern.settings.x==3);
        if (std::string(id)=="glass") {
            require(r.optics.settings.y==1 && r.optics.settings.w==.5F);
            bad=r;bad.pattern.settings.x=1;rejects([&]{asset::validate_material_recipe(bad);});
        }
    }
    std::cout<<"Seven standalone recipes: deterministic round-trip, finite validation, strict fields, supported glass.\n";
}
