#include "fluid_25d_terrain_demo.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace cubey::projects::fluid::fluid_25d;
using Json = nlohmann::json;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class Function> void throws(Function function) {
    bool failed = false;
    try {
        function();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "expected invalid operation to fail");
}
void write(const std::filesystem::path& path, const Json& value) {
    std::ofstream file(path);
    file << value.dump();
    require(bool(file), "fixture write failed");
}
Json read(const std::filesystem::path& path) {
    std::ifstream file(path);
    return Json::parse(file);
}

void contracts(const std::filesystem::path& root) {
    const std::string id = "mountain-patch-512-32m";
    Json entry{{"id", id},
               {"site", "mountain"},
               {"site_label", "Mountain valley"},
               {"configuration", "patch-512-32m"},
               {"label", "Coarse"},
               {"extent", "same"},
               {"width", 512},
               {"height", 512},
               {"spacing_m", 32},
               {"source_spacing_m", 30},
               {"case_dir", "/study/cases/" + id},
               {"surface_grid", "/study/cases/" + id + "/surface-grid.json"},
               {"available", true},
               {"error", ""},
               {"input_sha256", std::string(64, 'a')},
               {"solver_bed_sha256", std::string(64, 'b')},
               {"protocol_sha256", std::string(64, 'c')},
               {"climate_manifest_sha256", std::string(64, 'd')},
               {"duration_s", 72000},
               {"rain_mm_per_hour", 512}};
    Json catalogue{{"schema", kFluid25DTerrainDemoSchema},
                   {"launch_id", "owned"},
                   {"initial_case", id},
                   {"cases", Json::array({entry})}};
    Json status{{"schema", kFluid25DTerrainDemoSchema},
                {"launch_id", "owned"},
                {"request_id", 0},
                {"phase", "active"},
                {"case_id", id},
                {"session_name", "generation-00000000000000000000"},
                {"session_id", "first"},
                {"error", ""},
                {"published_unix_s", 123},
                {"worker_available", true}};
    write(root / "catalog.json", catalogue);
    write(root / "status.json", status);
    {
        Fluid25DTerrainDemoControl control(root);
        require(control.cases().size() == 1 && !control.busy(),
                "initial catalogue/control incorrect");
        throws([&] { Fluid25DTerrainDemoControl competing(root); });
        throws([&] { control.request("../other", 512, 60); });
        throws([&] { control.request(id, -1, 60); });
        throws([&] { control.request(id, 512, 0); });
        control.request(id, 256, 30);
        require(control.busy(), "request was not pending");
        const auto request = read(root / "request.json");
        require(request.at("rain_mm_per_hour") == 256 && request.at("request_id") == 1,
                "switch did not preserve explicit settings");
        throws([&] { control.request(id, 512, 60); });
        throws([&] { control.confirm_installed(true); });
        status["request_id"] = 1;
        status["phase"] = "ready";
        status["session_name"] = "generation-00000000000000000001";
        status["session_id"] = "second";
        write(root / "status.json", status);
        require(control.poll() && control.busy(), "ready prematurely completed replacement");
        require(control.ready_session_directory().filename() == status.at("session_name"),
                "wrong session directory");
        control.confirm_installed(true);
        require(read(root / "installed.json").at("session_id") == "second",
                "wrong installation identity");
        status["phase"] = "active";
        write(root / "status.json", status);
        control.poll();
        require(!control.busy(), "installed replacement still busy");
        status["session_name"] = "../unsafe";
        write(root / "status.json", status);
        throws([&] { control.poll(); });
        status["session_name"] = "generation-00000000000000000001";
        status["launch_id"] = "stale";
        write(root / "status.json", status);
        throws([&] { control.poll(); });
    }
    status["launch_id"] = "owned";
    write(root / "status.json", status);
    catalogue["cases"][0]["width"] = 2049;
    write(root / "catalog.json", catalogue);
    throws([&] { Fluid25DTerrainDemoControl invalid(root); });
    catalogue["cases"][0] = entry;
    // New catalogue-selected names work without a renderer allowlist edit.
    const std::string additional_id = "basin-crop-patch-512-32m";
    auto additional = entry;
    additional["id"] = additional_id;
    additional["site"] = "basin-crop";
    additional["site_label"] = "Basin crop";
    additional["case_dir"] = "/study/cases/" + additional_id;
    additional["surface_grid"] = "/study/cases/" + additional_id + "/surface-grid.json";
    catalogue["cases"].push_back(additional);
    write(root / "catalog.json", catalogue);
    {
        Fluid25DTerrainDemoControl expanded(root);
        require(expanded.cases().size() == 2, "catalogued extra site rejected");
    }
    for (const std::string unsafe : {"../basin", "-basin", "basin-", "basin--crop", "Basin"}) {
        catalogue["cases"][1]["site"] = unsafe;
        catalogue["cases"][1]["id"] = unsafe + "-patch-512-32m";
        write(root / "catalog.json", catalogue);
        throws([&] { Fluid25DTerrainDemoControl invalid_site(root); });
    }
    catalogue["cases"] = Json::array({entry});
    catalogue["cases"].push_back(entry);
    write(root / "catalog.json", catalogue);
    throws([&] { Fluid25DTerrainDemoControl duplicate(root); });
}

