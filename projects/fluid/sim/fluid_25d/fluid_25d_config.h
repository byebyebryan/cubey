#pragma once

#include "../common/fluid_config_schema.h"

#include <cubey/host/common_config.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {

// River V0 deliberately names the physical quantities used by the numerical
// contract.  A source or sink rate is a change in water depth per second over
// one cell (m/s); the oracle converts it to a volume rate with cell area (m2)
// before it enters the ledger.
enum class Fluid25DScenario : std::uint32_t {
    DryBed = 0,
    LakeAtRest = 1,
    RiverCatchment = 2,
    TerrainCase = 3,
    BoundaryDrainFixture = 4,
    // Opt-in presentation fixture. It uses finite-volume evidence rather
    // than changing River V0's virtual-pipes default or its oracle baseline.
    SourceOutletDemo = 5,
    // Opt-in terrain-backed product fixture. Its pinned crop, native spacing,
    // and finite-volume solver are part of the scenario contract.
    MountainSourceOutletDemo = 6,
};

// Both authored source/outlet demonstrations use the same endpoint language,
// while retaining their own terrain, solver, and presentation contracts.
[[nodiscard]] constexpr bool fluid_25d_is_source_outlet_demo(Fluid25DScenario scenario) {
    return scenario == Fluid25DScenario::SourceOutletDemo ||
           scenario == Fluid25DScenario::MountainSourceOutletDemo;
}

// VirtualPipes remains the product default. FiniteVolume is an opt-in
// CPU/GPU numerical comparison contract; it is not a promoted terrain-water
// product mode.
enum class Fluid25DSolver : std::uint32_t {
    VirtualPipes = 0,
    FiniteVolume = 1,
};

// Terrain-case forcing is deliberately narrow and candidate-independent. The
// protocol selects a complete field construction; it never carries a painted
// channel, a per-candidate source mask, or a terrain modification.
enum class Fluid25DTerrainWaterProtocol : std::uint32_t {
    None = 0,
    RainPulse = 1,
    SheetRelease = 2,
};

// Presentation-only diagnostic views. They never alter the River V0 solver
// contract or its persistent GPU state.
enum class Fluid25DDebugView : std::uint32_t {
    Terrain = 0,
    WaterDepth = 1,
    SurfaceHeight = 2,
    FlowMagnitude = 3,
    FlowDirection = 4,
    WetDry = 5,
};

// River V0 keeps the surface selector separate from its catchment reading
// modes. Catchment/Diagnostics is the stable top-level presentation boundary;
// the selected catchment mode remains latched while that boundary is toggled.
enum class Fluid25DPresentationView : std::uint32_t {
    Catchment = 0,
    Diagnostics = 1,
};

enum class Fluid25DCatchmentView : std::uint32_t {
    Composite = 0,
    WaterIsolation = 1,
    FlowInspection = 2,
    // This is deliberately an opt-in reading mode for the compact analytic
    // source/outlet dye study. It displays conserved tracer concentration,
    // not a procedural surface animation or a velocity field.
    TransportInspection = 3,
};

// The River V0 product default is a bounded 2:1 catchment. Focused CPU/GPU
// oracle fixtures deliberately override this with much smaller grids.
inline constexpr std::uint32_t kDefaultFluid25DGridWidth = 256;
inline constexpr std::uint32_t kDefaultFluid25DGridHeight = 128;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletGridWidth = 256;
inline constexpr std::uint32_t kFluid25DMountainSourceOutletGridHeight = 128;
inline constexpr float kFluid25DMountainSourceOutletCellSizeM = 30.0F;
inline constexpr std::uint32_t kMaxFluid25DSubsteps = 64;
inline constexpr float kFluid25DDefaultWindowedPresentationTimeScale = 1.0F;
inline constexpr float kFluid25DMinWindowedPresentationTimeScale = 0.125F;
inline constexpr float kFluid25DMaxWindowedPresentationTimeScale = 8.0F;
inline constexpr std::uint32_t kFluid25DWindowedMaxFixedStepsPerFrame = 4U;

struct Fluid25DWindowedPacingFrame {
    std::uint32_t fixed_step_count = 0U;
    bool dropped_backlog = false;
};

inline void validate_fluid_25d_windowed_presentation_time_scale(float time_scale) {
    if (!std::isfinite(time_scale) || time_scale < kFluid25DMinWindowedPresentationTimeScale ||
        time_scale > kFluid25DMaxWindowedPresentationTimeScale) {
        throw std::runtime_error(
            "fluid 2.5D windowed presentation time scale must be finite and within the inclusive "
            "0.125..8.0 range");
    }
}

