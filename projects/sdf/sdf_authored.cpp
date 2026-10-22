#include "sdf_authored.h"
#include "sdf_history.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <cubey/core/file_io.h>
#include <functional>
#include <iomanip>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
using Json = nlohmann::json;
struct Transform {
    math::Vec3 center{0};
    math::Quat rotation = math::identity_quat();
    float scale = 1;
};
float number(float value, float low, float high) {
    if (!std::isfinite(value) || value < low || value > high)
        throw std::invalid_argument("authored SDF number outside its finite range");
    return value;
}
math::Vec3 vector(const Json& value, float low = -10, float high = 10) {
    if (!value.is_array() || value.size() != 3)
        throw std::invalid_argument("authored SDF vector must contain three numbers");
    return {number(value.at(0).get<float>(), low, high),
            number(value.at(1).get<float>(), low, high),
            number(value.at(2).get<float>(), low, high)};
}
Transform transform(const Json& node, const Transform& parent) {
    Transform local;
    if (node.contains("position"))
        local.center = vector(node.at("position"));
    if (node.contains("rotation_xyzw")) {
        const auto& q = node.at("rotation_xyzw");
        if (!q.is_array() || q.size() != 4)
            throw std::invalid_argument("authored SDF quaternion must be xyzw");
        local.rotation = {number(q.at(3).get<float>(), -1, 1), number(q.at(0).get<float>(), -1, 1),
                          number(q.at(1).get<float>(), -1, 1), number(q.at(2).get<float>(), -1, 1)};
        if (glm::dot(local.rotation, local.rotation) < 1e-12F)
            throw std::invalid_argument("authored SDF rotation is zero");
        local.rotation = glm::normalize(local.rotation);
    }
    local.scale = number(node.value("scale", 1.0F), .001F, 100.0F);
    const Transform world{parent.center + parent.rotation * (local.center * parent.scale),
                          glm::normalize(parent.rotation * local.rotation),
                          number(parent.scale * local.scale, .0001F, 1000.0F)};
    for (int axis = 0; axis < 3; ++axis)
        number(world.center[axis], -1000, 1000);
    return world;
}
void hash_float(std::uint64_t& hash, float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned byte = 0; byte < 4; ++byte) {
        hash ^= (bits >> (8U * byte)) & 255U;
        hash *= 1099511628211ULL;
    }
}
std::string hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << value;
    return stream.str();
}
float primitive_distance(math::Vec3 point, const SceneInstruction& node) {
    const auto q = node.inverse_rotation;
    auto p = point - math::Vec3(node.center_code);
    p += 2.0F * glm::cross(math::Vec3(q), glm::cross(math::Vec3(q), p) + q.w * p);
    p /= node.settings.x;
    float distance = 0;
    const auto code = static_cast<ShapeCode>(static_cast<unsigned>(node.center_code.w));
    if (code == ShapeCode::Box)
        distance = box_distance(p, math::Vec3(node.parameters), node.parameters.w);
    else if (code == ShapeCode::Sphere)
        distance = glm::length(p) - node.parameters.x;
    else if (code == ShapeCode::Capsule) {
        p.y -= std::clamp(p.y, -node.parameters.y, node.parameters.y);
        distance = glm::length(p) - node.parameters.x;
    } else if (code == ShapeCode::Cylinder) {
        const math::Vec2 d{glm::length(math::Vec2{p.x, p.z}) - node.parameters.x,
                           std::abs(p.y) - node.parameters.y};
        distance = glm::length(glm::max(d, math::Vec2{0})) + std::min(std::max(d.x, d.y), 0.0F);
    } else if (code == ShapeCode::Torus) {
        distance =
            glm::length(math::Vec2{glm::length(math::Vec2{p.x, p.z}) - node.parameters.x, p.y}) -
            node.parameters.y;
    } else if (code == ShapeCode::CappedCone) {
        // Revolve the exact distance to the frustum's two caps and side segment.
        const float bottom = node.parameters.x, top = node.parameters.y, h = node.parameters.z;
        const math::Vec2 radial{glm::length(math::Vec2{p.x, p.z}), p.y};
        const math::Vec2 a{bottom, -h}, edge{top - bottom, 2 * h};
        const auto side =
            radial - a -
            edge * std::clamp(glm::dot(radial - a, edge) / glm::dot(edge, edge), 0.0F, 1.0F);
        const float caps =
            std::min(glm::length(math::Vec2{std::max(radial.x - bottom, 0.0F), radial.y + h}),
                     glm::length(math::Vec2{std::max(radial.x - top, 0.0F), radial.y - h}));
        const float radius = bottom + (top - bottom) * (radial.y + h) / (2 * h);
        const bool inside = std::abs(radial.y) <= h && radial.x <= radius;
        distance = std::min(caps, glm::length(side)) * (inside ? -1.0F : 1.0F);
    } else if (code == ShapeCode::RoundCone) {
        // Convex hull of two endpoint spheres. In a meridian plane the outer
        // boundary is a common tangent joined to the two circular caps.
        const float bottom = node.parameters.x, top = node.parameters.y;
        const float height = 2 * node.parameters.z;
        const math::Vec2 v{glm::length(math::Vec2{p.x, p.z}), p.y + node.parameters.z};
        if (std::abs(bottom - top) >= height) {
            distance = bottom >= top ? glm::length(v) - bottom
                                    : glm::length(v - math::Vec2{0, height}) - top;
        } else {
            const float slope = (top - bottom) / height;
            const float radial = std::sqrt(std::max(0.0F, 1 - slope * slope));
            const math::Vec2 tangent{slope, radial}, normal{radial, -slope};
            const float along = glm::dot(v, tangent);
            if (along < 0)
                distance = glm::length(v) - bottom;
            else if (along > height * radial)
                distance = glm::length(v - math::Vec2{0, height}) - top;
            else
                distance = glm::dot(v, normal) - bottom;
        }
    }
    return distance * node.settings.x;
}
void validate_material(const SurfaceMaterial& m) {
    for (int i = 0; i < 3; ++i) {
        number(m.base_roughness[i], 0, 1);
        number(m.emission[i], 0, 32);
    }
    number(m.base_roughness.w, .05F, 1);
}
} // namespace

