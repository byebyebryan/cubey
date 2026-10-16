// Standalone study adapter around the UNCHANGED production CPU oracle.
// No new numerical method, app configuration, or CMake target is introduced.
#include "fluid_25d_finite_volume_oracle.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace f25 = cubey::projects::fluid::fluid_25d;

namespace {
static_assert(sizeof(float) == 4 && std::endian::native == std::endian::little,
              "study field format requires little-endian float32");
struct Options {
    std::filesystem::path bed;
    std::filesystem::path output;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float dx = 30.0F;
    float dt = 2.0F;
    std::uint32_t substeps = 16;
    float damping = 0.15F;
    float rain_mm_h = 12.0F;
    float initial_depth = 0.0F;
    bool open = true;
    std::vector<double> times;
};

std::uint32_t integer(const std::string& value) {
    std::size_t used = 0;
    const auto parsed = std::stoull(value, &used);
    if (used != value.size() || parsed == 0 || parsed > 100000) {
        throw std::invalid_argument("invalid positive integer");
    }
    return static_cast<std::uint32_t>(parsed);
}

float number(const std::string& value) {
    std::size_t used = 0;
    const float parsed = std::stof(value, &used);
    if (used != value.size() || !std::isfinite(parsed)) {
        throw std::invalid_argument("invalid finite number");
    }
    return parsed;
}

Options parse(int argc, char** argv) {
    Options o;
    std::map<std::string, std::string> values;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc || !values.emplace(argv[i], argv[i + 1]).second) {
            throw std::invalid_argument("expected unique --key value pairs");
        }
    }
    const std::vector<std::string> allowed{
        "--bed",      "--output",  "--width",     "--height",        "--dx",       "--dt",
        "--substeps", "--damping", "--rain-mm-h", "--initial-depth", "--boundary", "--times"};
    for (const auto& [key, value] : values) {
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
            throw std::invalid_argument("unknown argument: " + key);
        }
    }
    o.bed = values.at("--bed");
    o.output = values.at("--output");
    o.width = integer(values.at("--width"));
    o.height = integer(values.at("--height"));
    if (values.contains("--dx"))
        o.dx = number(values.at("--dx"));
    if (values.contains("--dt"))
        o.dt = number(values.at("--dt"));
    if (values.contains("--substeps"))
        o.substeps = integer(values.at("--substeps"));
    if (values.contains("--damping"))
        o.damping = number(values.at("--damping"));
    if (values.contains("--rain-mm-h"))
        o.rain_mm_h = number(values.at("--rain-mm-h"));
    if (values.contains("--initial-depth"))
        o.initial_depth = number(values.at("--initial-depth"));
    if (values.contains("--boundary")) {
        const auto& boundary = values.at("--boundary");
        if (boundary != "open-dry" && boundary != "closed") {
            throw std::invalid_argument("boundary must be open-dry or closed");
        }
        o.open = boundary == "open-dry";
    }
    std::stringstream input(values.at("--times"));
    for (std::string item; std::getline(input, item, ',');) {
        const double t = number(item);
        if (t <= 0 || t > 86400 || t != std::round(t) || !(o.dt > 0) || t / o.dt > 1000000 ||
            std::abs(t / o.dt - std::round(t / o.dt)) > 1e-7 ||
            (!o.times.empty() && t <= o.times.back())) {
            throw std::invalid_argument("times must increase and be exact fixed-step multiples");
        }
        o.times.push_back(t);
    }
    if (o.times.empty() || o.initial_depth < 0 || o.rain_mm_h < 0 || o.damping < 0) {
        throw std::invalid_argument("invalid study forcing or empty observation schedule");
    }
    if (static_cast<std::uint64_t>(o.width) * o.height > 4194304U) {
        throw std::invalid_argument("study adapter is bounded to 4 million cells");
    }
    return o;
}

std::vector<float> read_bed(const Options& o) {
    const auto count = static_cast<std::size_t>(o.width) * o.height;
    if (std::filesystem::file_size(o.bed) != count * sizeof(float)) {
        throw std::invalid_argument("bed must be exactly width*height native float32 values");
    }
    std::vector<float> result(count);
    std::ifstream input(o.bed, std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(result.data()),
                    static_cast<std::streamsize>(count * sizeof(float))) ||
        !std::all_of(result.begin(), result.end(), [](float v) { return std::isfinite(v); })) {
        throw std::invalid_argument("bed read failed or contains nonfinite elevation");
    }
    return result;
}

void save_field(const std::filesystem::path& path, const std::vector<float>& values) {
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() * sizeof(float)));
    if (!output)
        throw std::runtime_error("field write failed");
}