// Windowed rendering is driven by a wall-time accumulator, while headless
// captures continue to use one deterministic fixed step per requested frame.
// Keeping this scheduler project-local makes the presentation time scale
// impossible to leak into the numerical configuration or headless timing.
class Fluid25DWindowedPacing {
  public:
    explicit Fluid25DWindowedPacing(
        float fixed_delta_seconds = 1.0F / 60.0F,
        float presentation_time_scale = kFluid25DDefaultWindowedPresentationTimeScale) {
        configure(fixed_delta_seconds, presentation_time_scale);
    }

    void configure(float fixed_delta_seconds, float presentation_time_scale) {
        if (!std::isfinite(fixed_delta_seconds) || fixed_delta_seconds <= 0.0F) {
            throw std::runtime_error(
                "fluid 2.5D windowed pacing fixed delta must be finite and positive");
        }
        validate_fluid_25d_windowed_presentation_time_scale(presentation_time_scale);
        fixed_delta_seconds_ = static_cast<long double>(fixed_delta_seconds);
        presentation_time_scale_ = static_cast<long double>(presentation_time_scale);
        reset();
    }

    void set_presentation_time_scale(float presentation_time_scale) {
        validate_fluid_25d_windowed_presentation_time_scale(presentation_time_scale);
        presentation_time_scale_ = static_cast<long double>(presentation_time_scale);
    }

    [[nodiscard]] float presentation_time_scale() const noexcept {
        return static_cast<float>(presentation_time_scale_);
    }

    [[nodiscard]] double accumulator_seconds() const noexcept {
        return static_cast<double>(accumulator_seconds_);
    }

    [[nodiscard]] std::uint64_t dropped_backlog_frames() const noexcept {
        return dropped_backlog_frames_;
    }

    [[nodiscard]] Fluid25DWindowedPacingFrame advance(double wall_delta_seconds,
                                                       bool paused) {
        if (!std::isfinite(wall_delta_seconds) || wall_delta_seconds < 0.0) {
            throw std::runtime_error(
                "fluid 2.5D windowed pacing wall delta must be finite and nonnegative");
        }
        if (paused || wall_delta_seconds == 0.0) {
            return {};
        }

        const long double scaled_delta_seconds =
            static_cast<long double>(wall_delta_seconds) * presentation_time_scale_;
        if (!std::isfinite(scaled_delta_seconds)) {
            throw std::runtime_error("fluid 2.5D windowed pacing scaled wall delta overflowed");
        }
        accumulator_seconds_ += scaled_delta_seconds;
        if (!std::isfinite(accumulator_seconds_)) {
            throw std::runtime_error("fluid 2.5D windowed pacing accumulator overflowed");
        }

        // A tiny relative tolerance keeps repeated rational frame deltas such
        // as 1/144 from losing a fixed step to floating-point roundoff, while
        // remaining far below any meaningful simulation interval.
        constexpr long double kStepCountEpsilon = 1.0e-6L;
        const long double available_steps =
            std::floor(accumulator_seconds_ / fixed_delta_seconds_ + kStepCountEpsilon);
        if (available_steps <= 0.0L) {
            return {};
        }

        if (available_steps > static_cast<long double>(kFluid25DWindowedMaxFixedStepsPerFrame)) {
            accumulator_seconds_ = std::fmod(accumulator_seconds_, fixed_delta_seconds_);
            if (accumulator_seconds_ < 0.0L) {
                accumulator_seconds_ = 0.0L;
            }
            if (dropped_backlog_frames_ != std::numeric_limits<std::uint64_t>::max()) {
                ++dropped_backlog_frames_;
            }
            return {
                .fixed_step_count = kFluid25DWindowedMaxFixedStepsPerFrame,
                .dropped_backlog = true,
            };
        }

        const std::uint32_t fixed_step_count = static_cast<std::uint32_t>(available_steps);
        accumulator_seconds_ -=
            static_cast<long double>(fixed_step_count) * fixed_delta_seconds_;
        if (accumulator_seconds_ < 0.0L &&
            accumulator_seconds_ > -kStepCountEpsilon * fixed_delta_seconds_) {
            accumulator_seconds_ = 0.0L;
        }
        return {.fixed_step_count = fixed_step_count, .dropped_backlog = false};
    }

