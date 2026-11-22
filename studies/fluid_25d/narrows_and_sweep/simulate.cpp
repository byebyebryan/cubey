#include "fluid_25d_finite_volume_oracle.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <span>
#include <stdexcept>
#include <vector>

using namespace cubey::projects::fluid::fluid_25d;

namespace {

constexpr unsigned display_width = 256;
constexpr unsigned display_height = 96;
constexpr unsigned samples_per_metre = 2;
constexpr unsigned width = display_width * samples_per_metre;
constexpr unsigned height = display_height * samples_per_metre;
constexpr unsigned cell_count = width * height;
constexpr unsigned snapshots = 361;
constexpr unsigned substeps = 32;
constexpr float spacing = 0.5F;
constexpr float snapshot_delta = 0.25F;
constexpr float base_flow = 7.0F;
constexpr float peak_flow = 119.0F;
constexpr float rise_seconds = 4.0F;
constexpr float hold_seconds = 0.0F;
constexpr float fall_seconds = 14.0F;
constexpr float damping_per_second = 0.003F;
constexpr float initial_stage = 2.0F;
constexpr float pi = std::numbers::pi_v<float>;

struct Knot {
    float x;
    float y;
};

float ease(float t) {
    t = std::clamp(t, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

float bump(float x, float middle, float radius) {
    const float q = (x - middle) / radius;
    return std::abs(q) < 1.0F ? 0.5F * (1.0F + std::cos(pi * q)) : 0.0F;
}

float centerline(float x) {
    constexpr Knot sweep[] = {{0, 48},   {18, 48},  {53, 60},  {95, 34}, {123, 39},
                              {168, 66}, {210, 39}, {236, 43}, {255, 43}};
    const auto knots = std::span(sweep);
    for (std::size_t i = 1; i < knots.size(); ++i) {
        if (x <= knots[i].x) {
            return std::lerp(knots[i - 1].y, knots[i].y,
                             ease((x - knots[i - 1].x) / (knots[i].x - knots[i - 1].x)));
        }
    }
    return knots.back().y;
}

struct Section {
    float center;
    float tangent;
    float normal_to_y;
    float near_half_width;
    float far_half_width;
    float floor;
    float near_crest;
    float far_crest;
};

Section section(float x) {
    const float center = centerline(x);
    const float tangent = centerline(x + 0.5F) - centerline(x - 0.5F);
    const float normal_to_y = std::hypot(1.0F, tangent);
    const float inlet = 3.4F * (1.0F - ease((x - 20.0F) / 29.0F));
    float near_half_width = 6.1F + inlet;
    float far_half_width = 6.1F + inlet;
    const float narrows = ease((x - 55.0F) / 28.0F) * ease((128.0F - x) / 26.0F);
    near_half_width += -1.8F * narrows + 1.7F * bump(x, 158.0F, 46.0F);
    far_half_width +=
        -1.8F * narrows + 1.0F * bump(x, 143.0F, 31.0F) - 0.7F * bump(x, 181.0F, 24.0F);
    const float floor = 4.40F - 0.00055F * (x - 18.0F);
    return {center,
            tangent,
            normal_to_y,
            near_half_width,
            far_half_width,
            floor,
            center - 1.58F * near_half_width * normal_to_y,
            center + 1.58F * far_half_width * normal_to_y};
}

float relief(const Section& s, float x, float y) {
    const float d = (y - s.center) / s.normal_to_y;
    const float half = d < 0.0F ? s.near_half_width : s.far_half_width;
    const float u = std::abs(d) / half;
    const float profile_distance = std::abs(d);
    float h = 3.30F * ease((profile_distance / half - 0.23F) / (1.58F - 0.23F));
    if (d < 0.0F) {
        // The inside point bar slopes up gradually into the shoulder. Its top
        // remains below the original shoulder and can still wet and dry.
        const float bar = bump(x, 162.0F, 30.0F) * bump(u, 1.04F, 0.49F);
        h -= 0.38F * bar;
    }
    const float outer = std::max(0.0F, profile_distance - 1.58F * half);
    const float left = d < 0.0F ? 1.0F : -1.0F;
    const float spur = 1.9F + 1.25F * std::cos(x / 31.0F + left * 2.1F);
    h += spur * ease(outer / 19.0F) + 0.42F * ease((outer - 10.0F) / 19.0F);
    return h;
}

struct Terrain {
    Fluid25DScenarioData scene;
    std::vector<Fluid25DVelocity> injection;
    std::vector<Fluid25DMomentum> momentum;
    std::vector<float> retained;
};

Terrain make_terrain(const std::filesystem::path& output_directory) {
    Terrain terrain;
    auto& scene = terrain.scene;
    scene.width = width;
    scene.height = height;
    scene.cell_size_m = spacing;
    scene.terrain_height_m.resize(cell_count);
    scene.initial_water_depth_m.resize(cell_count);
    scene.source_depth_rate_m_per_s.resize(cell_count);
    scene.sink_depth_rate_m_per_s.resize(cell_count);
    scene.boundary_outflow_face_mask.resize(cell_count);
    terrain.injection.resize(cell_count);
    terrain.momentum.resize(cell_count);
    terrain.retained.resize(cell_count);

    std::ofstream sections(output_directory / "sections.csv");
    sections << std::setprecision(9)
             << "x,center,near_half_width,far_half_width,near_bank_y,far_bank_y,floor_m,normal_to_"
                "y,thalweg_length_m\n";
    double source_weight = 0.0;
    double sink_weight = 0.0;
    float length = 0.0F;
    for (unsigned x = 0; x < width; ++x) {
        const float x_m = static_cast<float>(x) * spacing;
        const auto s = section(x_m);
        if (x != 0) {
            length += std::hypot(spacing, s.center - centerline(x_m - spacing));
        }
        const auto waterline = [&](float side) {
            float low = 0.0F;
            float high = side < 0.0F ? s.center - s.near_crest : s.far_crest - s.center;
            for (unsigned j = 0; j < 20; ++j) {
                const float mid = (low + high) * 0.5F;
                if (relief(s, x_m, s.center + side * mid) < initial_stage) {
                    low = mid;
                } else {
                    high = mid;
                }
            }
            return (low + high) * 0.5F;
        };
        if (x % samples_per_metre == 0) {
            sections << x_m << ',' << s.center << ',' << waterline(-1.0F) << ',' << waterline(1.0F)
                     << ',' << s.near_crest << ',' << s.far_crest << ',' << s.floor << ','
                     << s.normal_to_y << ',' << length << '\n';
        }

        float cross_area = 0.0F;
        for (unsigned y = 0; y < height; ++y) {
            const auto i = y * width + x;
            const float h = relief(s, x_m, static_cast<float>(y) * spacing);
            scene.terrain_height_m[i] = s.floor + h;
            const float depth = std::max(0.0F, initial_stage - h);
            scene.initial_water_depth_m[i] = depth;
            cross_area += depth * spacing;
            terrain.retained[i] = 0.87F * depth;
            const float wet_center = std::clamp(depth / initial_stage, 0.0F, 1.0F);
            const float inlet = bump(x_m, 8.0F, 8.5F) * wet_center * wet_center;
            const float outlet =
                (0.1F + 0.9F * ease((x_m - 220.0F) / 9.0F)) * wet_center * wet_center;
            scene.source_depth_rate_m_per_s[i] = inlet;
            scene.sink_depth_rate_m_per_s[i] = x_m >= 221.0F ? outlet : 0.0F;
            source_weight += inlet * spacing * spacing;
            sink_weight += scene.sink_depth_rate_m_per_s[i] * spacing * spacing;
            terrain.injection[i] = {.x_m_per_s = 1.1F, .y_m_per_s = 0.0F};
        }
        const float u = base_flow / cross_area;
        for (unsigned y = 0; y < height; ++y) {
            const auto i = y * width + x;
            const float q = scene.initial_water_depth_m[i] * u;
            terrain.momentum[i] = {.x_m2_per_s = q, .y_m2_per_s = q * s.tangent};
        }
    }
    for (unsigned i = 0; i < cell_count; ++i) {
        scene.source_depth_rate_m_per_s[i] /= static_cast<float>(source_weight);
        scene.sink_depth_rate_m_per_s[i] *= 90.0F / static_cast<float>(sink_weight);
    }
    if (!sections) {
        throw std::runtime_error("Could not write section metadata");
    }
    return terrain;
}

void write_display_field(std::ofstream& output, const std::vector<float>& values) {
    static_assert(sizeof(float) == 4 && std::endian::native == std::endian::little);
    if (values.size() != cell_count ||
        std::ranges::any_of(values, [](float value) { return !std::isfinite(value); })) {
        throw std::runtime_error("Invalid trace field");
    }
    std::array<float, display_width> row{};
    for (unsigned y = 0; y < display_height; ++y) {
        for (unsigned x = 0; x < display_width; ++x) {
            row[x] = values[(y * samples_per_metre) * width + x * samples_per_metre];
        }
        output.write(reinterpret_cast<const char*>(row.data()), row.size() * sizeof(float));
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("Usage: simulate OUTPUT_DIR");
        }
        const std::filesystem::path output_directory(argv[1]);
        std::filesystem::create_directories(output_directory);

        auto terrain = make_terrain(output_directory);
        Fluid25DConfig config;
        config.solver = Fluid25DSolver::FiniteVolume;
        config.scenario = Fluid25DScenario::SourceOutletDemo;
        config.grid_width = width;
        config.grid_height = height;
        config.cell_size_m = spacing;
        config.fixed_delta_seconds = snapshot_delta;
        config.simulation_substeps = substeps;
        config.flow_damping_per_second = damping_per_second;

        Fluid25DFiniteVolumeInitialState initial;
        initial.momentum_m2_per_s = std::move(terrain.momentum);
        Fluid25DFiniteVolumeOracle solver(config, std::move(terrain.scene), std::move(initial));
        const Fluid25DFiniteVolumeStepForcing forcing{
            .source_velocity_m_per_s = terrain.injection,
            .acceleration_m_per_s2 = {},
            .outlet_retained_depth_m = terrain.retained,
        };
        const std::vector<float> zero(cell_count);

        std::ofstream fields(output_directory / "fields.f32", std::ios::binary);
        std::ofstream ledger(output_directory / "ledger.csv");
        if (!fields || !ledger) {
            throw std::runtime_error("Could not create trace outputs");
        }
        ledger << std::setprecision(12) << "seconds,water_volume_m3,water_in_m3,water_out_m3,cfl\n";

        std::vector<std::size_t> exterior;
        for (unsigned x = 25 * samples_per_metre; x <= 218 * samples_per_metre; ++x) {
            const auto s = section(static_cast<float>(x) * spacing);
            for (unsigned y = 8 * samples_per_metre; y <= 87 * samples_per_metre; ++y) {
                const float ordinate = static_cast<float>(y) * spacing;
                if (ordinate <= s.near_crest || ordinate >= s.far_crest) {
                    exterior.push_back(y * width + x);
                }
            }
        }
        std::ofstream fringe(output_directory / "full-grid-fringe.csv");
        fringe << "seconds,peak_depth_m,area_over_10cm_m2\n";

        double water_in = 0.0;
        double water_out = 0.0;
        for (unsigned frame = 0; frame < snapshots; ++frame) {
            const auto& depth = solver.water_depth_m();
            float greatest = 0.0F;
            unsigned cells = 0;
            for (const auto index : exterior) {
                greatest = std::max(greatest, depth[index]);
                if (depth[index] > 0.1F) {
                    ++cells;
                }
            }
            fringe << frame * snapshot_delta << ',' << greatest << ',' << cells * spacing * spacing
                   << '\n';

            write_display_field(fields, solver.terrain_height_m());
            write_display_field(fields, solver.water_depth_m());
            write_display_field(fields, zero);
            ledger << frame * snapshot_delta << ',' << solver.total_water_volume_m3() << ','
                   << water_in << ',' << water_out << ',' << solver.last_cfl_number() << '\n';
            if (frame + 1 == snapshots) {
                break;
            }

            const float t = (frame + 0.5F) * snapshot_delta;
            float phase = 0.5F;
            if (t < rise_seconds) {
                phase = 0.5F * t / rise_seconds;
            } else if (t > rise_seconds + hold_seconds) {
                phase =
                    std::min(1.0F, 0.5F + 0.5F * (t - rise_seconds - hold_seconds) / fall_seconds);
            }
            const float w = phase < 1.0F ? std::sin(pi * phase) : 0.0F;
            const auto step = solver.step(base_flow + peak_flow * w * w, forcing);
            water_in += step.source_volume_m3;
            water_out += step.sink_volume_m3 + step.boundary_outflow_volume_m3;
        }

        fields.close();
        ledger.close();
        fringe.close();
        if (!fields || !ledger || !fringe) {
            throw std::runtime_error("Could not finish trace outputs");
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return 0;
}
