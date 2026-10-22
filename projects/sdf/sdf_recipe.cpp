#include "sdf_recipe.h"

#include <cubey/core/file_io.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {

class Random {
  public:
    explicit Random(std::uint32_t seed) : state_(static_cast<std::uint64_t>(seed) + 1U) {
        static_cast<void>(next());
    }
    std::uint32_t next() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + 1442695040888963407ULL;
        const auto bits = static_cast<std::uint32_t>(((old >> 18U) ^ old) >> 27U);
        return std::rotr(bits, static_cast<int>(old >> 59U));
    }
    float unit() {
        return static_cast<float>(next() >> 8U) * (1.0F / 16777216.0F);
    }
    float range(float low, float high) {
        return low + (high - low) * unit();
    }

  private:
    std::uint64_t state_;
};

bool finite(math::Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

bool valid_cutter_mode(std::string_view value) {
    return value == "original" || value == "intro-bores" || value == "through";
}

} // namespace

void validate_edit(const BoxEdit& edit) {
    const auto q = edit.rotation;
    const auto norm_squared = glm::dot(q, q);
    const bool box_core = glm::all(glm::greaterThan(edit.half_extent, math::Vec3{0.0F}));
    // box_distance(p, vec3(0), radius) is exactly length(p) - radius.
    // Permit that sphere case explicitly, without epsilon extents or a GPU tag.
    const bool sphere_core =
        glm::all(glm::equal(edit.half_extent, math::Vec3{0.0F})) && edit.rounding > 0.0F;
    if (!finite(edit.center) || !finite(edit.half_extent) || (!box_core && !sphere_core) ||
        !std::isfinite(edit.rounding) || edit.rounding < 0.0F || !std::isfinite(q.x) ||
        !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w) ||
        !std::isfinite(norm_squared) || norm_squared < 1.0e-12F ||
        (edit.operation != Operation::Fill && edit.operation != Operation::Remove)) {
        throw std::invalid_argument("invalid SDF box edit");
    }
}

GpuEdit gpu_edit(const BoxEdit& edit) {
    validate_edit(edit);
    const auto inverse = glm::conjugate(glm::normalize(edit.rotation));
    return {{edit.center, static_cast<float>(edit.operation)},
            {inverse.x, inverse.y, inverse.z, inverse.w},
            {edit.half_extent, edit.rounding}};
}

std::vector<GpuEdit> gpu_edits(std::span<const BoxEdit> edits) {
    std::vector<GpuEdit> result;
    result.reserve(edits.size());
    for (const auto& edit : edits)
        result.push_back(gpu_edit(edit));
    return result;
}

float box_distance(math::Vec3 point, math::Vec3 half_extent, float rounding) {
    const auto q = glm::abs(point) - half_extent;
    return glm::length(glm::max(q, math::Vec3{0.0F})) + std::min(std::max({q.x, q.y, q.z}), 0.0F) -
           rounding;
}

float edit_distance(math::Vec3 point, const BoxEdit& edit) {
    const auto local = glm::conjugate(glm::normalize(edit.rotation)) * (point - edit.center);
    return std::max(box_distance(local, edit.half_extent, edit.rounding),
                    box_distance(point, math::Vec3{0.499F}, 0.0F));
}

float recipe_distance(math::Vec3 point, std::span<const BoxEdit> edits) {
    float distance = 1000.0F;
    for (const auto& edit : edits) {
        const float next = edit_distance(point, edit);
        distance = edit.operation == Operation::Fill ? std::min(distance, next)
                                                     : std::max(distance, -next);
    }
    return distance;
}