    void reset() noexcept {
        accumulator_seconds_ = 0.0L;
        dropped_backlog_frames_ = 0U;
    }

  private:
    long double fixed_delta_seconds_ = 1.0L / 60.0L;
    long double presentation_time_scale_ =
        static_cast<long double>(kFluid25DDefaultWindowedPresentationTimeScale);
    long double accumulator_seconds_ = 0.0L;
    std::uint64_t dropped_backlog_frames_ = 0U;
};

struct Fluid25DConfig {
    std::uint32_t grid_width = kDefaultFluid25DGridWidth;
    std::uint32_t grid_height = kDefaultFluid25DGridHeight;

    // All spatial quantities are metres and all rates below are per-second.
    float cell_size_m = 1.0F;
    float fixed_delta_seconds = 1.0F / 60.0F;
    std::uint32_t simulation_substeps = 2;
    float gravity_m_per_s2 = 9.81F;
    float flow_damping_per_second = 0.15F;
    float minimum_wet_depth_m = 0.0001F;
    // Unset keeps sources active forever. When set, the fixed-step schedule
    // provides source scale one before this duration and zero afterwards.
    std::optional<float> source_active_duration_seconds{};
    // Conservative dye is opt-in and only applies to the compact analytic
    // source/outlet fixture. Both pulse fields must be present together.
    std::optional<float> dye_pulse_start_seconds{};
    std::optional<float> dye_pulse_duration_seconds{};

    // Only terrain-case may select a terrain-water protocol. Rain is stored
    // in the solver's native depth-rate unit (m/s), while the public CLI uses
    // mm/hour to make the forcing scale explicit to an author.
    Fluid25DTerrainWaterProtocol terrain_water_protocol = Fluid25DTerrainWaterProtocol::None;
    float rainfall_depth_rate_m_per_s = 0.0F;
    float sheet_initial_depth_m = 0.0F;

    Fluid25DScenario scenario = Fluid25DScenario::RiverCatchment;
    Fluid25DSolver solver = Fluid25DSolver::VirtualPipes;
};

// Keep the availability predicate next to the immutable simulation contract
// so startup validation and the interactive UI cannot disagree about whether
// Transport Inspection will contain a real, conservative dye pulse.
[[nodiscard]] inline bool
fluid_25d_transport_inspection_available(const Fluid25DConfig& config) noexcept {
    return config.scenario == Fluid25DScenario::SourceOutletDemo &&
           config.solver == Fluid25DSolver::FiniteVolume &&
           config.dye_pulse_start_seconds.has_value() &&
           std::isfinite(*config.dye_pulse_start_seconds) &&
           *config.dye_pulse_start_seconds >= 0.0F &&
           config.dye_pulse_duration_seconds.has_value() &&
           std::isfinite(*config.dye_pulse_duration_seconds) &&
           *config.dye_pulse_duration_seconds > 0.0F;
}

// Startup options retain "unset" separately from the concrete, validated
// runtime values.  This is the same Config V2 shape used by the active fluid
// projects, but remains project-local until a runtime target exists.
struct Fluid25DStartupOptions {
    std::optional<float> cell_size_m{};
    std::optional<float> fixed_delta_seconds{};
    std::optional<std::uint32_t> simulation_substeps{};
    std::optional<float> gravity_m_per_s2{};
    std::optional<float> flow_damping_per_second{};
    std::optional<float> minimum_wet_depth_m{};
    std::optional<float> source_active_duration_seconds{};
    std::optional<float> dye_pulse_start_seconds{};
    std::optional<float> dye_pulse_duration_seconds{};
    std::optional<std::string> terrain_water_protocol{};
    std::optional<float> rainfall_rate_mm_per_hour{};
    std::optional<float> sheet_depth_m{};
    std::optional<std::string> scenario{};
    std::optional<std::string> solver{};
};

[[nodiscard]] inline const char* fluid_25d_scenario_name(Fluid25DScenario scenario) {
    switch (scenario) {
    case Fluid25DScenario::DryBed:
        return "dry-bed";
    case Fluid25DScenario::LakeAtRest:
        return "lake-at-rest";
    case Fluid25DScenario::RiverCatchment:
        return "river-catchment";
    case Fluid25DScenario::TerrainCase:
        return "terrain-case";
    case Fluid25DScenario::BoundaryDrainFixture:
        return "boundary-drain-fixture";
    case Fluid25DScenario::SourceOutletDemo:
        return "source-outlet-demo";
    case Fluid25DScenario::MountainSourceOutletDemo:
        return "mountain-source-outlet-demo";
    }
    return "river-catchment";
}

