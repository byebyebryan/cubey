#include "fluid_25d_backend_adapters.h"

#include "../../fluid_25d/fluid_25d_project_config.h"
#include "fluid_25d_config.h"
#include "fluid_25d_recording.h"
#include "fluid_25d_scenarios.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaximumScenarioStringBytes = 4096U;
constexpr std::size_t kMaximumRecordingJsonBytes = 8U * 1024U * 1024U;
constexpr std::size_t kMaximumCanonicalIdentityBytes = 64U * 1024U * 1024U;

[[noreturn]] void invalid(std::string message) {
    throw std::invalid_argument("fluid 2.5D backend adapter: " + std::move(message));
}

// A small canonical writer: every integer is little-endian, every string is
// length-prefixed UTF-8 bytes, and floats are their exact IEEE-754 bit pattern.
// This keeps identities independent of host endianness and C++ object layout.
class CanonicalWriter {
  public:
    void boolean(bool value) {
        u8(value ? 1U : 0U);
    }

    void u8(std::uint8_t value) {
        ensure_append(1U);
        bytes_.push_back(static_cast<std::byte>(value));
    }

    void u32(std::uint32_t value) {
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void u64(std::uint64_t value) {
        for (unsigned shift = 0U; shift < 64U; shift += 8U) {
            u8(static_cast<std::uint8_t>((value >> shift) & 0xffU));
        }
    }

    void f32(float value) {
        u32(std::bit_cast<std::uint32_t>(value));
    }
    void f64(double value) {
        u64(std::bit_cast<std::uint64_t>(value));
    }

    void string(std::string_view value) {
        if (value.size() > kMaximumCanonicalIdentityBytes) {
            invalid("canonical identity string exceeds the total identity size bound");
        }
        u64(static_cast<std::uint64_t>(value.size()));
        ensure_append(value.size());
        for (const char raw_character : value) {
            const auto character = static_cast<unsigned char>(raw_character);
            bytes_.push_back(static_cast<std::byte>(character));
        }
    }

    [[nodiscard]] std::string digest() const {
        return cubey::asset::sha256_hex(bytes_);
    }

  private:
    void ensure_append(std::size_t amount) {
        if (amount > kMaximumCanonicalIdentityBytes - bytes_.size()) {
            invalid("canonical identity exceeds the total size bound");
        }
        const std::size_t required = bytes_.size() + amount;
        if (required <= bytes_.capacity()) {
            return;
        }
        const std::size_t doubled = bytes_.capacity() > kMaximumCanonicalIdentityBytes / 2U
                                        ? kMaximumCanonicalIdentityBytes
                                        : std::max<std::size_t>(bytes_.capacity() * 2U, 256U);
        bytes_.reserve(std::min(kMaximumCanonicalIdentityBytes, std::max(required, doubled)));
    }

    std::vector<std::byte> bytes_{};
};

template <typename Enum> void enum_value(CanonicalWriter& writer, Enum value) {
    writer.u32(static_cast<std::uint32_t>(value));
}

void optional_float(CanonicalWriter& writer, const std::optional<float>& value) {
    writer.boolean(value.has_value());
    if (value) {
        writer.f32(*value);
    }
}

[[nodiscard]] std::string canonical_float_array_digest(std::span<const float> values) {
    CanonicalWriter writer;
    writer.string("cubey.float32-le-array.v1");
    writer.u64(static_cast<std::uint64_t>(values.size()));
    for (const float value : values) {
        writer.f32(value);
    }
    return writer.digest();
}

[[nodiscard]] std::string raw_float32_le_sha256(std::span<const float> values) {
    if (values.size() > kFluid25DBackendMaximumCells ||
        values.size() > std::numeric_limits<std::size_t>::max() / sizeof(float)) {
        invalid("raw float32 bed exceeds the backend contract size bound");
    }
    std::vector<std::byte> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (const float value : values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((bits >> shift) & 0xffU));
        }
    }
    return cubey::asset::sha256_hex(bytes);
}

[[nodiscard]] std::string canonical_u32_array_digest(std::span<const std::uint32_t> values) {
    CanonicalWriter writer;
    writer.string("cubey.uint32-le-array.v1");
    writer.u64(static_cast<std::uint64_t>(values.size()));
    for (const std::uint32_t value : values) {
        writer.u32(value);
    }
    return writer.digest();
}