Recipe make_recipe(std::string_view scene, std::uint32_t seed, std::uint32_t count,
                   float cutter_width, std::string_view cutter_mode) {
    Recipe recipe;
    recipe.generator = scene == "sticks" ? "sticks-pcg32-v1" : std::string(scene);
    recipe.seed = seed;
    if (scene == "negative-stick-ball") {
        if (!std::isfinite(cutter_width) || cutter_width < 0.003F || cutter_width > 0.06F)
            throw std::invalid_argument("negative stick ball cutter width must be 0.003–0.06");
        if (!valid_cutter_mode(cutter_mode))
            throw std::invalid_argument("invalid negative stick ball cutter mode");
        if (count > 65535U)
            throw std::invalid_argument(
                "negative scenes accept at most 65535 cutters plus their base");
        // Start with the literal additive counterpart. The default width keeps
        // its dimensions; wider variants retain transforms and long box cores.
        recipe = make_recipe("sticks", seed, count);
        recipe.generator = "negative-stick-ball-pcg32-v1";
        const float scale = cutter_width / 0.003F;
        if (cutter_width != 0.003F) {
            recipe.generator = "negative-stick-ball-width-pcg32-v1";
            recipe.cutter_width = cutter_width;
        }
        if (cutter_mode != "original") {
            recipe.generator = cutter_mode == "intro-bores"
                                   ? "negative-stick-ball-intro-bores-pcg32-v1"
                                   : "negative-stick-ball-through-pcg32-v1";
            recipe.cutter_mode = cutter_mode;
        }
        for (std::size_t i = 0; i < recipe.edits.size(); ++i) {
            auto& edit = recipe.edits[i];
            edit.operation = Operation::Remove;
            edit.half_extent.y *= scale;
            edit.half_extent.z *= scale;
            edit.rounding *= scale;
            // Move only the four presentation bores away from the surface shell.
            // A long surface-tangent cutter would otherwise read as a rim notch.
            if (cutter_mode == "intro-bores" && i < 4)
                edit.center *= 0.25F;
            if (cutter_mode == "through" || (cutter_mode == "intro-bores" && i < 4))
                // Both endpoints are beyond the radius-0.30 sphere even when
                // its stick center is at radius 0.30: 0.65 - 0.30 > 0.30.
                edit.half_extent.x = 0.65F;
        }
        BoxEdit base;
        base.half_extent = math::Vec3{0.0F};
        base.rounding = 0.30F; // The additive stick centers occupy radii 0.29–0.30.
        recipe.edits.insert(recipe.edits.begin(), base);
        return recipe;
    }
    if (scene == "negative-sticks" || scene == "negative-sphere") {
        if (count > 65535U)
            throw std::invalid_argument(
                "negative scenes accept at most 65535 cutters plus their base");
        const bool sphere = scene == "negative-sphere";
        recipe.generator = sphere ? "negative-sphere-pcg32-v1" : "negative-sticks-pcg32-v1";
        recipe.edits.reserve(static_cast<std::size_t>(count) + 1);
        BoxEdit base;
        base.half_extent = sphere ? math::Vec3{0.0F} : math::Vec3{0.27F};
        base.rounding = sphere ? 0.36F : 0.018F;
        recipe.edits.push_back(base);
        Random random(seed);
        constexpr float tau = 2.0F * std::numbers::pi_v<float>;
        for (std::uint32_t i = 0; i < count; ++i) {
            const float u = random.unit();
            const float a = random.range(0.0F, tau);
            const float b = random.range(0.0F, tau);
            BoxEdit cutter;
            cutter.operation = Operation::Remove;
            cutter.rotation =
                math::Quat{std::sqrt(u) * std::cos(b), std::sqrt(1.0F - u) * std::sin(a),
                           std::sqrt(1.0F - u) * std::cos(a), std::sqrt(u) * std::sin(b)};
            const float radius = 0.20F * std::sqrt(random.unit());
            const float angle = random.range(0.0F, tau);
            cutter.center =
                cutter.rotation * math::Vec3{0, radius * std::cos(angle), radius * std::sin(angle)};
            const float low = i < 4 ? 0.04F : (i < 16 ? 0.03F : 0.02F);
            const float high = i < 4 ? 0.05F : (i < 16 ? 0.04F : 0.03F);
            cutter.half_extent = {0.65F, random.range(low, high), random.range(low, high)};
            cutter.rounding = 0.004F;
            recipe.edits.push_back(cutter);
        }
        return recipe;
    }
    if (scene == "box" || scene == "rotated" || scene == "cut") {
        BoxEdit box;
        box.half_extent = {0.24F, 0.19F, 0.22F};
        if (scene != "box")
            box.rotation = math::euler_xyz_quat({0.2F, 0.37F, 0.1F});
        recipe.edits.push_back(box);
        if (scene == "cut") {
            BoxEdit cut;
            cut.center = {0.12F, 0.11F, 0.0F};
            cut.half_extent = {0.17F, 0.14F, 0.4F};
            cut.rotation = math::angle_axis_quat(0.3F, {0, 0, 1});
            cut.operation = Operation::Remove;
            recipe.edits.push_back(cut);
        }
        return recipe;
    }
    if (scene != "sticks" || count > 65536U)
        throw std::invalid_argument("SDF scene must be box, rotated, cut, sticks or "
                                    "negative-sticks, negative-sphere or negative-stick-ball "
                                    "(sticks count <= 65536)");
    Random random(seed);
    constexpr float tau = 2.0F * std::numbers::pi_v<float>;
    recipe.edits.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const float z = random.range(-1.0F, 1.0F);
        const float angle = random.range(0.0F, tau);
        const float radial = std::sqrt(std::max(0.0F, 1.0F - z * z));
        BoxEdit edit;
        edit.center = math::Vec3{radial * std::cos(angle), z, radial * std::sin(angle)} *
                      random.range(0.29F, 0.3F);
        const float u = random.unit();
        const float a = random.range(0.0F, tau);
        const float b = random.range(0.0F, tau);
        edit.rotation = math::Quat{std::sqrt(u) * std::cos(b), std::sqrt(1.0F - u) * std::sin(a),
                                   std::sqrt(1.0F - u) * std::cos(a), std::sqrt(u) * std::sin(b)};
        edit.half_extent = {random.range(0.05F, 0.2F), 0.001F, 0.001F};
        recipe.edits.push_back(edit);
    }
    return recipe;
}

