#include "fluid_25d_backend_adapters.h"

#include "../../fluid_25d/fluid_25d_project_config.h"
#include "fluid_25d_config.h"
#include "fluid_25d_recording.h"
#include "fluid_25d_scenarios.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace cubey::projects::fluid::fluid_25d;
using Json = nlohmann::json;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Callable> void require_throws(Callable&& callable, const char* message) {
    bool threw = false;
    try {
        callable();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, message);
}

[[nodiscard]] std::string digest(std::span<const std::byte> bytes) {
    return cubey::asset::sha256_hex(bytes);
}

[[nodiscard]] std::vector<std::byte> encode_float32_le(std::span<const float> values) {
    std::vector<std::byte> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (const float value : values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        for (unsigned shift = 0U; shift < 32U; shift += 8U) {
            bytes.push_back(static_cast<std::byte>((bits >> shift) & 0xffU));
        }
    }
    return bytes;
}

[[nodiscard]] std::string expected_float32_le_sha256(std::span<const float> values) {
    const std::vector<std::byte> bytes = encode_float32_le(values);
    return digest(bytes);
}

void write_bytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output || !output.write(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("failed to write backend adapter test payload");
    }
}

void write_json(const std::filesystem::path& path, const Json& value) {
    std::ofstream output(path);
    if (!output) {
        throw std::runtime_error("failed to write backend adapter test manifest");
    }
    output << value.dump(2) << '\n';
    if (!output) {
        throw std::runtime_error("failed to flush backend adapter test manifest");
    }
}

struct TemporaryRecording {
    std::filesystem::path directory{};

    ~TemporaryRecording() {
        std::error_code error;
        if (!directory.empty()) {
            std::filesystem::remove_all(directory, error);
        }
    }

    TemporaryRecording() = default;
    TemporaryRecording(const TemporaryRecording&) = delete;
    TemporaryRecording& operator=(const TemporaryRecording&) = delete;
    TemporaryRecording(TemporaryRecording&& other) noexcept
        : directory(std::move(other.directory)) {
        other.directory.clear();
    }
    TemporaryRecording& operator=(TemporaryRecording&&) = delete;
};