[[nodiscard]] std::size_t bounded_cell_count(std::uint32_t width, std::uint32_t height) {
    if (width < 2U || height < 2U || width > kFluid25DBackendMaximumDimension ||
        height > kFluid25DBackendMaximumDimension) {
        invalid("grid dimensions are outside the backend contract bounds");
    }
    const std::size_t wide_width = width;
    const std::size_t wide_height = height;
    if (wide_width > kFluid25DBackendMaximumCells / wide_height) {
        invalid("grid cell count exceeds the backend contract bound");
    }
    return wide_width * wide_height;
}

void validate_float_values(std::span<const float> values, std::string_view label,
                           bool nonnegative) {
    for (const float value : values) {
        if (!std::isfinite(value) || (nonnegative && value < 0.0F)) {
            invalid(std::string(label) + " contains a non-finite or negative value");
        }
    }
}

void validate_short_string(std::string_view value, std::string_view label) {
    if (value.size() > kMaximumScenarioStringBytes || value.find('\0') != std::string_view::npos) {
        invalid(std::string(label) + " is too long or contains NUL");
    }
}

void append_natural_flow_metadata(CanonicalWriter& writer,
                                  const std::optional<Fluid25DNaturalFlowStudyMetadata>& metadata,
                                  std::size_t cell_count, std::uint32_t width,
                                  std::uint32_t height) {
    writer.boolean(metadata.has_value());
    if (!metadata) {
        return;
    }
    const Fluid25DNaturalFlowStudyMetadata& natural = *metadata;
    validate_short_string(natural.candidate_id, "natural-flow candidate id");
    if (natural.source_cells.size() > cell_count || natural.gauges.size() > cell_count) {
        invalid("natural-flow metadata exceeds the grid-bounded entry count");
    }
    switch (natural.expected_outlet_edge) {
    case Fluid25DNaturalFlowEdge::West:
    case Fluid25DNaturalFlowEdge::East:
    case Fluid25DNaturalFlowEdge::North:
    case Fluid25DNaturalFlowEdge::South:
        break;
    default:
        invalid("natural-flow outlet edge is invalid");
    }
    writer.boolean(natural.has_expected_outlet);
    writer.string(natural.candidate_id);
    writer.u64(static_cast<std::uint64_t>(natural.source_cells.size()));
    for (const std::size_t cell : natural.source_cells) {
        if (cell >= cell_count) {
            invalid("natural-flow source cell is outside the grid");
        }
        writer.u64(static_cast<std::uint64_t>(cell));
    }
    enum_value(writer, natural.expected_outlet_edge);
    writer.u32(natural.expected_outlet_x_min);
    writer.u32(natural.expected_outlet_z_min);
    writer.u32(natural.expected_outlet_x_max);
    writer.u32(natural.expected_outlet_z_max);
    if (natural.expected_outlet_x_min > natural.expected_outlet_x_max ||
        natural.expected_outlet_z_min > natural.expected_outlet_z_max ||
        natural.expected_outlet_x_max >= width || natural.expected_outlet_z_max >= height) {
        invalid("natural-flow outlet bounds are outside the grid");
    }
    writer.u64(static_cast<std::uint64_t>(natural.gauges.size()));
    for (const Fluid25DNaturalFlowGauge& gauge : natural.gauges) {
        validate_short_string(gauge.name, "natural-flow gauge name");
        if (gauge.name.empty() || gauge.x_cell >= width || gauge.z_cell >= height ||
            !std::isfinite(gauge.distance_m) || gauge.distance_m < 0.0) {
            invalid("natural-flow gauge metadata is invalid");
        }
        writer.string(gauge.name);
        writer.u32(gauge.x_cell);
        writer.u32(gauge.z_cell);
        writer.u32(std::bit_cast<std::uint32_t>(gauge.tangent_dx));
        writer.u32(std::bit_cast<std::uint32_t>(gauge.tangent_dz));
        writer.f64(gauge.distance_m);
        writer.u32(gauge.half_span_cells);
    }
}