void validate_carve_recipe(const Recipe& recipe) {
    if (recipe.edits.empty() || recipe.edits.front().operation != Operation::Fill ||
        std::any_of(recipe.edits.begin() + 1, recipe.edits.end(),
                    [](const BoxEdit& edit) { return edit.operation != Operation::Remove; }))
        throw std::invalid_argument(
            "Carve motion requires one initial fill followed only by removals");
}

std::uint64_t recipe_hash(const Recipe& recipe) {
    std::uint64_t hash = 14695981039346656037ULL;
    // Hash canonical GPU lanes in a specified little-endian byte order.
    for (const auto& edit : recipe.edits) {
        const auto packed = gpu_edit(edit);
        for (const auto lane :
             {packed.center_operation, packed.inverse_rotation, packed.extent_rounding}) {
            for (int component = 0; component < 4; ++component) {
                const auto bits = std::bit_cast<std::uint32_t>(lane[component]);
                for (unsigned byte = 0; byte < 4; ++byte) {
                    hash ^= (bits >> (byte * 8U)) & 255U;
                    hash *= 1099511628211ULL;
                }
            }
        }
    }
    return hash;
}

std::size_t construction_prefix(std::size_t count, double seconds, double duration) {
    if (!std::isfinite(seconds) || !std::isfinite(duration) || duration <= 0.0)
        throw std::invalid_argument("SDF construction time must be finite with positive duration");
    return static_cast<std::size_t>(
        std::floor(static_cast<double>(count) * std::clamp(seconds / duration, 0.0, 1.0)));
}

