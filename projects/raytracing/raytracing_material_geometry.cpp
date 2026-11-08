#include "raytracing_material_geometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace cubey::projects::raytracing {
namespace {
using Cell = std::array<int, 3>;
struct CellHash {
    std::size_t operator()(const Cell& c) const {
        std::size_t h = 0;
        for (int value : c)
            h ^= std::hash<int>{}(value) + 0x9e3779b9U + (h << 6) + (h >> 2);
        return h;
    }
};
} // namespace
Mesh prepare_workshop_mesh(const render::PrimitiveMeshData<render::PbrVertex>& source) {
    if (source.vertices.empty() || source.indices.size() % 3)
        throw std::invalid_argument("invalid workshop mesh");
    Mesh mesh;
    auto& report = mesh.admission;
    const float huge = std::numeric_limits<float>::max();
    report.minimum = math::Vec3(huge);
    report.maximum = math::Vec3(-huge);
    for (const auto& v : source.vertices) {
        report.minimum = glm::min(report.minimum, v.position);
        report.maximum = glm::max(report.maximum, v.position);
    }
    const float epsilon = 1e-7F * std::max(1.F, glm::length(report.maximum - report.minimum));
    std::unordered_map<Cell, std::vector<std::uint32_t>, CellHash> cells;
    std::vector<math::Vec3> canonical;
    std::vector<std::uint32_t> topology;
    for (const auto& v : source.vertices) {
        Cell cell;
        for (int k = 0; k < 3; ++k)
            cell[static_cast<std::size_t>(k)] =
                static_cast<int>(std::floor(v.position[k] / epsilon));
        std::uint32_t found = UINT32_MAX;
        for (int z = -1; z <= 1 && found == UINT32_MAX; ++z)
            for (int y = -1; y <= 1 && found == UINT32_MAX; ++y)
                for (int x = -1; x <= 1 && found == UINT32_MAX; ++x) {
                    const auto it = cells.find({cell[0] + x, cell[1] + y, cell[2] + z});
                    if (it == cells.end())
                        continue;
                    for (auto candidate : it->second)
                        if (glm::length(canonical[candidate] - v.position) <= epsilon) {
                            found = candidate;
                            break;
                        }
                }
        if (found == UINT32_MAX) {
            found = static_cast<std::uint32_t>(canonical.size());
            canonical.push_back(v.position);
            cells[cell].push_back(found);
        }
        const auto position = canonical[found];
        const double snap = glm::length(glm::dvec3(position) - glm::dvec3(v.position));
        if (snap > 0)
            ++report.snapped_positions;
        report.maximum_position_snap = std::max(report.maximum_position_snap, snap);
        topology.push_back(found);
        mesh.vertices.push_back({{position, 1}, {v.normal, 0}});
    }
    report.welded_vertices = static_cast<std::uint32_t>(canonical.size());
    struct Edge {
        int count = 0, balance = 0;
    };
    std::map<std::pair<std::uint32_t, std::uint32_t>, Edge> edges;
    for (std::size_t j = 0; j < source.indices.size(); j += 3) {
        std::uint32_t a = source.indices[j], b = source.indices[j + 1], c = source.indices[j + 2];
        const auto pa = math::Vec3(mesh.vertices.at(a).position),
                   pb = math::Vec3(mesh.vertices.at(b).position),
                   pc = math::Vec3(mesh.vertices.at(c).position);
        if (topology[a] == topology[b] || topology[b] == topology[c] ||
            topology[c] == topology[a]) {
            ++report.removed_triangles;
            continue;
        }
        const auto normal =
            math::Vec3(mesh.vertices[a].normal + mesh.vertices[b].normal + mesh.vertices[c].normal);
        const auto cross = glm::cross(pb - pa, pc - pa);
        if (glm::dot(cross, cross) <= 0)
            throw std::invalid_argument("zero-area workshop boundary");
        if (glm::dot(cross, normal) < 0)
            std::swap(b, c);
        mesh.indices.insert(mesh.indices.end(), {a, b, c});
        const auto x = glm::dvec3(mesh.vertices[a].position),
                   y = glm::dvec3(mesh.vertices[b].position),
                   z = glm::dvec3(mesh.vertices[c].position);
        report.signed_volume += glm::dot(x, glm::cross(y, z)) / 6;
        for (auto pair : {std::pair{topology[a], topology[b]}, std::pair{topology[b], topology[c]},
                          std::pair{topology[c], topology[a]}}) {
            const int sign = pair.first < pair.second ? 1 : -1;
            if (sign < 0)
                std::swap(pair.first, pair.second);
            auto& edge = edges[pair];
            ++edge.count;
            edge.balance += sign;
        }
    }
    report.closed = report.signed_volume > 1e-12 &&
                    std::all_of(edges.begin(), edges.end(), [](const auto& entry) {
                        return entry.second.count == 2 && entry.second.balance == 0;
                    });
    if (!report.closed)
        throw std::invalid_argument(
            "workshop boundary is not closed, consistently wound and positive-volume");
    return mesh;
}
std::vector<WorkshopVolume> workshop_volumes(const Scene& scene) {
    std::vector<WorkshopVolume> result;
    const float huge = std::numeric_limits<float>::max();
    for (const auto& instance : scene.instances) {
        const auto& mesh = scene.meshes.at(instance.mesh);
        if (!mesh.admission.closed && instance.recipe_role != Instance::RecipeRole::Support)
            throw std::invalid_argument("unqualified workshop optical boundary");
        WorkshopVolume body{math::Vec3(huge), math::Vec3(-huge), instance.material,
                            instance.recipe_role};
        if (instance.recipe_role == Instance::RecipeRole::Support) {
            for (const auto& vertex : mesh.vertices) {
                const auto world = math::Vec3(instance.transform * vertex.position);
                body.minimum = glm::min(body.minimum, world);
                body.maximum = glm::max(body.maximum, world);
            }
            result.push_back(body);
            continue;
        }
        for (int corner = 0; corner < 8; ++corner) {
            math::Vec3 p;
            for (int k = 0; k < 3; ++k)
                p[k] = (corner & (1 << k)) ? mesh.admission.maximum[k] : mesh.admission.minimum[k];
            const auto world = math::Vec3(instance.transform * math::Vec4(p, 1));
            body.minimum = glm::min(body.minimum, world);
            body.maximum = glm::max(body.maximum, world);
        }
        result.push_back(body);
    }
    return result;
}
std::string workshop_route(const Config& c) {
    if (sampled_dragon_scene(c))
        return "dragon";
    return c.optical_pair != "none"  ? "optical-pair"
           : c.material == "gallery" ? "gallery"
                                     : c.specimen;
}
std::vector<glm::uvec4> optical_edge_neighbors(const Mesh& mesh) {
    // Exact-position identity only; the certificate already binds this boundary.
    std::vector<std::uint32_t> order(mesh.vertices.size()), welded(order.size());
    std::iota(order.begin(), order.end(), 0U);
    auto key = [&](std::uint32_t i) {
        const auto p = mesh.vertices[i].position;
        return std::tuple{p.x, p.y, p.z};
    };
    std::sort(order.begin(), order.end(), [&](auto a, auto b) { return key(a) < key(b); });
    std::uint32_t identity = 0;
    for (std::size_t i = 0; i < order.size(); ++i) {
        if (i && key(order[i]) != key(order[i - 1]))
            ++identity;
        welded[order[i]] = identity;
    }
    struct Edge {
        std::uint32_t a, b, face, corner;
    };
    std::vector<Edge> edges;
    edges.reserve(mesh.indices.size());
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3)
        for (std::uint32_t corner = 0; corner < 3; ++corner) {
            auto a = welded.at(mesh.indices[i + (corner + 1) % 3]);
            auto b = welded.at(mesh.indices[i + (corner + 2) % 3]);
            if (a > b)
                std::swap(a, b);
            edges.push_back({a, b, static_cast<std::uint32_t>(i / 3), corner});
        }
    std::sort(edges.begin(), edges.end(),
              [](auto a, auto b) { return std::tie(a.a, a.b) < std::tie(b.a, b.b); });
    std::vector<glm::uvec4> neighbors(mesh.indices.size() / 3, glm::uvec4(0xffffffffU));
    for (std::size_t i = 0; i < edges.size(); i += 2) {
        if (i + 1 == edges.size() || edges[i].a != edges[i + 1].a || edges[i].b != edges[i + 1].b ||
            (i + 2 < edges.size() && edges[i].a == edges[i + 2].a && edges[i].b == edges[i + 2].b))
            throw std::invalid_argument("optical adjacency requires exactly two faces per edge");
        neighbors[edges[i].face][static_cast<int>(edges[i].corner)] = edges[i + 1].face;
        neighbors[edges[i + 1].face][static_cast<int>(edges[i + 1].corner)] = edges[i].face;
    }
    return neighbors;
}
void validate_workshop_view(std::span<const WorkshopVolume> volumes,
                            std::span<const Scene::Route> routes, const WorkshopState& state,
                            const Config& config, math::Vec3 eye) {
    const auto name = workshop_route(config);
    const auto route =
        std::find_if(routes.begin(), routes.end(), [&](const auto& r) { return r.name == name; });
    if (route == routes.end())
        throw std::invalid_argument("unprepared workshop view");
    auto glass = [&](std::size_t index) {
        if (volumes[index].recipe_role == Instance::RecipeRole::Support)
            return false;
        const auto material = volumes[index].recipe_role == Instance::RecipeRole::Selected
                                  ? state.selected
                                  : volumes[index].material;
        return state.recipes.at(material).optics.settings.y > 0;
    };
    for (std::size_t i = route->first; i < route->first + route->count; ++i) {
        const bool optical_body = config.scene == "dragon-optics" &&
                                  volumes[i].recipe_role == Instance::RecipeRole::Selected;
        if ((glass(i) || optical_body) &&
            glm::all(glm::greaterThanEqual(eye, volumes[i].minimum)) &&
            glm::all(glm::lessThanEqual(eye, volumes[i].maximum)))
            throw std::invalid_argument("camera overlaps the conservative bounds of a glass body");
        for (std::size_t j = i + 1; j < route->first + route->count; ++j) {
            if (!(glass(i) || glass(j)))
                continue;
            const bool overlap =
                glm::all(glm::lessThanEqual(volumes[i].minimum, volumes[j].maximum)) &&
                glm::all(glm::lessThanEqual(volumes[j].minimum, volumes[i].maximum));
            if (overlap)
                throw std::invalid_argument(
                    "nested or overlapping workshop optical bodies are unsupported");
        }
    }
}
nlohmann::json mesh_admission_identity(const Mesh& mesh) {
    const auto& r = mesh.admission;
    return {{"closed", r.closed},
            {"optical_volume_qualified", r.closed},
            {"certificate_id", r.certificate_id},
            {"model_sha256", r.model_sha256},
            {"signed_volume", r.signed_volume},
            {"welded_topology_vertices", r.welded_vertices},
            {"removed_degenerate_triangles", r.removed_triangles},
            {"snapped_positions", r.snapped_positions},
            {"maximum_position_snap", r.maximum_position_snap},
            {"minimum", {r.minimum.x, r.minimum.y, r.minimum.z}},
            {"maximum", {r.maximum.x, r.maximum.y, r.maximum.z}}};
}
} // namespace cubey::projects::raytracing