void append_terrain_provenance(CanonicalWriter& writer,
                               const std::optional<Fluid25DTerrainCaseProvenance>& provenance) {
    writer.boolean(provenance.has_value());
    if (!provenance) {
        return;
    }
    const Fluid25DTerrainCaseProvenance& terrain = *provenance;
    validate_short_string(terrain.source_id, "terrain source id");
    validate_short_string(terrain.identity, "terrain identity");
    if (terrain.source_id.empty() || terrain.identity.empty() ||
        !cubey::asset::is_sha256_hex(terrain.elevation_sha256) ||
        !cubey::asset::is_sha256_hex(terrain.transformed_crop_sha256) ||
        !std::isfinite(terrain.sample_spacing_m) || terrain.sample_spacing_m <= 0.0F) {
        invalid("terrain provenance is malformed");
    }
    // manifest_path is deliberately excluded: moving an identical immutable
    // source tree must not change the physical input identity.
    writer.string(terrain.source_id);
    writer.string(terrain.elevation_sha256);
    writer.string(terrain.transformed_crop_sha256);
    writer.u32(terrain.crop_x);
    writer.u32(terrain.crop_z);
    writer.u32(terrain.crop_width);
    writer.u32(terrain.crop_height);
    writer.f32(terrain.sample_spacing_m);
    writer.string(terrain.identity);
}

[[nodiscard]] std::string builtin_input_sha256(const Fluid25DProjectConfig& project,
                                               const Fluid25DScenarioData& scenario,
                                               std::size_t cell_count) {
    const Fluid25DConfig& config = project.simulation;
    CanonicalWriter writer;
    writer.string("cubey.fluid25d.builtin-input.v1");

    // Numerical configuration is encoded field-by-field; do not hash struct
    // bytes because they include padding and host-native representation.
    writer.boolean(config.mass_audit);
    writer.u32(config.grid_width);
    writer.u32(config.grid_height);
    writer.f32(config.cell_size_m);
    writer.f32(config.fixed_delta_seconds);
    writer.u32(config.simulation_substeps);
    writer.f32(config.gravity_m_per_s2);
    writer.f32(config.flow_damping_per_second);
    writer.f32(config.minimum_wet_depth_m);
    optional_float(writer, config.source_active_duration_seconds);
    writer.f32(config.headwaters_source_scale);
    writer.f32(config.natural_flow_source_m3_per_s);
    optional_float(writer, config.dye_pulse_start_seconds);
    optional_float(writer, config.dye_pulse_duration_seconds);
    enum_value(writer, config.terrain_water_protocol);
    writer.f32(config.rainfall_depth_rate_m_per_s);
    writer.f32(config.sheet_initial_depth_m);
    enum_value(writer, config.scenario);
    enum_value(writer, config.solver);

    // These project-level options select immutable producer inputs or
    // deterministic startup controls; presentation-only camera/view settings
    // are intentionally omitted.
    writer.boolean(project.gpu_oracle_validation);
    writer.boolean(project.mass_audit);
    validate_short_string(project.mass_audit_control, "mass audit control");
    writer.string(project.mass_audit_control);
    writer.boolean(project.hillside_supply_response);
    writer.boolean(project.hillside_supply_gpu_controls);
    writer.boolean(project.rain_study_gpu_controls);

    writer.u32(scenario.width);
    writer.u32(scenario.height);
    writer.f32(scenario.cell_size_m);
    const auto append_marker = [&writer](std::size_t cell) {
        writer.u64(cell == kFluid25DNoCell ? std::numeric_limits<std::uint64_t>::max()
                                           : static_cast<std::uint64_t>(cell));
    };
    append_marker(scenario.source_cell);
    append_marker(scenario.secondary_source_cell);
    append_marker(scenario.outlet_cell);
    append_marker(scenario.sink_cell);
    writer.string(canonical_float_array_digest(scenario.terrain_height_m));
    writer.string(canonical_float_array_digest(scenario.initial_water_depth_m));
    writer.string(canonical_float_array_digest(scenario.source_depth_rate_m_per_s));
    writer.string(canonical_float_array_digest(scenario.sink_depth_rate_m_per_s));
    writer.string(canonical_u32_array_digest(scenario.boundary_outflow_face_mask));
    append_terrain_provenance(writer, scenario.terrain_provenance);
    append_natural_flow_metadata(writer, scenario.natural_flow_study, cell_count, scenario.width,
                                 scenario.height);
    return writer.digest();
}

