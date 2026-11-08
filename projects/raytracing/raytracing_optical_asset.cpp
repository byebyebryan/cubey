#include "raytracing_optical_asset.h"
#include <cubey/asset/file_digest.h>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cubey::projects::raytracing {
void admit_optical_dragon(Mesh& mesh, const std::filesystem::path& model) {
    std::ifstream certificate(std::filesystem::path(CUBEY_RAYTRACING_ASSET_DIR) /
                              "stanford-dragon-optical.json");
    if (!certificate)
        throw std::invalid_argument("missing approved dragon optical certificate");
    const auto doc = nlohmann::json::parse(certificate);
    const auto& boundary = doc.at("serialized_boundary");
    if (doc.at("format") != "cubey.raytracing.optical-model" || doc.at("version") != 1 ||
        doc.at("model_id") != "stanford-dragon-optical-v1" ||
        !doc.at("optical_admission").get<bool>() ||
        !doc.at("one_connected_embedded_boundary_checks_passed").get<bool>() ||
        !boundary.at("closed").get<bool>() || !boundary.at("valid").get<bool>() ||
        boundary.at("connected_components") != 1 || boundary.at("self_intersection_pairs") != 0 ||
        !boundary.at("consistent_winding").get<bool>() ||
        boundary.at("non_disk_vertex_links") != 0 ||
        !boundary.at("cgal_is_outward_oriented_predicate").get<bool>() ||
        boundary.at("signed_volume").get<double>() <= 0)
        throw std::invalid_argument("unqualified dragon optical certificate");
    std::ifstream file(model, std::ios::binary | std::ios::ate);
    const auto size = file.tellg();
    if (!file || size <= 0 || size > 64 * 1024 * 1024)
        throw std::invalid_argument("invalid optical dragon file size");
    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(bytes.data(), size))
        throw std::invalid_argument("cannot read optical dragon");
    const auto digest = asset::sha256_hex(std::as_bytes(std::span{bytes}));
    if (doc.at("glb_sha256") != digest || doc.at("render_vertices") != mesh.vertices.size() ||
        boundary.at("triangles") != mesh.indices.size() / 3)
        throw std::invalid_argument(
            "dragon optics requires the exact certified model; run prepare_optical_dragon.py");
    auto& report = mesh.admission;
    report.minimum = math::Vec3(std::numeric_limits<float>::max());
    report.maximum = -report.minimum;
    for (const auto& vertex : mesh.vertices) {
        report.minimum = glm::min(report.minimum, math::Vec3(vertex.position));
        report.maximum = glm::max(report.maximum, math::Vec3(vertex.position));
    }
    report.closed = true;
    report.welded_vertices = boundary.at("active_vertices").get<std::uint32_t>();
    report.signed_volume = boundary.at("signed_volume").get<double>();
    report.certificate_id = doc.at("model_id").get<std::string>();
    report.model_sha256 = digest;
}
} // namespace cubey::projects::raytracing