void snapshot(const Options& o, const f25::Fluid25DFiniteVolumeOracle& oracle, double time) {
    const auto name = std::to_string(static_cast<std::uint64_t>(std::llround(time)));
    save_field(o.output / (name + "-depth.f32"), oracle.water_depth_m());
    std::vector<float> ux;
    std::vector<float> uy;
    ux.reserve(oracle.velocity_m_per_s().size());
    uy.reserve(oracle.velocity_m_per_s().size());
    for (const auto v : oracle.velocity_m_per_s()) {
        ux.push_back(v.x_m_per_s);
        uy.push_back(v.y_m_per_s);
    }
    save_field(o.output / (name + "-ux.f32"), ux);
    save_field(o.output / (name + "-uy.f32"), uy);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto o = parse(argc, argv);
        f25::Fluid25DConfig config;
        config.grid_width = o.width;
        config.grid_height = o.height;
        config.cell_size_m = o.dx;
        config.fixed_delta_seconds = o.dt;
        config.simulation_substeps = o.substeps;
        config.flow_damping_per_second = o.damping;
        config.solver = f25::Fluid25DSolver::FiniteVolume;
        config.scenario = f25::Fluid25DScenario::DryBed;
        f25::validate_fluid_25d_config(config);
        f25::Fluid25DScenarioData scenario;
        scenario.width = o.width;
        scenario.height = o.height;
        scenario.cell_size_m = o.dx;
        scenario.terrain_height_m = read_bed(o);
        const auto count = scenario.terrain_height_m.size();
        scenario.initial_water_depth_m.assign(count, o.initial_depth);
        // Match the app's mm/hour -> float32 m/second source construction.
        const float source_rate =
            f25::fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(o.rain_mm_h);
        scenario.source_depth_rate_m_per_s.assign(count, source_rate);
        scenario.sink_depth_rate_m_per_s.assign(count, 0.0F);
        scenario.boundary_outflow_face_mask.assign(count, 0U);
        if (o.open)
            f25::open_fluid_25d_all_outward_boundary_faces(scenario);
        f25::Fluid25DFiniteVolumeOracle oracle(config, scenario);
        if (!std::filesystem::create_directory(o.output)) {
            throw std::runtime_error("refusing to overwrite existing output directory");
        }
        std::ofstream metadata(o.output / "protocol.csv");
        metadata << std::setprecision(17)
                 << "width,height,dx_m,dt_s,substeps,gravity_m_s2,damping_per_s,rain_m_s,initial_"
                    "depth_m,open_dry\n"
                 << o.width << ',' << o.height << ',' << o.dx << ',' << o.dt << ',' << o.substeps
                 << ',' << config.gravity_m_per_s2 << ',' << o.damping << ',' << source_rate << ','
                 << o.initial_depth << ',' << o.open << '\n';
        std::ofstream ledger(o.output / "ledger.csv");
        ledger << std::setprecision(17)
               << "time_s,initial_m3,source_m3,sink_m3,outflow_m3,stored_m3,residual_m3,max_step_"
                  "residual_m3,max_cfl\n";
        const double initial = oracle.total_water_volume_m3();
        double source = 0;
        double sink = 0;
        double outflow = 0;
        double max_step_residual = 0;
        double max_cfl = 0;
        std::size_t next = 0;
        const auto final_step = static_cast<std::uint64_t>(std::llround(o.times.back() / o.dt));
        for (std::uint64_t step = 1; step <= final_step; ++step) {
            const auto accounting = oracle.step();
            source += accounting.source_volume_m3;
            sink += accounting.sink_volume_m3;
            outflow += accounting.boundary_outflow_volume_m3;
            max_step_residual =
                std::max(max_step_residual, std::abs(accounting.conservation_error_m3()));
            max_cfl = std::max(max_cfl, static_cast<double>(oracle.last_cfl_number()));
            const double time = static_cast<double>(step) * static_cast<double>(o.dt);
            if (next < o.times.size() && std::abs(time - o.times[next]) < 1e-7) {
                const double stored = oracle.total_water_volume_m3();
                ledger << time << ',' << initial << ',' << source << ',' << sink << ',' << outflow
                       << ',' << stored << ',' << stored - initial - source + sink + outflow << ','
                       << max_step_residual << ',' << max_cfl << '\n';
                ledger.flush();
                snapshot(o, oracle, time);
                std::cout << "completed_time_s=" << time << " stored_m3=" << stored
                          << " outflow_m3=" << outflow << '\n'
                          << std::flush;
                ++next;
            }
        }
        if (!ledger || !metadata || next != o.times.size())
            throw std::runtime_error("incomplete output");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