void validate_builtin_scenario(const Fluid25DProjectConfig& project,
                               const Fluid25DScenarioData& scenario, std::size_t& cell_count_out) {
    const Fluid25DConfig& config = project.simulation;
    validate_fluid_25d_project_config(project);
    if (fluid_25d_selected_backend(project) != "builtin") {
        invalid("built-in metadata requires the built-in data source");
    }
    const std::size_t count = bounded_cell_count(config.grid_width, config.grid_height);
    if (scenario.width != config.grid_width || scenario.height != config.grid_height ||
        scenario.cell_size_m != config.cell_size_m || scenario.terrain_height_m.size() != count ||
        scenario.initial_water_depth_m.size() != count ||
        scenario.source_depth_rate_m_per_s.size() != count ||
        scenario.sink_depth_rate_m_per_s.size() != count ||
        scenario.boundary_outflow_face_mask.size() != count) {
        invalid("scenario geometry or input array sizes do not match the configured grid");
    }
    if (!std::isfinite(scenario.cell_size_m) || scenario.cell_size_m <= 0.0F) {
        invalid("scenario spacing must be finite and positive");
    }
    // All size checks precede array traversal and digest-buffer allocation.
    validate_float_values(scenario.terrain_height_m, "terrain bed", false);
    validate_float_values(scenario.initial_water_depth_m, "initial depth", true);
    validate_float_values(scenario.source_depth_rate_m_per_s, "source rate", true);
    validate_float_values(scenario.sink_depth_rate_m_per_s, "sink rate", true);
    validate_fluid_25d_boundary_outflow_face_mask(scenario.width, scenario.height,
                                                  scenario.boundary_outflow_face_mask);
    for (const std::size_t marker : {scenario.source_cell, scenario.secondary_source_cell,
                                     scenario.outlet_cell, scenario.sink_cell}) {
        if (marker != kFluid25DNoCell && marker >= count) {
            invalid("scenario marker cell is outside the grid");
        }
    }
    if (scenario.terrain_provenance) {
        if (scenario.terrain_provenance->crop_width != scenario.width ||
            scenario.terrain_provenance->crop_height != scenario.height ||
            scenario.terrain_provenance->sample_spacing_m != scenario.cell_size_m) {
            invalid("terrain provenance geometry does not match the scenario");
        }
    }
    if ((config.scenario == Fluid25DScenario::TerrainCase ||
         config.scenario == Fluid25DScenario::MountainSourceOutletDemo ||
         fluid_25d_is_natural_terrain_study(config.scenario) ||
         (config.scenario == Fluid25DScenario::HillsideRainStudy &&
          !project.rain_study_gpu_controls)) &&
        !scenario.terrain_provenance) {
        invalid("terrain-backed scenario is missing immutable terrain identity");
    }
    cell_count_out = count;
}

[[nodiscard]] std::string bounded_json_dump(const Json& value, std::string_view label) {
    const std::string encoded = value.dump();
    if (encoded.size() > kMaximumRecordingJsonBytes) {
        invalid(std::string(label) + " exceeds the retained recording size bound");
    }
    return encoded;
}

[[nodiscard]] const Json& required_object_member(const Json& object, std::string_view key,
                                                 std::string_view label) {
    if (!object.is_object() || !object.contains(std::string(key))) {
        invalid(std::string(label) + " is missing required immutable provenance");
    }
    return object.at(std::string(key));
}

[[nodiscard]] std::string required_sha256(const Json& object, std::string_view key,
                                          std::string_view label) {
    const Json& value = required_object_member(object, key, label);
    if (!value.is_string()) {
        invalid(std::string(label) + " digest must be a string");
    }
    const std::string result = value.get<std::string>();
    if (!cubey::asset::is_sha256_hex(result)) {
        invalid(std::string(label) + " digest is not lowercase SHA-256");
    }
    return result;
}