[[nodiscard]] inline const char* fluid_25d_solver_name(Fluid25DSolver solver) {
    switch (solver) {
    case Fluid25DSolver::VirtualPipes:
        return "virtual-pipes";
    case Fluid25DSolver::FiniteVolume:
        return "finite-volume";
    }
    return "virtual-pipes";
}

[[nodiscard]] inline Fluid25DSolver fluid_25d_solver_from_name(std::string_view name) {
    if (name.empty() || name == "virtual-pipes") {
        return Fluid25DSolver::VirtualPipes;
    }
    if (name == "finite-volume") {
        return Fluid25DSolver::FiniteVolume;
    }
    throw std::runtime_error("fluid 2.5D solver must be virtual-pipes or finite-volume");
}

[[nodiscard]] inline Fluid25DScenario fluid_25d_scenario_from_name(std::string_view name) {
    if (name.empty() || name == "river" || name == "river-catchment") {
        return Fluid25DScenario::RiverCatchment;
    }
    if (name == "dry" || name == "dry-bed") {
        return Fluid25DScenario::DryBed;
    }
    if (name == "lake" || name == "lake-at-rest") {
        return Fluid25DScenario::LakeAtRest;
    }
    if (name == "terrain-case") {
        return Fluid25DScenario::TerrainCase;
    }
    if (name == "boundary-drain-fixture") {
        return Fluid25DScenario::BoundaryDrainFixture;
    }
    if (name == "source-outlet-demo") {
        return Fluid25DScenario::SourceOutletDemo;
    }
    if (name == "mountain-source-outlet-demo") {
        return Fluid25DScenario::MountainSourceOutletDemo;
    }
    throw std::runtime_error(
        "fluid 2.5D scenario must be dry-bed, lake-at-rest, river-catchment, terrain-case, "
        "boundary-drain-fixture, source-outlet-demo, or mountain-source-outlet-demo");
}

[[nodiscard]] inline const char*
fluid_25d_terrain_water_protocol_name(Fluid25DTerrainWaterProtocol protocol) {
    switch (protocol) {
    case Fluid25DTerrainWaterProtocol::None:
        return "none";
    case Fluid25DTerrainWaterProtocol::RainPulse:
        return "rain-pulse";
    case Fluid25DTerrainWaterProtocol::SheetRelease:
        return "sheet-release";
    }
    return "none";
}

[[nodiscard]] inline Fluid25DTerrainWaterProtocol
fluid_25d_terrain_water_protocol_from_name(std::string_view name) {
    if (name.empty() || name == "none") {
        return Fluid25DTerrainWaterProtocol::None;
    }
    if (name == "rain-pulse") {
        return Fluid25DTerrainWaterProtocol::RainPulse;
    }
    if (name == "sheet-release") {
        return Fluid25DTerrainWaterProtocol::SheetRelease;
    }
    throw std::runtime_error(
        "fluid 2.5D terrain-water protocol must be none, rain-pulse, or sheet-release");
}

[[nodiscard]] inline float
fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(float rainfall_rate_mm_per_hour) {
    if (!std::isfinite(rainfall_rate_mm_per_hour) || rainfall_rate_mm_per_hour < 0.0F) {
        throw std::runtime_error("fluid 2.5D rainfall rate must be finite and nonnegative");
    }
    constexpr float kMillimetresPerMetre = 1000.0F;
    constexpr float kSecondsPerHour = 3600.0F;
    return rainfall_rate_mm_per_hour / (kMillimetresPerMetre * kSecondsPerHour);
}

[[nodiscard]] inline Fluid25DDebugView fluid_25d_debug_view_from_name(std::string_view name) {
    if (name.empty() || name == "terrain") {
        return Fluid25DDebugView::Terrain;
    }
    if (name == "depth" || name == "water-depth") {
        return Fluid25DDebugView::WaterDepth;
    }
    if (name == "surface" || name == "surface-height") {
        return Fluid25DDebugView::SurfaceHeight;
    }
    if (name == "flow" || name == "flow-magnitude") {
        return Fluid25DDebugView::FlowMagnitude;
    }
    if (name == "direction" || name == "flow-direction") {
        return Fluid25DDebugView::FlowDirection;
    }
    if (name == "wet-dry" || name == "wetdry") {
        return Fluid25DDebugView::WetDry;
    }
    throw std::runtime_error("fluid 2.5D debug view must be terrain, depth, surface, flow, "
                             "direction, or wet-dry");
}