void helpers(const std::filesystem::path& root) {
    require(fluid_25d_demo_restart_allowed(false, false, true), "healthy restart denied");
    require(!fluid_25d_demo_restart_allowed(false, true, true), "live command raced by restart");
    require(fluid_25d_demo_restart_allowed(false, true, false), "dead command blocked recovery");
    require(!fluid_25d_demo_restart_allowed(true, true, false),
            "pending restart raced by recovery");
    require(fluid_25d_demo_time_label(72000) == "20h 0m 0s", "hours wrapped");
    require(fluid_25d_demo_time_label(-1) == "unknown", "invalid time accepted");
    Fluid25DDemoPaceMeter meter;
    meter.sample(0, 0, true);
    meter.sample(.5, 30, true);
    require(!meter.achieved(), "insufficient pace window accepted");
    meter.sample(1, 60, true);
    require(meter.achieved() && std::abs(*meter.achieved() - 60) < 1e-9,
            "pace measurement incorrect");
    meter.sample(2, 60, false);
    require(!meter.achieved(), "paused solver reported running pace");
    meter.sample(3, 0, true);
    meter.sample(4, 0, true);
    require(meter.achieved() && *meter.achieved() == 0, "stalled source reported progress");
    Fluid25DDemoPaceMeter fast_meter;
    for (int i = 0; i <= 10000; ++i)
        fast_meter.sample(i / 5000., i / 5000. * 60, true);
    require(fast_meter.achieved() && std::abs(*fast_meter.achieved() - 60) < 1e-9,
            "uncapped render rate lost its pace window");
    Json script{
        {"schema", kFluid25DTerrainDemoSchema},
        {"actions", Json::array({Json{{"kind", "switch"},
                                      {"after_s", 1},
                                      {"case_id", "mountain-patch-512-32m"},
                                      {"rain_mm_per_hour", 512},
                                      {"pacing", 60}},
                                 Json{{"kind", "resume"}, {"after_s", 1}},
                                 Json{{"kind", "set-rain"}, {"after_s", 1}, {"value", 0}}})}};
    write(root / "script.json", script);
    require(fluid_25d_load_terrain_demo_script(root / "script.json").size() == 3,
            "script not decoded");
    script["actions"][0]["after_s"] = -1;
    write(root / "script.json", script);
    throws([&] { fluid_25d_load_terrain_demo_script(root / "script.json"); });
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("cubey-fluid-terrain-demo-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        require(std::filesystem::create_directory(root), "fresh fixture directory failed");
        contracts(root);
        helpers(root);
        std::filesystem::remove_all(root);
        std::cout << "terrain demo contracts PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (fixture retained at " << root << ")\n";
        return 1;
    }
}