[[nodiscard]] std::string recording_input_sha256(const Fluid25DRecording& recording,
                                                 std::size_t cell_count) {
    const Json& provenance = recording.provenance();
    const Json& source_hashes = required_object_member(provenance, "source_sha256", "source hash");
    CanonicalWriter writer;
    writer.string("cubey.fluid25d.recording-input.v1");
    writer.u32(recording.grid_width());
    writer.u32(recording.grid_height());
    writer.f32(recording.cell_size_m());
    for (const std::string_view key :
         {"case_spec", "case_protocol", "dem_asc", "native_numerical_bed"}) {
        writer.string(key);
        writer.string(required_sha256(source_hashes, key, key));
    }

    const Json& terrain_identity =
        required_object_member(provenance, "terrain_source_identity", "terrain source identity");
    const Json& precision = required_object_member(provenance, "precision", "precision report");
    writer.string(bounded_json_dump(terrain_identity, "terrain source identity"));
    writer.string(bounded_json_dump(precision, "precision report"));
    writer.string(bounded_json_dump(recording.protocol(), "recording protocol"));

    const std::span<const Fluid25DRecordedRainKnot> rain = recording.rainfall_history();
    if (rain.empty()) {
        invalid("recording rainfall history is empty");
    }
    writer.u64(static_cast<std::uint64_t>(rain.size()));
    double previous_time = -1.0;
    for (const Fluid25DRecordedRainKnot& knot : rain) {
        if (!std::isfinite(knot.time_s) || knot.time_s < 0.0 || !std::isfinite(knot.rate_m_per_s) ||
            knot.rate_m_per_s < 0.0 || knot.time_s <= previous_time) {
            invalid("recording rainfall history is malformed");
        }
        writer.f64(knot.time_s);
        writer.f64(knot.rate_m_per_s);
        previous_time = knot.time_s;
    }

    const std::span<const float> bed = recording.bed();
    const std::span<const float> source_bed = recording.source_bed();
    if (bed.size() != cell_count || source_bed.size() != cell_count) {
        invalid("recording bed arrays do not match the bounded grid");
    }
    validate_float_values(bed, "recording solver bed", false);
    validate_float_values(source_bed, "recording source bed", false);
    writer.string(canonical_float_array_digest(source_bed));
    writer.string(canonical_float_array_digest(bed));
    return writer.digest();
}

[[nodiscard]] Fluid25DBackendCapabilities builtin_capabilities() noexcept {
    Fluid25DBackendCapabilities capabilities{};
    capabilities.solver.pause = true;
    capabilities.solver.resume = true;
    capabilities.solver.reset = true;
    capabilities.solver.stop = true;
    capabilities.solver.set_time_scale = true;
    return capabilities;
}

[[nodiscard]] Fluid25DBackendMetadata recording_metadata(const Fluid25DRecording& recording,
                                                         std::string viewer_session_id) {
    const std::size_t cell_count =
        bounded_cell_count(recording.grid_width(), recording.grid_height());
    if (!std::isfinite(recording.cell_size_m()) || recording.cell_size_m() <= 0.0F) {
        invalid("recording spacing must be finite and positive");
    }
    Fluid25DBackendMetadata metadata{};
    const bool live = recording.is_live_stream();
    metadata.kind = live ? Fluid25DBackendKind::External : Fluid25DBackendKind::Recorded;
    metadata.profile = live ? Fluid25DBackendProfile::StockExternalBridge
                            : Fluid25DBackendProfile::RecordingPlayback;
    metadata.capability_source = live ? Fluid25DCapabilitySource::ViewingOnlyBridge
                                      : Fluid25DCapabilitySource::RecordingFormat;
    metadata.id = live ? "external.stock-bridge" : "recording.playback";
    metadata.grid.width = recording.grid_width();
    metadata.grid.height = recording.grid_height();
    metadata.grid.spacing_m = recording.cell_size_m();
    metadata.grid.orientation = Fluid25DGridOrientation::RowMajorXFastestZRows;
    metadata.grid.input_sha256 = recording_input_sha256(recording, cell_count);
    metadata.grid.solver_bed_sha256 = raw_float32_le_sha256(recording.bed());
    metadata.fields.depth_m = true;
    metadata.fields.horizontal_velocity_x_m_per_s = true;
    metadata.fields.horizontal_velocity_z_m_per_s = true;
    metadata.fields.momentum_x_m2_per_s = true;
    metadata.fields.momentum_z_m2_per_s = true;
    metadata.capabilities = live ? fluid_25d_stock_external_bridge_capabilities()
                                 : fluid_25d_recording_playback_capabilities();
    metadata.session.session_id = std::move(viewer_session_id);
    metadata.session.reset_generation = 1U;
    metadata.session.frame_sequence = 0U;
    metadata.session.physical_time_s = 0.0;
    metadata.session.lifecycle = Fluid25DLifecycle::Ready;
    validate_fluid_25d_backend_metadata(metadata);
    return metadata;
}

} // namespace