[[nodiscard]] inline Fluid25DPresentationView
fluid_25d_presentation_view_from_name(std::string_view name) {
    if (name.empty() || name == "catchment") {
        return Fluid25DPresentationView::Catchment;
    }
    if (name == "diagnostics" || name == "diagnostic") {
        return Fluid25DPresentationView::Diagnostics;
    }
    throw std::runtime_error("fluid 2.5D view must be catchment or diagnostics");
}

[[nodiscard]] inline Fluid25DCatchmentView
fluid_25d_catchment_view_from_name(std::string_view name) {
    if (name.empty() || name == "composite") {
        return Fluid25DCatchmentView::Composite;
    }
    if (name == "water-isolation" || name == "water_isolation") {
        return Fluid25DCatchmentView::WaterIsolation;
    }
    if (name == "flow-inspection" || name == "flow_inspection") {
        return Fluid25DCatchmentView::FlowInspection;
    }
    if (name == "transport-inspection" || name == "transport_inspection") {
        return Fluid25DCatchmentView::TransportInspection;
    }
    throw std::runtime_error(
        "fluid 2.5D catchment view must be composite, water-isolation, flow-inspection, or "
        "transport-inspection");
}

[[nodiscard]] inline const char* fluid_25d_presentation_view_name(Fluid25DPresentationView view) {
    switch (view) {
    case Fluid25DPresentationView::Catchment:
        return "Catchment";
    case Fluid25DPresentationView::Diagnostics:
        return "Diagnostics";
    }
    return "Catchment";
}

[[nodiscard]] inline const char* fluid_25d_catchment_view_name(Fluid25DCatchmentView view) {
    switch (view) {
    case Fluid25DCatchmentView::Composite:
        return "Composite";
    case Fluid25DCatchmentView::WaterIsolation:
        return "Water Isolation";
    case Fluid25DCatchmentView::FlowInspection:
        return "Flow Inspection";
    case Fluid25DCatchmentView::TransportInspection:
        return "Transport Inspection";
    }
    return "Composite";
}

[[nodiscard]] inline const char* fluid_25d_debug_view_name(Fluid25DDebugView view) {
    switch (view) {
    case Fluid25DDebugView::Terrain:
        return "Terrain";
    case Fluid25DDebugView::WaterDepth:
        return "Water Depth";
    case Fluid25DDebugView::SurfaceHeight:
        return "Surface Height";
    case Fluid25DDebugView::FlowMagnitude:
        return "Flow Magnitude";
    case Fluid25DDebugView::FlowDirection:
        return "Flow Direction";
    case Fluid25DDebugView::WetDry:
        return "Wet / Dry";
    }
    return "Terrain";
}

[[nodiscard]] inline std::size_t fluid_25d_cell_count(const Fluid25DConfig& config) {
    if (config.grid_width == 0 || config.grid_height == 0) {
        throw std::runtime_error("fluid 2.5D grid dimensions must be positive");
    }
    const std::size_t width = static_cast<std::size_t>(config.grid_width);
    const std::size_t height = static_cast<std::size_t>(config.grid_height);
    if (width > std::numeric_limits<std::size_t>::max() / height) {
        throw std::runtime_error("fluid 2.5D grid dimensions are too large");
    }
    return width * height;
}

[[nodiscard]] inline std::size_t fluid_25d_mesh_vertex_count(const Fluid25DConfig& config) {
    if (config.grid_width < 2U || config.grid_height < 2U) {
        throw std::runtime_error("fluid 2.5D product mesh requires grid dimensions of at least 2");
    }
    const std::size_t cells_x = static_cast<std::size_t>(config.grid_width - 1U);
    const std::size_t cells_y = static_cast<std::size_t>(config.grid_height - 1U);
    if (cells_x > std::numeric_limits<std::size_t>::max() / cells_y) {
        throw std::runtime_error("fluid 2.5D product mesh cell count is too large");
    }
    const std::size_t quad_count = cells_x * cells_y;
    if (quad_count > std::numeric_limits<std::size_t>::max() / 6U) {
        throw std::runtime_error("fluid 2.5D product mesh vertex count is too large");
    }
    return quad_count * 6U;
}