AuthoredScene parse_authored_scene(std::string_view text) {
    const auto json = Json::parse(text);
    if (json.at("format") != "cubey.sdf.scene" || !json.at("version").is_number_integer() ||
        json.at("version") != 2 || !json.at("materials").is_array() ||
        json.at("materials").empty() || json.at("materials").size() > 16)
        throw std::invalid_argument("unsupported authored SDF scene or material count");
    AuthoredScene result;
    result.name = json.at("name").get<std::string>();
    if (result.name.empty())
        throw std::invalid_argument("authored SDF scene needs a name");
    std::map<std::string, unsigned> material_indices;
    for (const auto& item : json.at("materials")) {
        const auto name = item.at("name").get<std::string>();
        if (name.empty() ||
            !material_indices.emplace(name, static_cast<unsigned>(result.materials.size())).second)
            throw std::invalid_argument("authored SDF material names must be unique");
        SurfaceMaterial m;
        m.base_roughness = {vector(item.at("base_color"), 0, 1),
                            number(item.value("roughness", .5F), .05F, 1)};
        if (item.contains("emission"))
            m.emission = {vector(item.at("emission"), 0, 32), 0};
        result.materials.push_back(m);
        result.material_names.push_back(name);
    }
    const auto material_index = [&](const std::string& name) {
        const auto found = material_indices.find(name);
        if (found == material_indices.end())
            throw std::invalid_argument("unknown authored SDF material: " + name);
        return found->second;
    };
    std::set<std::string> names;
    std::size_t visited = 0;
    std::function<void(const Json&, Transform, unsigned, unsigned)> compile;
    compile = [&](const Json& node, Transform parent, unsigned inherited, unsigned depth) {
        if (++visited > authored_max_instructions || depth > 24)
            throw std::invalid_argument("authored SDF tree exceeds node/depth limits");
        const auto name = node.at("name").get<std::string>();
        if (name.empty() || !names.insert(name).second)
            throw std::invalid_argument("authored SDF part names must be unique");
        const auto world = transform(node, parent);
        const auto mat = node.contains("material")
                             ? material_index(node.at("material").get<std::string>())
                             : inherited;
        const auto type = node.at("type").get<std::string>();
        if (type == "group") {
            const auto& children = node.at("children");
            if (!children.is_array() || children.size() != 1)
                throw std::invalid_argument("authored transform group requires one child");
            compile(children.at(0), world, mat, depth + 1);
        } else if (type == "union" || type == "subtract" || type == "smooth-union" ||
                   type == "smooth-subtract") {
            const bool subtract = type == "subtract" || type == "smooth-subtract";
            const auto& children = node.at("children");
            if (!children.is_array() || children.size() < 2 ||
                children.size() > authored_max_leaves || (subtract && children.size() != 2))
                throw std::invalid_argument("invalid authored SDF operator arity");
            compile(children.at(0), world, mat, depth + 1);
            for (std::size_t i = 1; i < children.size(); ++i) {
                compile(children.at(i), world, mat, depth + 1);
                SceneInstruction instruction;
                instruction.center_code.w =
                    static_cast<float>(type == "union"             ? ShapeCode::Union
                                       : type == "subtract"        ? ShapeCode::Subtract
                                       : type == "smooth-subtract" ? ShapeCode::SmoothSubtract
                                                                   : ShapeCode::SmoothUnion);
                instruction.settings.y =
                    (type == "smooth-union" || type == "smooth-subtract")
                        ? number(node.at("radius").get<float>(), .00001F, 1) * world.scale
                        : 0;
                instruction.settings.w =
                    subtract ? (node.contains("cut_material")
                                    ? static_cast<float>(material_index(
                                          node.at("cut_material").get<std::string>()))
                                    : -1.0F)
                             : (node.value("blend_materials", true) ? 1.0F : 0.0F);
                result.program.push_back(instruction);
                result.part_names.push_back(name);
            }
        } else {
            if (++result.leaf_count > authored_max_leaves)
                throw std::invalid_argument("authored SDF scene exceeds 128 primitive leaves");
            SceneInstruction instruction;
            ShapeCode code;
            if (type == "box") {
                code = ShapeCode::Box;
                instruction.parameters = {vector(node.at("half_extent"), .000001F, 10),
                                          number(node.value("rounding", 0.0F), 0, 1)};
            } else if (type == "sphere") {
                code = ShapeCode::Sphere;
                instruction.parameters.x = number(node.at("radius").get<float>(), .000001F, 10);
            } else if (type == "capsule" || type == "cylinder") {
                code = type == "capsule" ? ShapeCode::Capsule : ShapeCode::Cylinder;
                instruction.parameters.x = number(node.at("radius").get<float>(), .000001F, 10);
                instruction.parameters.y =
                    number(node.at("half_length").get<float>(), .000001F, 10);
            } else if (type == "torus") {
                code = ShapeCode::Torus;
                const float major = number(node.at("major_radius").get<float>(), .000001F, 10);
                const float minor = number(node.at("minor_radius").get<float>(), .000001F, 10);
                if (minor >= major)
                    throw std::invalid_argument(
                        "authored torus requires minor radius below major radius");
                instruction.parameters = {major, minor, 0, 0};
            } else if (type == "capped-cone" || type == "round-cone") {
                code = type == "capped-cone" ? ShapeCode::CappedCone : ShapeCode::RoundCone;
                const float bottom = number(node.at("bottom_radius").get<float>(), 0, 10);
                const float top = number(node.at("top_radius").get<float>(), 0, 10);
                if (std::max(bottom, top) < .000001F)
                    throw std::invalid_argument("authored cone needs a positive end radius");
                instruction.parameters = {
                    bottom, top, number(node.at("half_length").get<float>(), .000001F, 10), 0};
            } else
                throw std::invalid_argument("unknown authored SDF primitive: " + type);
            const auto inverse = glm::conjugate(world.rotation);
            instruction.center_code = {world.center, static_cast<float>(code)};
            instruction.inverse_rotation = {inverse.x, inverse.y, inverse.z, inverse.w};
            instruction.settings = {world.scale, 0, static_cast<float>(mat), 0};
            result.program.push_back(instruction);
            result.part_names.push_back(name);
        }
        if (result.program.size() > authored_max_instructions)
            throw std::invalid_argument("authored SDF program exceeds 256 instructions");
    };
    if (!json.at("root").is_null())
        compile(json.at("root"), {}, 0, 0);
    unsigned stack = 0;
    for (const auto& instruction : result.program) {
        if (instruction.center_code.w < 10)
            ++stack;
        else {
            if (stack < 2)
                throw std::invalid_argument("invalid authored SDF stack");
            --stack;
        }
        if (stack > 32)
            throw std::invalid_argument("authored SDF stack exceeds 32 entries");
    }
    if (stack != (json.at("root").is_null() ? 0U : 1U))
        throw std::invalid_argument("authored SDF program has no single root");
    result.document = json.dump();
    if (json.contains("geometry_hash") &&
        json.at("geometry_hash") != hex(authored_geometry_hash(result)))
        throw std::invalid_argument("authored SDF geometry hash mismatch");
    if (json.contains("appearance_hash") &&
        json.at("appearance_hash") != hex(authored_appearance_hash(result)))
        throw std::invalid_argument("authored SDF appearance hash mismatch");
    read_authored_history(result);
    return result;
}