Fluid25DBackendMetadata
make_fluid_25d_builtin_backend_metadata(const Fluid25DProjectConfig& config,
                                        const Fluid25DScenarioData& scenario,
                                        std::string session_id) {
    std::size_t cell_count = 0U;
    validate_builtin_scenario(config, scenario, cell_count);

    Fluid25DBackendMetadata metadata{};
    metadata.kind = Fluid25DBackendKind::Builtin;
    metadata.profile = config.simulation.solver == Fluid25DSolver::FiniteVolume
                           ? Fluid25DBackendProfile::BuiltinFiniteVolume
                           : Fluid25DBackendProfile::BuiltinVirtualPipes;
    metadata.capability_source = Fluid25DCapabilitySource::BuiltinAdapter;
    metadata.id = config.simulation.solver == Fluid25DSolver::FiniteVolume
                      ? "builtin.finite-volume"
                      : "builtin.virtual-pipes";
    metadata.grid.width = scenario.width;
    metadata.grid.height = scenario.height;
    metadata.grid.spacing_m = scenario.cell_size_m;
    metadata.grid.orientation = Fluid25DGridOrientation::RowMajorXFastestZRows;
    metadata.grid.input_sha256 = builtin_input_sha256(config, scenario, cell_count);
    metadata.grid.solver_bed_sha256 = raw_float32_le_sha256(scenario.terrain_height_m);
    metadata.fields.depth_m = true;
    metadata.fields.horizontal_velocity_x_m_per_s = true;
    metadata.fields.horizontal_velocity_z_m_per_s = true;
    metadata.fields.water_ledger = true;
    if (config.simulation.solver == Fluid25DSolver::FiniteVolume) {
        metadata.fields.momentum_x_m2_per_s = true;
        metadata.fields.momentum_z_m2_per_s = true;
    } else {
        metadata.fields.face_discharge_m3_per_s = true;
    }
    if (fluid_25d_transport_inspection_available(config.simulation)) {
        metadata.fields.tracer_depth_equivalent_m = true;
        metadata.fields.tracer_ledger = true;
    }
    metadata.capabilities = builtin_capabilities();
    metadata.session.session_id = std::move(session_id);
    metadata.session.reset_generation = 1U;
    metadata.session.frame_sequence = 0U;
    metadata.session.physical_time_s = 0.0;
    metadata.session.lifecycle = Fluid25DLifecycle::Ready;
    validate_fluid_25d_backend_metadata(metadata);
    return metadata;
}

Fluid25DBackendMetadata
make_fluid_25d_recording_backend_metadata(const Fluid25DRecording& recording,
                                          std::string viewer_session_id) {
    return recording_metadata(recording, std::move(viewer_session_id));
}

std::string fluid_25d_new_backend_session_id() {
    static std::atomic<std::uint64_t> sequence{0U};
    static const std::uint64_t process_nonce = [] {
        std::uint64_t result = 0U;
        try {
            std::random_device random;
            result = (static_cast<std::uint64_t>(random()) << 32U) ^ random();
        } catch (const std::exception&) {
            // The process-local sequence and clocks provide a collision-resistant
            // fallback if the platform has no random-device provider.
        }
        const auto system_now = std::chrono::system_clock::now().time_since_epoch().count();
        return result ^ static_cast<std::uint64_t>(system_now);
    }();
    const std::uint64_t serial = sequence.fetch_add(1U, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::uint64_t clock_bits = static_cast<std::uint64_t>(now);
    const std::uint64_t first = process_nonce ^ clock_bits ^ (serial * 0x9e3779b97f4a7c15ULL);
    const std::uint64_t second = serial ^ std::rotl(clock_bits, 23) ^ std::rotl(process_nonce, 41);
    constexpr char kHex[] = "0123456789abcdef";
    std::string result(32U, '0');
    for (std::size_t index = 0U; index < 16U; ++index) {
        const unsigned shift = static_cast<unsigned>((15U - index) * 4U);
        result[index] = kHex[(first >> shift) & 0xfU];
        result[16U + index] = kHex[(second >> shift) & 0xfU];
    }
    return result;
}

} // namespace cubey::projects::fluid::fluid_25d