inline void validate_fluid_25d_config(const Fluid25DConfig& config) {
    static_cast<void>(fluid_25d_cell_count(config));
    static_cast<void>(fluid_25d_mesh_vertex_count(config));
    if (config.scenario != Fluid25DScenario::DryBed &&
        config.scenario != Fluid25DScenario::LakeAtRest &&
        config.scenario != Fluid25DScenario::RiverCatchment &&
        config.scenario != Fluid25DScenario::TerrainCase &&
        config.scenario != Fluid25DScenario::BoundaryDrainFixture &&
        config.scenario != Fluid25DScenario::SourceOutletDemo &&
        config.scenario != Fluid25DScenario::MountainSourceOutletDemo) {
        throw std::runtime_error("fluid 2.5D scenario value is invalid");
    }
    if (config.solver != Fluid25DSolver::VirtualPipes &&
        config.solver != Fluid25DSolver::FiniteVolume) {
        throw std::runtime_error("fluid 2.5D solver value is invalid");
    }
    if ((config.scenario == Fluid25DScenario::SourceOutletDemo ||
         config.scenario == Fluid25DScenario::MountainSourceOutletDemo) &&
        config.solver != Fluid25DSolver::FiniteVolume) {
        throw std::runtime_error(
            "fluid 2.5D source/outlet demos require --fluid25d-solver finite-volume");
    }
    if (!(config.cell_size_m > 0.0F) || !std::isfinite(config.cell_size_m)) {
        throw std::runtime_error("fluid 2.5D cell size must be finite and positive");
    }
    if (!(config.fixed_delta_seconds > 0.0F) || !std::isfinite(config.fixed_delta_seconds)) {
        throw std::runtime_error("fluid 2.5D fixed delta must be finite and positive");
    }
    if (config.simulation_substeps == 0 || config.simulation_substeps > kMaxFluid25DSubsteps) {
        throw std::runtime_error("fluid 2.5D substeps must be in 1..64");
    }
    if (!(config.gravity_m_per_s2 > 0.0F) || !std::isfinite(config.gravity_m_per_s2)) {
        throw std::runtime_error("fluid 2.5D gravity must be finite and positive");
    }
    if (config.flow_damping_per_second < 0.0F || !std::isfinite(config.flow_damping_per_second)) {
        throw std::runtime_error("fluid 2.5D flow damping must be finite and nonnegative");
    }
    if (config.minimum_wet_depth_m < 0.0F || !std::isfinite(config.minimum_wet_depth_m)) {
        throw std::runtime_error("fluid 2.5D minimum wet depth must be finite and nonnegative");
    }
    if (config.source_active_duration_seconds.has_value() &&
        (!std::isfinite(*config.source_active_duration_seconds) ||
         *config.source_active_duration_seconds < 0.0F)) {
        throw std::runtime_error(
            "fluid 2.5D source active duration must be finite and nonnegative");
    }
    const bool has_dye_pulse_start = config.dye_pulse_start_seconds.has_value();
    const bool has_dye_pulse_duration = config.dye_pulse_duration_seconds.has_value();
    if (has_dye_pulse_start != has_dye_pulse_duration) {
        throw std::runtime_error(
            "fluid 2.5D dye pulse start and duration must be supplied together");
    }
    if (has_dye_pulse_start) {
        const float start_seconds = *config.dye_pulse_start_seconds;
        const float duration_seconds = *config.dye_pulse_duration_seconds;
        if (!std::isfinite(start_seconds) || start_seconds < 0.0F ||
            !std::isfinite(duration_seconds) || duration_seconds <= 0.0F ||
            !std::isfinite(static_cast<double>(start_seconds) +
                           static_cast<double>(duration_seconds))) {
            throw std::runtime_error(
                "fluid 2.5D dye pulse start must be finite and nonnegative, and duration must be "
                "finite and positive");
        }
        if (config.scenario != Fluid25DScenario::SourceOutletDemo ||
            config.solver != Fluid25DSolver::FiniteVolume) {
            throw std::runtime_error(
                "fluid 2.5D dye pulse timing requires source-outlet-demo with finite-volume");
        }
    }
    if (config.terrain_water_protocol != Fluid25DTerrainWaterProtocol::None &&
        config.terrain_water_protocol != Fluid25DTerrainWaterProtocol::RainPulse &&
        config.terrain_water_protocol != Fluid25DTerrainWaterProtocol::SheetRelease) {
        throw std::runtime_error("fluid 2.5D terrain-water protocol value is invalid");
    }
    if (!std::isfinite(config.rainfall_depth_rate_m_per_s) ||
        config.rainfall_depth_rate_m_per_s < 0.0F) {
        throw std::runtime_error("fluid 2.5D rainfall depth rate must be finite and nonnegative");
    }
    if (!std::isfinite(config.sheet_initial_depth_m) || config.sheet_initial_depth_m < 0.0F) {
        throw std::runtime_error("fluid 2.5D sheet depth must be finite and nonnegative");
    }
    if (config.scenario == Fluid25DScenario::MountainSourceOutletDemo) {
        if (config.grid_width != kFluid25DMountainSourceOutletGridWidth ||
            config.grid_height != kFluid25DMountainSourceOutletGridHeight) {
            throw std::runtime_error(
                "fluid 2.5D mountain-source-outlet-demo requires a 256x128 grid");
        }
        if (config.cell_size_m != kFluid25DMountainSourceOutletCellSizeM) {
            throw std::runtime_error(
                "fluid 2.5D mountain-source-outlet-demo requires 30 metre cells");
        }
        if (config.terrain_water_protocol != Fluid25DTerrainWaterProtocol::None ||
            config.rainfall_depth_rate_m_per_s != 0.0F || config.sheet_initial_depth_m != 0.0F ||
            config.source_active_duration_seconds.has_value() || has_dye_pulse_start) {
            throw std::runtime_error("fluid 2.5D mountain-source-outlet-demo rejects terrain-water "
                                     "protocol and forcing options");
        }
    }
    if (config.scenario != Fluid25DScenario::TerrainCase) {
        return;
    }
    switch (config.terrain_water_protocol) {
    case Fluid25DTerrainWaterProtocol::None:
        if (config.rainfall_depth_rate_m_per_s != 0.0F || config.sheet_initial_depth_m != 0.0F ||
            config.source_active_duration_seconds.has_value() || has_dye_pulse_start) {
            throw std::runtime_error(
                "fluid 2.5D terrain-case protocol none rejects forcing parameters");
        }
        break;
    case Fluid25DTerrainWaterProtocol::RainPulse:
        if (!(config.rainfall_depth_rate_m_per_s > 0.0F) ||
            !(config.source_active_duration_seconds.has_value() &&
              *config.source_active_duration_seconds > 0.0F) ||
            config.sheet_initial_depth_m != 0.0F || has_dye_pulse_start) {
            throw std::runtime_error(
                "fluid 2.5D rain-pulse requires positive rainfall and source-active duration, and "
                "rejects sheet depth");
        }
        break;
    case Fluid25DTerrainWaterProtocol::SheetRelease:
        if (!(config.sheet_initial_depth_m > 0.0F) || config.rainfall_depth_rate_m_per_s != 0.0F ||
            config.source_active_duration_seconds.has_value() || has_dye_pulse_start) {
            throw std::runtime_error(
                "fluid 2.5D sheet-release requires positive sheet depth and rejects rain and "
                "source-active duration");
        }
        break;
    }
}