std::uint64_t authored_geometry_hash(const AuthoredScene& scene) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const auto& node : scene.program) {
        for (auto lane : {node.center_code, node.inverse_rotation, node.parameters})
            for (int i = 0; i < 4; ++i)
                hash_float(hash, lane[i]);
        hash_float(hash, node.settings.x);
        hash_float(hash, node.settings.y);
    }
    return hash;
}
std::uint64_t authored_appearance_hash(const AuthoredScene& scene) {
    auto hash = authored_geometry_hash(scene);
    for (const auto& node : scene.program) {
        hash_float(hash, node.settings.z);
        hash_float(hash, node.settings.w);
    }
    for (const auto& material : scene.materials) {
        validate_material(material);
        for (auto lane : {material.base_roughness, material.emission})
            for (int i = 0; i < 4; ++i)
                hash_float(hash, lane[i]);
    }
    return hash;
}
SurfaceSample authored_surface(math::Vec3 point, const AuthoredScene& scene) {
    if (scene.program.empty())
        return {};
    std::array<SurfaceSample, 32> stack{};
    std::size_t size = 0;
    for (std::size_t i = 0; i < scene.program.size(); ++i) {
        const auto& instruction = scene.program[i];
        const auto code = static_cast<ShapeCode>(static_cast<unsigned>(instruction.center_code.w));
        if (instruction.center_code.w < 10) {
            stack[size++] = {primitive_distance(point, instruction),
                             scene.materials.at(static_cast<unsigned>(instruction.settings.z)),
                             static_cast<unsigned>(i)};
        } else {
            const auto b = stack[--size];
            auto& a = stack[size - 1];
            if (code == ShapeCode::Union) {
                const float distance = std::min(a.distance, b.distance);
                if (b.distance < a.distance - 1e-6F)
                    a = b;
                a.distance = distance;
            } else if (code == ShapeCode::Subtract) {
                const float distance = std::max(a.distance, -b.distance);
                if (-b.distance > a.distance + 1e-6F) {
                    if (instruction.settings.w >= 0) {
                        a.material =
                            scene.materials.at(static_cast<unsigned>(instruction.settings.w));
                        a.part = static_cast<unsigned>(i);
                    }
                }
                a.distance = distance;
            } else if (code == ShapeCode::SmoothSubtract) {
                const float radius = instruction.settings.y;
                const float h =
                    std::clamp(.5F + .5F * (a.distance + b.distance) / radius, 0.0F, 1.0F);
                const float distance = glm::mix(-b.distance, a.distance, h) + radius * h * (1 - h);
                if (h < .5F && instruction.settings.w >= 0) {
                    a.material = scene.materials.at(static_cast<unsigned>(instruction.settings.w));
                    a.part = static_cast<unsigned>(i);
                }
                a.distance = distance;
            } else {
                const float radius = instruction.settings.y;
                const float h =
                    std::clamp(.5F + .5F * (b.distance - a.distance) / radius, 0.0F, 1.0F);
                const float distance = glm::mix(b.distance, a.distance, h) - radius * h * (1 - h);
                if (instruction.settings.w > .5F) {
                    a.material.base_roughness =
                        glm::mix(b.material.base_roughness, a.material.base_roughness, h);
                    a.material.emission = glm::mix(b.material.emission, a.material.emission, h);
                } else if (h < .5F)
                    a.material = b.material;
                if (h < .5F)
                    a.part = b.part;
                a.distance = distance;
            }
        }
    }
    auto result = stack[0];
    result.distance = std::max(result.distance, box_distance(point, math::Vec3{.499F}, 0));
    return result;
}
float authored_distance(math::Vec3 point, const AuthoredScene& scene) {
    return authored_surface(point, scene).distance;
}
std::string serialize_authored_scene(const AuthoredScene& scene) {
    auto json = Json::parse(scene.document);
    for (std::size_t i = 0; i < scene.materials.size(); ++i) {
        const auto& m = scene.materials[i];
        validate_material(m);
        json["materials"][i]["base_color"] = {m.base_roughness.x, m.base_roughness.y,
                                              m.base_roughness.z};
        json["materials"][i]["roughness"] = m.base_roughness.w;
        json["materials"][i]["emission"] = {m.emission.x, m.emission.y, m.emission.z};
    }
    json["geometry_hash"] = hex(authored_geometry_hash(scene));
    json["appearance_hash"] = hex(authored_appearance_hash(scene));
    return json.dump(2) + '\n';
}
AuthoredScene load_authored_scene(const std::filesystem::path& path) {
    const auto bytes = read_binary_file(path);
    return parse_authored_scene({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
void save_authored_scene(const std::filesystem::path& path, const AuthoredScene& scene) {
    const auto text = serialize_authored_scene(scene);
    write_binary_file(path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
std::vector<std::uint8_t> authored_program_bytes(const AuthoredScene* scene) {
    std::vector<std::uint8_t> bytes(
        16 + (scene ? scene->program.size() : 1) * sizeof(SceneInstruction), 0);
    if (scene) {
        const std::array<std::uint32_t, 4> header{static_cast<unsigned>(scene->program.size()),
                                                  static_cast<unsigned>(scene->materials.size()), 0,
                                                  0};
        std::memcpy(bytes.data(), header.data(), 16);
        std::memcpy(bytes.data() + 16, scene->program.data(),
                    scene->program.size() * sizeof(SceneInstruction));
    }
    return bytes;
}
} // namespace cubey::projects::sdf
