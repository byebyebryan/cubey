#include "sdf_history.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <numbers>
#include <set>
#include <stdexcept>

namespace cubey::projects::sdf {
namespace {
using Json = nlohmann::json;
constexpr double opening_hold = .65;
double history_duration(const AuthoredScene& scene) {
    double result = opening_hold;
    for (const auto& step : scene.history)
        result += step.seconds;
    return result;
}
Json reveal(const Json& node, const std::set<std::string>& visible) {
    if (node.is_null())
        return nullptr;
    if (!node.contains("children"))
        return visible.contains(node.at("name").get<std::string>()) ? node : Json(nullptr);
    auto result = node;
    auto children = Json::array();
    const bool subtract = node.at("type") == "subtract" || node.at("type") == "smooth-subtract";
    for (const auto& child : node.at("children")) {
        auto part = reveal(child, visible);
        if (subtract && children.empty() && part.is_null())
            return nullptr; // A cutter cannot exist before its retained body.
        if (!part.is_null())
            children.push_back(std::move(part));
    }
    if (children.empty())
        return nullptr;
    if (children.size() == 1) {
        // Keep the local transform/material without inventing a unary Boolean.
        result["type"] = "group";
        result.erase("cut_material");
        result.erase("radius");
        result.erase("blend_materials");
    }
    result["children"] = std::move(children);
    return result;
}
} // namespace

AuthoredScene authored_history_scene(const AuthoredScene& scene, std::size_t count) {
    if (scene.history.empty() || count > scene.history.size())
        throw std::invalid_argument("authored history prefix outside the saved sequence");
    if (count == scene.history.size())
        return scene;
    auto json = Json::parse(serialize_authored_scene(scene));
    json.erase("history");
    json.erase("geometry_hash");
    json.erase("appearance_hash");
    std::set<std::string> visible;
    for (std::size_t i = 0; i < count; ++i)
        visible.insert(scene.history[i].nodes.begin(), scene.history[i].nodes.end());
    json["root"] = reveal(json.at("root"), visible);
    return parse_authored_scene(json.dump());
}

void read_authored_history(AuthoredScene& scene) {
    const auto json = Json::parse(scene.document);
    if (!json.contains("history"))
        return;
    const auto& history = json.at("history");
    if (!history.is_array() || history.empty() || history.size() > scene.leaf_count)
        throw std::invalid_argument("authored history must reveal every primitive exactly once");
    std::map<std::string, bool> primitives;
    std::function<void(const Json&, bool)> collect = [&](const Json& node, bool negative) {
        if (node.is_null())
            return;
        if (!node.contains("children")) {
            primitives.emplace(node.at("name").get<std::string>(), negative);
            return;
        }
        std::size_t index = 0;
        for (const auto& child : node.at("children")) {
            collect(child, negative != ((node.at("type") == "subtract" ||
                                         node.at("type") == "smooth-subtract") &&
                                        index == 1));
            ++index;
        }
    };
    collect(json.at("root"), false);
    for (const auto& item : history) {
        AuthoredStep step;
        if (item.contains("node") == item.contains("nodes"))
            throw std::invalid_argument("history item needs exactly one of node or nodes");
        step.nodes = item.contains("node")
                         ? std::vector<std::string>{item.at("node").get<std::string>()}
                         : item.at("nodes").get<std::vector<std::string>>();
        step.label = item.at("label").get<std::string>();
        step.seconds = item.value("seconds", .65);
        if (step.nodes.empty() || step.label.empty() || step.label.size() > 160 ||
            !std::isfinite(step.seconds) || step.seconds < .2 || step.seconds > 5)
            throw std::invalid_argument("invalid authored history node, label or duration");
        std::optional<bool> negative;
        for (const auto& name : step.nodes) {
            const auto found = primitives.find(name);
            if (found == primitives.end())
                throw std::invalid_argument("history must reveal each primitive exactly once");
            if (negative && *negative != found->second)
                throw std::invalid_argument(
                    "history operation mixes additive and subtractive leaves");
            negative = found->second;
            if (found->second) {
                const auto part = std::find(scene.part_names.begin(), scene.part_names.end(), name);
                step.cutters.push_back(
                    scene.program.at(static_cast<std::size_t>(part - scene.part_names.begin())));
            }
            primitives.erase(found);
        }
        scene.history.push_back(std::move(step));
    }
    if (!primitives.empty())
        throw std::invalid_argument("authored history must reveal every primitive exactly once");
    // A subtraction is visible as soon as its body is visible. Requiring the
    // first body leaf before the first tool leaf at every subtraction proves
    // that every prefix participates immediately, including nested tools.
    // This avoids serializing/recompiling all N prefixes on every scene load.
    std::map<std::string, std::size_t> order;
    for (std::size_t i = 0; i < scene.history.size(); ++i)
        for (const auto& name : scene.history[i].nodes)
            order.emplace(name, i);
    std::function<std::size_t(const Json&)> first = [&](const Json& node) {
        if (!node.contains("children"))
            return order.at(node.at("name").get<std::string>());
        const auto& children = node.at("children");
        auto earliest = first(children.at(0));
        const bool subtract = node.at("type") == "subtract" || node.at("type") == "smooth-subtract";
        for (std::size_t i = 1; i < children.size(); ++i) {
            const auto next = first(children.at(i));
            if (subtract && next < earliest)
                throw std::invalid_argument("authored history reveals a cutter before its body");
            earliest = std::min(earliest, next);
        }
        return earliest;
    };
    first(json.at("root"));
}

AuthoredHistorySample sample_authored_history(const TimelineConfig& config,
                                              const AuthoredScene& scene, double seconds,
                                              std::uint32_t debug) {
    validate_timeline(config);
    if (scene.history.empty() || !std::isfinite(seconds))
        throw std::invalid_argument("authored construction needs a saved history and finite time");
    const double t = std::clamp(seconds, 0.0, config.duration);
    const double history_time = t / config.construction_seconds * history_duration(scene);
    AuthoredHistorySample result;
    result.timeline.debug = debug;
    double begin = opening_hold;
    for (std::size_t i = 0; i < scene.history.size(); ++i) {
        const auto& step = scene.history[i];
        if (history_time < begin)
            break;
        const double phase = (history_time - begin) / step.seconds;
        if (!step.cutters.empty() && phase < .35) {
            result.cutters = step.cutters;
            result.timeline.cutter_opacity =
                .45F * static_cast<float>(std::sin(std::numbers::pi_v<double> *
                                                   std::clamp(phase / .35, 0.0, 1.0)));
            break;
        }
        result.timeline.prefix = i + 1;
        begin += step.seconds;
    }
    if (config.motion == "presentation")
        result.timeline.yaw =
            2 * std::numbers::pi_v<float> * config.orbit_turns *
            static_cast<float>(std::clamp((t - config.construction_seconds) /
                                              (config.duration - config.construction_seconds),
                                          0.0, 1.0));
    return result;
}

double authored_history_seek(const TimelineConfig& config, const AuthoredScene& scene,
                             std::size_t count) {
    validate_timeline(config);
    if (scene.history.empty() || count > scene.history.size())
        throw std::invalid_argument("authored history seek outside the saved sequence");
    if (count == 0)
        return 0;
    double t = opening_hold;
    for (std::size_t i = 0; i < count - 1; ++i)
        t += scene.history[i].seconds;
    t += .7 * scene.history[count - 1].seconds;
    return t / history_duration(scene) * config.construction_seconds;
}
} // namespace cubey::projects::sdf