// Rendering and forcing share the same completed fixed-step clock. Keep the
// conversion in one place so a presentation timestamp cannot accidentally
// depend on wall time or lose determinism through an intermediate float.
[[nodiscard]] inline float fluid_25d_elapsed_seconds(const Fluid25DConfig& config,
                                                     std::uint64_t completed_steps) {
    validate_fluid_25d_config(config);
    const double elapsed_seconds =
        static_cast<double>(completed_steps) * static_cast<double>(config.fixed_delta_seconds);
    if (!std::isfinite(elapsed_seconds) ||
        elapsed_seconds > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::runtime_error("fluid 2.5D elapsed fixed-step time is not representable");
    }
    return static_cast<float>(elapsed_seconds);
}

[[nodiscard]] inline Fluid25DConfig
fluid_25d_config_from_options(const common::FluidGridOptions& grid,
                              const Fluid25DStartupOptions& options) {
    Fluid25DConfig config;
    if (grid.width) {
        config.grid_width = *grid.width;
    }
    if (grid.height) {
        config.grid_height = *grid.height;
    }
    if (options.cell_size_m) {
        config.cell_size_m = *options.cell_size_m;
    }
    if (options.fixed_delta_seconds) {
        config.fixed_delta_seconds = *options.fixed_delta_seconds;
    }
    if (options.simulation_substeps) {
        config.simulation_substeps = *options.simulation_substeps;
    }
    if (options.gravity_m_per_s2) {
        config.gravity_m_per_s2 = *options.gravity_m_per_s2;
    }
    if (options.flow_damping_per_second) {
        config.flow_damping_per_second = *options.flow_damping_per_second;
    }
    if (options.minimum_wet_depth_m) {
        config.minimum_wet_depth_m = *options.minimum_wet_depth_m;
    }
    config.source_active_duration_seconds = options.source_active_duration_seconds;
    config.dye_pulse_start_seconds = options.dye_pulse_start_seconds;
    config.dye_pulse_duration_seconds = options.dye_pulse_duration_seconds;
    config.terrain_water_protocol =
        fluid_25d_terrain_water_protocol_from_name(options.terrain_water_protocol.value_or(""));
    if (options.rainfall_rate_mm_per_hour.has_value()) {
        config.rainfall_depth_rate_m_per_s = fluid_25d_rainfall_depth_rate_m_per_s_from_mm_per_hour(
            options.rainfall_rate_mm_per_hour.value());
    }
    if (options.sheet_depth_m.has_value()) {
        config.sheet_initial_depth_m = options.sheet_depth_m.value();
    }
    config.scenario = fluid_25d_scenario_from_name(options.scenario.value_or(""));
    config.solver = fluid_25d_solver_from_name(options.solver.value_or(""));
    if (config.scenario == Fluid25DScenario::MountainSourceOutletDemo &&
        !options.cell_size_m.has_value()) {
        config.cell_size_m = kFluid25DMountainSourceOutletCellSizeM;
    }
    validate_fluid_25d_config(config);
    return config;
}