std::string serialize_recipe(const Recipe& recipe) {
    nlohmann::json edits = nlohmann::json::array();
    for (const auto& edit : recipe.edits) {
        validate_edit(edit);
        edits.push_back(
            {{"operation", edit.operation == Operation::Fill ? "fill" : "remove"},
             {"center", {edit.center.x, edit.center.y, edit.center.z}},
             {"rotation_xyzw",
              {edit.rotation.x, edit.rotation.y, edit.rotation.z, edit.rotation.w}},
             {"half_extent", {edit.half_extent.x, edit.half_extent.y, edit.half_extent.z}},
             {"rounding", edit.rounding}});
    }
    std::ostringstream hash;
    hash << std::hex << std::setfill('0') << std::setw(16) << recipe_hash(recipe);
    nlohmann::json generator{{"name", recipe.generator}, {"seed", recipe.seed}};
    if (recipe.cutter_width) {
        if (!std::isfinite(*recipe.cutter_width) || *recipe.cutter_width < 0.003F ||
            *recipe.cutter_width > 0.06F)
            throw std::invalid_argument("invalid cutter width metadata");
        generator["cutter_width"] = *recipe.cutter_width;
    }
    if (recipe.cutter_mode) {
        if (!valid_cutter_mode(*recipe.cutter_mode))
            throw std::invalid_argument("invalid cutter mode metadata");
        generator["cutter_mode"] = *recipe.cutter_mode;
    }
    return nlohmann::json{{"format", "cubey.sdf.boxes"},
                          {"version", 1},
                          {"generator", std::move(generator)},
                          {"hash_algorithm", "fnv1a64-gpu-lanes-le-v1"},
                          {"hash", hash.str()},
                          {"edits", std::move(edits)}}
               .dump(2) +
           "\n";
}

Recipe parse_recipe(std::string_view text) {
    const auto json = nlohmann::json::parse(text);
    if (json.at("format") != "cubey.sdf.boxes" || !json.at("version").is_number_integer() ||
        json.at("version") != 1 || json.at("hash_algorithm") != "fnv1a64-gpu-lanes-le-v1" ||
        !json.at("edits").is_array() || json.at("edits").size() > 65536)
        throw std::invalid_argument("unsupported SDF recipe format/version or edit count");
    Recipe recipe;
    recipe.generator = json.at("generator").at("name").get<std::string>();
    recipe.seed = json.at("generator").at("seed").get<std::uint32_t>();
    if (json.at("generator").contains("cutter_width"))
        recipe.cutter_width = json.at("generator").at("cutter_width").get<float>();
    if (json.at("generator").contains("cutter_mode"))
        recipe.cutter_mode = json.at("generator").at("cutter_mode").get<std::string>();
    const auto vector = [](const nlohmann::json& values) {
        if (!values.is_array() || values.size() != 3)
            throw std::invalid_argument("SDF vector must contain exactly three numbers");
        return math::Vec3{values.at(0).get<float>(), values.at(1).get<float>(),
                          values.at(2).get<float>()};
    };
    for (const auto& item : json.at("edits")) {
        BoxEdit edit;
        const auto operation = item.at("operation").get<std::string>();
        if (operation != "fill" && operation != "remove")
            throw std::invalid_argument("invalid SDF box operation");
        edit.operation = operation == "fill" ? Operation::Fill : Operation::Remove;
        edit.center = vector(item.at("center"));
        edit.half_extent = vector(item.at("half_extent"));
        edit.rounding = item.at("rounding").get<float>();
        const auto& q = item.at("rotation_xyzw");
        if (!q.is_array() || q.size() != 4)
            throw std::invalid_argument("SDF quaternion must be xyzw");
        edit.rotation = {q.at(3).get<float>(), q.at(0).get<float>(), q.at(1).get<float>(),
                         q.at(2).get<float>()};
        validate_edit(edit);
        recipe.edits.push_back(edit);
    }
    const auto canonical = nlohmann::json::parse(serialize_recipe(recipe));
    if (canonical.at("hash") != json.at("hash"))
        throw std::invalid_argument("SDF recipe hash mismatch");
    return recipe;
}
void save_recipe(const std::filesystem::path& path, const Recipe& recipe) {
    const auto text = serialize_recipe(recipe);
    write_binary_file(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
Recipe load_recipe(const std::filesystem::path& path) {
    const auto bytes = read_binary_file(path);
    return parse_recipe({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}

} // namespace cubey::projects::sdf