[[nodiscard]] TemporaryRecording
make_recording_fixture(bool live, std::size_t frame_count, char audit_digest = 'a',
                       char case_result_digest = 'b', double rain_rate_m_per_s = 3.0e-6,
                       std::uint64_t revision = 1U, std::string producer_state = "running",
                       std::string producer_session = "0123456789abcdef0123456789abcdef") {
    require(frame_count >= 1U && frame_count <= 4U, "test fixture frame count is out of range");
    static std::uint64_t serial = 0U;
    TemporaryRecording fixture{};
    fixture.directory =
        std::filesystem::temp_directory_path() /
        ("cubey-fluid25d-backend-adapter-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(serial++));
    std::filesystem::create_directories(fixture.directory);

    constexpr std::array<float, 4U> bed{100.0F, 101.0F, 102.0F, 103.0F};
    constexpr std::array<float, 4U> source_bed{99.0F, 100.0F, 101.0F, 102.0F};
    const std::vector<std::byte> bed_bytes = encode_float32_le(bed);
    const std::vector<std::byte> source_bed_bytes = encode_float32_le(source_bed);
    write_bytes(fixture.directory / "bed.f32", bed_bytes);
    write_bytes(fixture.directory / "source-bed.f32", source_bed_bytes);

    Json frames = Json::array();
    Json raw_hashes = Json::array();
    constexpr std::array<float, 12U> zero_frame{};
    const std::vector<std::byte> frame_bytes = encode_float32_le(zero_frame);
    const std::string frame_hash = digest(frame_bytes);
    for (std::size_t index = 0U; index < frame_count; ++index) {
        const double time = static_cast<double>(index) * 10.0;
        const std::string relative = "frames/frame-00000" + std::to_string(index) + ".f32";
        write_bytes(fixture.directory / relative, frame_bytes);
        Json frame{
            {"time_s", time},           {"water_volume_m3", 0.0},          {"max_depth_m", 0.0},
            {"max_speed_m_per_s", 0.0}, {"dry_nonzero_momentum_cells", 0}, {"path", relative},
            {"sha256", frame_hash}};
        if (live) {
            frame["published_unix_s"] = 1000.0 + time;
        }
        frames.push_back(std::move(frame));
        raw_hashes.push_back(Json{{"time_s", time},
                                  {"h", std::string(64U, 'a')},
                                  {"hUx", std::string(64U, 'b')},
                                  {"hUy", std::string(64U, 'c')}});
    }

    Json source_hashes{{"case_spec", std::string(64U, '1')},
                       {"case_protocol", std::string(64U, '2')},
                       {"dem_asc", std::string(64U, '3')},
                       {"native_numerical_bed", std::string(64U, '4')}};
    if (live) {
        source_hashes["pre_solver_audit"] = std::string(64U, audit_digest);
    } else {
        source_hashes["case_result"] = std::string(64U, case_result_digest);
    }
    const std::string repeated_digest(64U, '5');
    Json protocol{{"cols", 2},
                  {"rows", 2},
                  {"dx_m", 10.0},
                  {"duration_s", 30.0},
                  {"output_interval_s", 10.0},
                  {"rain_rate_m_per_s", rain_rate_m_per_s},
                  {"rainfall_history", Json::array({Json::array({0.0, rain_rate_m_per_s}),
                                                    Json::array({30.0, rain_rate_m_per_s})})}};
    Json manifest{
        {"schema", live ? "cubey.fluid25d.stream.v1" : "cubey.fluid25d.recording.v1"},
        {"encoding", "float32-little-endian"},
        {"fields", Json::array({"h_m", "qx_m2_per_s", "qz_m2_per_s"})},
        {"field_layout", "planar"},
        {"grid", Json{{"width", 2},
                      {"height", 2},
                      {"cell_size_m", 10.0},
                      {"storage_order", "row-major"},
                      {"row_direction", "world-z-positive"},
                      {"sample_location", "cell-center"}}},
        {"bed", Json{{"path", "bed.f32"}, {"sha256", digest(bed_bytes)}}},
        {"source_bed", Json{{"path", "source-bed.f32"}, {"sha256", digest(source_bed_bytes)}}},
        {"frames", std::move(frames)},
        {"protocol", std::move(protocol)},
        {"provenance",
         Json{{"source_sha256", std::move(source_hashes)},
              {"raw_asc_sha256", std::move(raw_hashes)},
              {"terrain_source_identity", Json{{"elevation_sha256", repeated_digest},
                                               {"fixture_sha256", std::string(64U, '6')},
                                               {"manifest_sha256", std::string(64U, '7')},
                                               {"transformed_crop_sha256", std::string(64U, '8')},
                                               {"transformed_halo_sha256", std::string(64U, '9')}}},
              {"precision", Json{{"native_numerical_bed_format", "%g (six significant digits)"},
                                 {"native_bed_vs_source_bed_max_abs_error_m", 1.0},
                                 {"native_bed_vs_source_bed_rms_error_m", 0.5},
                                 {"native_output_ascii_decimal_places", 6},
                                 {"native_output_ascii_rounding_half_step", 0.0000005},
                                 {"native_world_z_momentum_sign", -1}}}}}};
    if (live) {
        manifest["session_id"] = std::move(producer_session);
        manifest["revision"] = revision;
        manifest["producer"] =
            Json{{"state", std::move(producer_state)},
                 {"pid", 1234},
                 {"message", "test state is intentionally mutable"},
                 {"latest_native_time_s", static_cast<double>(frame_count - 1U) * 10.0},
                 {"native_running", false}};
        write_json(fixture.directory / "stream.json", manifest);
    } else {
        write_json(fixture.directory / "recording.json", manifest);
    }
    return fixture;
}

[[nodiscard]] Fluid25DProjectConfig builtin_config(std::uint32_t width = 6U,
                                                   std::uint32_t height = 3U) {
    Fluid25DProjectConfig project{};
    project.backend = "builtin";
    project.simulation.grid_width = width;
    project.simulation.grid_height = height;
    project.simulation.cell_size_m = 10.0F;
    project.simulation.scenario = Fluid25DScenario::RiverCatchment;
    project.simulation.solver = Fluid25DSolver::VirtualPipes;
    validate_fluid_25d_project_config(project);
    return project;
}

[[nodiscard]] Fluid25DScenarioData make_scenario(const Fluid25DProjectConfig& project) {
    return make_fluid_25d_scenario(project.simulation.scenario, project.simulation.grid_width,
                                   project.simulation.grid_height, project.simulation.cell_size_m,
                                   project.simulation.headwaters_source_scale);
}

void test_builtin_field_availability_capabilities_and_startup_state() {
    Fluid25DProjectConfig vp = builtin_config();
    const Fluid25DScenarioData vp_scenario = make_scenario(vp);
    const Fluid25DBackendMetadata vp_metadata =
        make_fluid_25d_builtin_backend_metadata(vp, vp_scenario, "vp-session-01");
    require(vp_metadata.kind == Fluid25DBackendKind::Builtin &&
                vp_metadata.profile == Fluid25DBackendProfile::BuiltinVirtualPipes &&
                vp_metadata.fields.depth_m && vp_metadata.fields.horizontal_velocity_x_m_per_s &&
                vp_metadata.fields.horizontal_velocity_z_m_per_s &&
                vp_metadata.fields.face_discharge_m3_per_s &&
                !vp_metadata.fields.momentum_x_m2_per_s &&
                !vp_metadata.fields.momentum_z_m2_per_s && vp_metadata.fields.water_ledger &&
                !vp_metadata.fields.tracer_depth_equivalent_m && !vp_metadata.fields.tracer_ledger,
            "virtual-pipes field availability is incorrect");
    require(vp_metadata.capabilities.solver.pause && vp_metadata.capabilities.solver.resume &&
                vp_metadata.capabilities.solver.reset && vp_metadata.capabilities.solver.stop &&
                vp_metadata.capabilities.solver.set_time_scale &&
                !vp_metadata.capabilities.solver.step && !vp_metadata.capabilities.solver.seek &&
                !vp_metadata.capabilities.solver.set_rain &&
                !vp_metadata.capabilities.playback.pause,
            "built-in controls overclaim step, seek, rain, or playback");
    require(vp_metadata.session.lifecycle == Fluid25DLifecycle::Ready &&
                vp_metadata.session.reset_generation == 1U &&
                vp_metadata.session.frame_sequence == 0U &&
                vp_metadata.session.physical_time_s == 0.0 &&
                vp_metadata.grid.orientation == Fluid25DGridOrientation::RowMajorXFastestZRows,
            "built-in startup metadata is not a pre-first-frame Ready session");
    require(vp_metadata.grid.solver_bed_sha256 ==
                expected_float32_le_sha256(vp_scenario.terrain_height_m),
            "solver bed digest is not canonical row-major little-endian float32");

    Fluid25DProjectConfig fv = vp;
    fv.simulation.solver = Fluid25DSolver::FiniteVolume;
    fv.simulation.scenario = Fluid25DScenario::SourceOutletDemo;
    fv.simulation.grid_width = 8U;
    fv.simulation.grid_height = 5U;
    fv.simulation.dye_pulse_start_seconds = 2.0F;
    fv.simulation.dye_pulse_duration_seconds = 8.0F;
    validate_fluid_25d_project_config(fv);
    const Fluid25DScenarioData fv_scenario = make_scenario(fv);
    const Fluid25DBackendMetadata fv_metadata =
        make_fluid_25d_builtin_backend_metadata(fv, fv_scenario, "fv-session-01");
    require(fv_metadata.profile == Fluid25DBackendProfile::BuiltinFiniteVolume &&
                fv_metadata.fields.momentum_x_m2_per_s && fv_metadata.fields.momentum_z_m2_per_s &&
                !fv_metadata.fields.face_discharge_m3_per_s && fv_metadata.fields.water_ledger &&
                fv_metadata.fields.tracer_depth_equivalent_m && fv_metadata.fields.tracer_ledger,
            "finite-volume momentum or eligible tracer metadata is incorrect");

    fv.simulation.dye_pulse_duration_seconds.reset();
    fv.simulation.dye_pulse_start_seconds.reset();
    const Fluid25DScenarioData fv_no_dye_scenario = make_scenario(fv);
    const Fluid25DBackendMetadata fv_no_dye_metadata =
        make_fluid_25d_builtin_backend_metadata(fv, fv_no_dye_scenario, "fv-session-02");
    require(!fv_no_dye_metadata.fields.tracer_depth_equivalent_m &&
                !fv_no_dye_metadata.fields.tracer_ledger,
            "unconfigured dye fields were advertised");
}

void test_builtin_identity_is_stable_and_tracks_immutable_physics() {
    Fluid25DProjectConfig project = builtin_config();
    const Fluid25DScenarioData scenario = make_scenario(project);
    const Fluid25DBackendMetadata first =
        make_fluid_25d_builtin_backend_metadata(project, scenario, "input-session-a");
    const Fluid25DBackendMetadata repeat =
        make_fluid_25d_builtin_backend_metadata(project, scenario, "input-session-b");
    require(first.grid.input_sha256 == repeat.grid.input_sha256 &&
                first.grid.solver_bed_sha256 == repeat.grid.solver_bed_sha256,
            "same built-in immutable input produced unstable digests");

    Fluid25DProjectConfig changed_config = project;
    changed_config.simulation.gravity_m_per_s2 += 0.25F;
    const Fluid25DBackendMetadata changed_setting =
        make_fluid_25d_builtin_backend_metadata(changed_config, scenario, "input-session-c");
    require(changed_setting.grid.input_sha256 != first.grid.input_sha256,
            "changed numerical setting did not change the complete input identity");

    Fluid25DScenarioData changed_scenario = scenario;
    changed_scenario.initial_water_depth_m[0] += 0.125F;
    const Fluid25DBackendMetadata changed_depth =
        make_fluid_25d_builtin_backend_metadata(project, changed_scenario, "input-session-d");
    require(changed_depth.grid.input_sha256 != first.grid.input_sha256 &&
                changed_depth.grid.solver_bed_sha256 == first.grid.solver_bed_sha256,
            "changed initial depth did not alter only the complete input identity");
    changed_scenario = scenario;
    changed_scenario.terrain_height_m[0] += 0.125F;
    const Fluid25DBackendMetadata changed_bed =
        make_fluid_25d_builtin_backend_metadata(project, changed_scenario, "input-session-e");
    require(changed_bed.grid.input_sha256 != first.grid.input_sha256 &&
                changed_bed.grid.solver_bed_sha256 != first.grid.solver_bed_sha256,
            "changed solver bed did not alter both input and canonical bed identities");
}

void test_builtin_rejects_bounds_geometry_and_nonfinite_arrays() {
    Fluid25DProjectConfig project = builtin_config();
    Fluid25DScenarioData scenario = make_scenario(project);
    project.simulation.grid_width = kFluid25DBackendMaximumDimension + 1U;
    project.simulation.grid_height = 2U;
    require_throws(
        [&] {
            (void)make_fluid_25d_builtin_backend_metadata(project, scenario, "bad-size-session");
        },
        "oversized grid was not rejected before array digest allocation");

    project = builtin_config();
    scenario = make_scenario(project);
    scenario.terrain_height_m[0] = std::numeric_limits<float>::quiet_NaN();
    require_throws(
        [&] {
            (void)make_fluid_25d_builtin_backend_metadata(project, scenario, "bad-bed-session");
        },
        "non-finite solver bed value was accepted");

    scenario = make_scenario(project);
    scenario.width += 1U;
    require_throws(
        [&] {
            (void)make_fluid_25d_builtin_backend_metadata(project, scenario, "bad-grid-session");
        },
        "scenario geometry mismatch was accepted");
}

void test_recording_and_stock_bridge_have_viewer_scoped_playback_metadata() {
    TemporaryRecording saved_fixture = make_recording_fixture(false, 4U);
    Fluid25DRecording saved(saved_fixture.directory);
    const Fluid25DBackendMetadata recording_metadata =
        make_fluid_25d_recording_backend_metadata(saved, "saved-viewer-session");
    const Fluid25DBackendCapabilities playback = fluid_25d_recording_playback_capabilities();
    require(recording_metadata.kind == Fluid25DBackendKind::Recorded &&
                recording_metadata.profile == Fluid25DBackendProfile::RecordingPlayback &&
                recording_metadata.capability_source == Fluid25DCapabilitySource::RecordingFormat &&
                recording_metadata.session.session_id == "saved-viewer-session" &&
                recording_metadata.session.lifecycle == Fluid25DLifecycle::Ready &&
                recording_metadata.session.physical_time_s == 0.0 &&
                recording_metadata.fields.depth_m &&
                recording_metadata.fields.horizontal_velocity_x_m_per_s &&
                recording_metadata.fields.horizontal_velocity_z_m_per_s &&
                recording_metadata.fields.momentum_x_m2_per_s &&
                recording_metadata.fields.momentum_z_m2_per_s &&
                !recording_metadata.fields.face_discharge_m3_per_s &&
                !recording_metadata.fields.water_ledger &&
                recording_metadata.capabilities.playback.seek == playback.playback.seek &&
                recording_metadata.capabilities.playback.step == playback.playback.step &&
                !recording_metadata.capabilities.solver.pause &&
                !recording_metadata.capabilities.solver.set_rain,
            "recording metadata confused viewer playback with producer controls");
    require(recording_metadata.grid.solver_bed_sha256 == expected_float32_le_sha256(saved.bed()),
            "recording solver-bed digest includes framing instead of exact raw LE float bytes");
    require(recording_metadata.session.session_id != saved.session_id(),
            "recording viewer reused the source producer/recording identity");

    TemporaryRecording short_stream_fixture = make_recording_fixture(
        true, 3U, 'a', 'b', 3.0e-6, 4U, "running", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    TemporaryRecording extended_stream_fixture = make_recording_fixture(
        true, 4U, 'f', 'b', 3.0e-6, 9U, "running", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    Fluid25DRecording short_stream(short_stream_fixture.directory, true);
    Fluid25DRecording extended_stream(extended_stream_fixture.directory, true);
    const Fluid25DBackendMetadata short_stream_metadata =
        make_fluid_25d_recording_backend_metadata(short_stream, "live-viewer-session");
    const Fluid25DBackendMetadata extended_stream_metadata =
        make_fluid_25d_recording_backend_metadata(extended_stream, "another-viewer-session");
    require(short_stream_metadata.kind == Fluid25DBackendKind::External &&
                short_stream_metadata.profile == Fluid25DBackendProfile::StockExternalBridge &&
                short_stream_metadata.capability_source ==
                    Fluid25DCapabilitySource::ViewingOnlyBridge &&
                short_stream_metadata.capabilities.playback.pause &&
                short_stream_metadata.capabilities.playback.seek &&
                short_stream_metadata.capabilities.playback.set_time_scale &&
                !short_stream_metadata.capabilities.solver.pause &&
                !short_stream_metadata.capabilities.solver.stop &&
                !short_stream_metadata.capabilities.solver.set_rain,
            "stock external bridge claimed native producer controls or hid viewer controls");
    require(short_stream_metadata.session.session_id == "live-viewer-session" &&
                short_stream_metadata.session.session_id != short_stream.session_id(),
            "live bridge metadata did not keep viewer ownership separate from producer session");
    require(short_stream_metadata.grid.input_sha256 == extended_stream_metadata.grid.input_sha256 &&
                short_stream_metadata.grid.solver_bed_sha256 ==
                    extended_stream_metadata.grid.solver_bed_sha256,
            "stream prefix, producer state, or producer ID changed immutable input identity");

    TemporaryRecording different_rain_fixture = make_recording_fixture(false, 4U, 'a', 'b', 4.0e-6);
    Fluid25DRecording different_rain(different_rain_fixture.directory);
    const Fluid25DBackendMetadata different_rain_metadata =
        make_fluid_25d_recording_backend_metadata(different_rain, "rain-viewer-session");
    require(different_rain_metadata.grid.input_sha256 != recording_metadata.grid.input_sha256,
            "changed immutable recording rainfall did not alter input identity");

    TemporaryRecording changed_result_fixture = make_recording_fixture(false, 4U, 'a', 'c');
    Fluid25DRecording changed_result(changed_result_fixture.directory);
    const Fluid25DBackendMetadata changed_result_metadata =
        make_fluid_25d_recording_backend_metadata(changed_result, "result-viewer-session");
    require(changed_result_metadata.grid.input_sha256 == recording_metadata.grid.input_sha256,
            "recording case_result audit hash leaked into immutable physics identity");
}

void test_builtin_identity_buffer_is_bounded() {
    Fluid25DProjectConfig project = builtin_config(132U, 129U);
    Fluid25DScenarioData scenario = make_scenario(project);
    Fluid25DNaturalFlowStudyMetadata metadata{};
    metadata.candidate_id = "bounded-test";
    metadata.expected_outlet_x_min = 0U;
    metadata.expected_outlet_x_max = 0U;
    metadata.expected_outlet_z_min = 0U;
    metadata.expected_outlet_z_max = 0U;
    const std::string long_gauge_name(4096U, 'g');
    metadata.gauges.reserve(16'500U);
    for (std::uint32_t index = 0U; index < 16'500U; ++index) {
        Fluid25DNaturalFlowGauge gauge{};
        gauge.name = long_gauge_name;
        gauge.distance_m = static_cast<double>(index);
        metadata.gauges.push_back(std::move(gauge));
    }
    scenario.natural_flow_study = std::move(metadata);
    require_throws(
        [&] {
            (void)make_fluid_25d_builtin_backend_metadata(project, scenario, "bounded-session");
        },
        "large natural-flow metadata exceeded the canonical identity buffer bound");
}

void test_generated_session_ids_are_bounded_unique_identifiers() {
    const std::string first = fluid_25d_new_backend_session_id();
    const std::string second = fluid_25d_new_backend_session_id();
    const auto valid = [](std::string_view value) {
        return value.size() == 32U && std::all_of(value.begin(), value.end(), [](char character) {
                   return (character >= '0' && character <= '9') ||
                          (character >= 'a' && character <= 'f');
               });
    };
    require(valid(first) && valid(second) && first != second,
            "generated backend session ids are invalid or not unique in-process");
}

} // namespace

int main() {
    try {
        test_builtin_field_availability_capabilities_and_startup_state();
        test_builtin_identity_is_stable_and_tracks_immutable_physics();
        test_builtin_rejects_bounds_geometry_and_nonfinite_arrays();
        test_recording_and_stock_bridge_have_viewer_scoped_playback_metadata();
        test_builtin_identity_buffer_is_bounded();
        test_generated_session_ids_are_bounded_unique_identifiers();
    } catch (const std::exception& exception) {
        std::cerr << "fluid 2.5D backend adapter test failed: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "fluid 2.5D backend adapter tests passed\n";
    return 0;
}