// The forcing clock advances once per public fixed solver step, never on wall
// time. Reset and pause policy stay app-owned while CPU and GPU share its
// resulting scalar for the next step.
class Fluid25DSourceRateSchedule {
  public:
    [[nodiscard]] float source_rate_scale(const Fluid25DConfig& config) const {
        validate_fluid_25d_config(config);
        if (!config.source_active_duration_seconds.has_value()) {
            return 1.0F;
        }
        const double elapsed_seconds =
            static_cast<double>(completed_steps_) * static_cast<double>(config.fixed_delta_seconds);
        return elapsed_seconds < static_cast<double>(*config.source_active_duration_seconds) ? 1.0F
                                                                                             : 0.0F;
    }

    [[nodiscard]] float elapsed_seconds(const Fluid25DConfig& config) const {
        return fluid_25d_elapsed_seconds(config, completed_steps_);
    }

    void advance_fixed_step() {
        if (completed_steps_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("fluid 2.5D source schedule step count overflowed");
        }
        ++completed_steps_;
    }

    void reset() noexcept {
        completed_steps_ = 0U;
    }

    [[nodiscard]] std::uint64_t completed_steps() const noexcept {
        return completed_steps_;
    }

  private:
    std::uint64_t completed_steps_ = 0U;
};

// The dye pulse uses the same completed fixed-step clock as hydraulic forcing.
// A half-open interval makes the start and end boundaries deterministic: the
// first step at start is dyed, while the first step at start+duration is not.
class Fluid25DDyeSourceSchedule {
  public:
    [[nodiscard]] float source_concentration(const Fluid25DConfig& config) const {
        validate_fluid_25d_config(config);
        if (!config.dye_pulse_start_seconds.has_value()) {
            return 0.0F;
        }
        const double elapsed_seconds =
            static_cast<double>(completed_steps_) * static_cast<double>(config.fixed_delta_seconds);
        const double start_seconds = static_cast<double>(*config.dye_pulse_start_seconds);
        const double end_seconds =
            start_seconds + static_cast<double>(*config.dye_pulse_duration_seconds);
        return elapsed_seconds >= start_seconds && elapsed_seconds < end_seconds ? 1.0F : 0.0F;
    }

    [[nodiscard]] float elapsed_seconds(const Fluid25DConfig& config) const {
        return fluid_25d_elapsed_seconds(config, completed_steps_);
    }

    void advance_fixed_step() {
        if (completed_steps_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::runtime_error("fluid 2.5D dye schedule step count overflowed");
        }
        ++completed_steps_;
    }

    void reset() noexcept {
        completed_steps_ = 0U;
    }

    [[nodiscard]] std::uint64_t completed_steps() const noexcept {
        return completed_steps_;
    }

  private:
    std::uint64_t completed_steps_ = 0U;
};

} // namespace cubey::projects::fluid::fluid_25d
