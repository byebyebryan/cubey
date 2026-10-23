#include <cubey/asset/material_recipe.h>
#include <nlohmann/json.hpp>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace cubey::asset {
namespace {
using J = nlohmann::json;
void keys(const J& j, std::initializer_list<const char*> allowed) {
    if (!j.is_object()) throw std::invalid_argument("material recipe needs an object");
    for (const auto& [k,v] : j.items()) {
        (void)v;
        if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return k == a; }))
            throw std::invalid_argument("unknown material recipe field: " + k);
    }
}
math::Vec3 v3(const J& j) {
    if (!j.is_array() || j.size()!=3) throw std::invalid_argument("material vector needs three numbers");
    return {j.at(0).get<float>(),j.at(1).get<float>(),j.at(2).get<float>()};
}
J arr(math::Vec3 v) { return J::array({v.x,v.y,v.z}); }
}
void validate_material_recipe(const MaterialRecipe& r) {
    if (r.id.empty() || r.id.size()>64 || r.label.empty() || r.label.size()>128 ||
        !std::all_of(r.id.begin(),r.id.end(),[](unsigned char c) {
            return (c>='a'&&c<='z') || (c>='0'&&c<='9') || c=='-' || c=='_';
        })) throw std::invalid_argument("invalid material recipe identity");
    for (int i=0;i<4;++i) {
        const float v=r.surface.base_roughness[i], e=r.surface.emission[i];
        if (!std::isfinite(v) || v<0 || v>1 || (i==3 && v<.05F) ||
            !std::isfinite(e) || e<0 || e>32 || (i==3 && e!=0))
            throw std::invalid_argument("invalid material surface values");
        for (int k=0;k<4;++k)
            if (!std::isfinite(r.material_from_object[i][k]) || std::abs(r.material_from_object[i][k])>1000)
                throw std::invalid_argument("invalid material domain");
    }
    if (r.material_from_object[0][3]!=0 || r.material_from_object[1][3]!=0 ||
        r.material_from_object[2][3]!=0 || r.material_from_object[3][3]!=1 ||
        std::abs(glm::determinant(math::Mat3{r.material_from_object}))<1e-6F)
        throw std::invalid_argument("material domain must be an invertible affine transform");
    procedural::validate_material_pattern(r.pattern);
    procedural::validate_material_optics(r.optics);
    if (r.optics.settings.y>0 && (r.optics.settings.x!=0 || r.pattern.settings.x!=0 ||
        r.surface.base_roughness!=math::Vec4{1,1,1,.05F} ||
        r.surface.emission!=math::Vec4{0} || r.optics.attenuation.w!=0))
        throw std::invalid_argument("V1 glass requires plain white, roughness 0.05 and zero metal/emission/diffuse roughness");
}
MaterialRecipe parse_material_recipe(std::string_view text) {
    const J j=J::parse(text);
    keys(j,{"format","version","id","label","surface","material_from_object"});
    if (j.at("format")!="cubey.material.recipe" || !j.at("version").is_number_integer() || j.at("version")!=1)
        throw std::invalid_argument("unsupported material recipe version");
    MaterialRecipe r;
    r.id=j.at("id").get<std::string>(); r.label=j.at("label").get<std::string>();
    const auto& s=j.at("surface");
    keys(s,{"base_color","roughness","emission","optics","procedural"});
    r.surface.base_roughness={v3(s.at("base_color")),s.value("roughness",.5F)};
    if (s.contains("emission")) r.surface.emission={v3(s.at("emission")),0};
    if (s.contains("optics")) {
        const auto& o=s.at("optics");
        keys(o,{"metallic","transmission","ior","attenuation_color","attenuation_distance","diffuse_roughness"});
        r.optics.settings={o.value("metallic",0.0F),o.value("transmission",0.0F),o.value("ior",1.5F),o.value("attenuation_distance",1.0F)};
        r.optics.attenuation={o.contains("attenuation_color")?v3(o.at("attenuation_color")):math::Vec3{1},o.value("diffuse_roughness",0.0F)};
    }
    if (s.contains("procedural")) {
        const auto& p=s.at("procedural");
        keys(p,{"type","seed","scale","secondary_color","color_strength","roughness_variation","vein_direction","vein_frequency","bump_strength","grain_scale","vein_width","warp_strength","offset","color_variation"});
        const auto type=p.at("type").get<std::string>();
        if (type!="limestone" && type!="marble" && type!="granular") throw std::invalid_argument("unknown material pattern");
        r.pattern.settings={type=="limestone"?1.0F:(type=="marble"?2.0F:3.0F),p.value("seed",7.0F),p.value("scale",6.0F),p.value("color_strength",.5F)};
        r.pattern.secondary={p.contains("secondary_color")?v3(p.at("secondary_color")):math::Vec3{r.pattern.secondary},p.value("roughness_variation",.08F)};
        r.pattern.veins={p.contains("vein_direction")?v3(p.at("vein_direction")):math::Vec3{r.pattern.veins},p.value("vein_frequency",5.0F)};
        r.pattern.detail={p.value("bump_strength",.0005F),p.value("grain_scale",160.0F),p.value("vein_width",.1F),p.value("warp_strength",5.0F)};
        r.pattern.offset={p.contains("offset")?v3(p.at("offset")):math::Vec3{0},p.value("color_variation",0.0F)};
    }
    if (j.contains("material_from_object")) {
        const auto& m=j.at("material_from_object");
        if (!m.is_array() || m.size()!=16) throw std::invalid_argument("material domain needs 16 column-major numbers");
        for (int i=0;i<16;++i) r.material_from_object[i/4][i%4]=m.at(static_cast<std::size_t>(i)).get<float>();
    }
    validate_material_recipe(r); return r;
}
std::string serialize_material_recipe(const MaterialRecipe& r) {
    validate_material_recipe(r);
    J s={{"base_color",arr(math::Vec3{r.surface.base_roughness})},{"roughness",r.surface.base_roughness.w},{"emission",arr(math::Vec3{r.surface.emission})}};
    s["optics"]={{"metallic",r.optics.settings.x},{"transmission",r.optics.settings.y},{"ior",r.optics.settings.z},{"attenuation_distance",r.optics.settings.w},{"attenuation_color",arr(math::Vec3{r.optics.attenuation})},{"diffuse_roughness",r.optics.attenuation.w}};
    const auto& p=r.pattern;
    if (p.settings.x!=0) s["procedural"]={{"type",p.settings.x==1?"limestone":(p.settings.x==2?"marble":"granular")},{"seed",p.settings.y},{"scale",p.settings.z},{"color_strength",p.settings.w},{"secondary_color",arr(math::Vec3{p.secondary})},{"roughness_variation",p.secondary.w},{"vein_direction",arr(math::Vec3{p.veins})},{"vein_frequency",p.veins.w},{"bump_strength",p.detail.x},{"grain_scale",p.detail.y},{"vein_width",p.detail.z},{"warp_strength",p.detail.w},{"offset",arr(math::Vec3{p.offset})},{"color_variation",p.offset.w}};
    J m=J::array(); for (int i=0;i<16;++i) m.push_back(r.material_from_object[i/4][i%4]);
    return J{{"format","cubey.material.recipe"},{"version",1},{"id",r.id},{"label",r.label},{"surface",s},{"material_from_object",m}}.dump(2)+"\n";
}
MaterialRecipe load_material_recipe(const std::filesystem::path& path) {
    std::ifstream in(path); if (!in) throw std::runtime_error("cannot open material recipe: "+path.string());
    return parse_material_recipe(std::string{std::istreambuf_iterator<char>{in},{}});
}
void save_material_recipe(const std::filesystem::path& path,const MaterialRecipe& r) {
    const auto text=serialize_material_recipe(r);
    std::ofstream out(path); if (!out || !(out<<text)) throw std::runtime_error("cannot write material recipe: "+path.string());
}
std::uint64_t material_recipe_fingerprint(const MaterialRecipe& r) {
    std::uint64_t h=14695981039346656037ULL;
    for (char c:serialize_material_recipe(r)) {h^=static_cast<unsigned char>(c);h*=1099511628211ULL;}
    return h;
}
}
