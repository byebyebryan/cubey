#include "terrain_water_config.h"
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
auto parse(std::vector<std::string> args) {
    args.insert(args.begin(), "terrain_water");
    std::vector<char*> argv;
    for (auto& arg : args)
        argv.push_back(arg.data());
    return cubey::projects::terrain_water::parse_terrain_water_project_config(
        static_cast<int>(argv.size()), argv.data());
}
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
void rejects(std::vector<std::string> args) {
    bool rejected = false;
    try { (void)parse(std::move(args)); }
    catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "ambiguous/conflicting producer must fail");
}
} // namespace
int main() {
    try {
        rejects({});
        rejects({"--fluid25d-backend", "external"});
        rejects({"--fluid25d-backend", "builtin", "--fluid25d-recording", "/recording.json"});
        const auto replay = parse({"--fluid25d-recording", "/recording.json"});
        require(replay.recording_path.has_value() && !replay.external_session_path,
                "recording is a separate producer, not a solver");
        const auto external = parse({"--fluid25d-backend", "external",
                                     "--fluid25d-external-session", "/session"});
        require(external.external_session_path.has_value() && !external.recording_path,
                "external attach must preserve session identity");
        const auto builtin = parse({"--fluid25d-backend", "builtin"});
        require(builtin.backend == "builtin" && !builtin.recording_path &&
                    !builtin.external_session_path, "builtin must be explicit");
        const auto fv = parse({"--fluid25d-backend", "builtin", "--fluid25d-solver", "finite-volume"});
        require(fv.simulation.solver == cubey::projects::fluid::fluid_25d::Fluid25DSolver::FiniteVolume,
                "builtin numerical method remains available");
        std::puts("Terrain Water frontend producer contracts passed");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
