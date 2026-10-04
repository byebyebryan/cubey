#include "fluid_25d_recording.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using cubey::projects::fluid::fluid_25d::Fluid25DRecordedRainPhase;
using cubey::projects::fluid::fluid_25d::Fluid25DRecording;
using cubey::projects::fluid::fluid_25d::Fluid25DRecordingPlayback;
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

void require_close(double actual, double expected, double tolerance, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

[[nodiscard]] std::string sha256(std::span<const std::uint8_t> bytes) {
    return cubey::asset::sha256_hex(std::as_bytes(bytes));
}

[[nodiscard]] std::vector<std::uint8_t> encode_float32_le(std::span<const float> values) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (const float value : values) {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        bytes.push_back(static_cast<std::uint8_t>(bits & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 16U) & 0xffU));
        bytes.push_back(static_cast<std::uint8_t>((bits >> 24U) & 0xffU));
    }
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    if (!stream || !stream.write(reinterpret_cast<const char*>(bytes.data()),
                                 static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("failed to write recording test payload");
    }
}

void write_manifest(const std::filesystem::path& path, const Json& document) {
    std::ofstream stream(path);
    if (!stream) {
        throw std::runtime_error("failed to write recording test manifest");
    }
    stream << document.dump(2) << '\n';
}

struct FrameFixture {
    std::array<float, 4U> depth{};
    std::array<float, 4U> qx{};
    std::array<float, 4U> qz{};
};

[[nodiscard]] std::vector<std::uint8_t> encoded_frame(const FrameFixture& frame) {
    std::array<float, 12U> planar{};
    std::copy(frame.depth.begin(), frame.depth.end(), planar.begin());
    std::copy(frame.qx.begin(), frame.qx.end(), planar.begin() + 4);
    std::copy(frame.qz.begin(), frame.qz.end(), planar.begin() + 8);
    return encode_float32_le(planar);
}

[[nodiscard]] Json frame_stats(const FrameFixture& frame, double time_s) {
    constexpr double area = 100.0;
    double volume = 0.0;
    float max_depth = 0.0F;
    float max_speed = 0.0F;
    for (std::size_t index = 0U; index < frame.depth.size(); ++index) {
        const float h = frame.depth[index];
        const float qx = frame.qx[index];
        const float qz = frame.qz[index];
        volume += static_cast<double>(h) * area;
        max_depth = std::max(max_depth, h);
        if (h > 0.0F) {
            max_speed = std::max(max_speed, static_cast<float>(std::hypot(qx, qz) / h));
        }
    }
    return Json{{"time_s", time_s},
                {"water_volume_m3", volume},
                {"max_depth_m", max_depth},
                {"max_speed_m_per_s", max_speed},
                {"dry_nonzero_momentum_cells", 0}};
}

struct TestRecording {
    std::filesystem::path directory{};
    Json manifest{};
    std::array<FrameFixture, 4U> frames{};

    ~TestRecording() {
        std::error_code error;
        if (!directory.empty()) {
            std::filesystem::remove_all(directory, error);
        }
    }

    TestRecording() = default;
    TestRecording(const TestRecording&) = delete;
    TestRecording& operator=(const TestRecording&) = delete;
    TestRecording(TestRecording&& other) noexcept
        : directory(std::move(other.directory)), manifest(std::move(other.manifest)),
          frames(other.frames) {
        other.directory.clear();
    }
    TestRecording& operator=(TestRecording&&) = delete;
};

[[nodiscard]] TestRecording make_recording() {
    static std::uint64_t serial = 0U;
    TestRecording fixture{};
    fixture.directory =
        std::filesystem::temp_directory_path() /
        ("cubey-fluid25d-recording-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(serial++));
    std::filesystem::create_directories(fixture.directory);

    fixture.frames = {
        FrameFixture{},
        FrameFixture{
            {0.1F, 0.0F, 0.5F, 0.25F}, {0.2F, 0.0F, 0.0F, 0.25F}, {0.3F, 0.0F, 0.5F, -0.25F}},
        FrameFixture{
            {0.0F, 0.2F, 0.4F, 0.1F}, {0.0F, -0.1F, 0.2F, 0.0F}, {0.0F, 0.2F, -0.1F, 0.0F}},
        FrameFixture{
            {0.2F, 0.3F, 0.1F, 0.4F}, {0.1F, 0.0F, -0.1F, 0.0F}, {0.0F, 0.1F, 0.0F, -0.2F}},
    };

    const std::array<float, 4U> bed_values{100.0F, 101.0F, 102.0F, 103.0F};
    const std::array<float, 4U> source_values{99.0F, 100.0F, 101.0F, 102.0F};
    const auto bed_bytes = encode_float32_le(bed_values);
    const auto source_bytes = encode_float32_le(source_values);
    write_bytes(fixture.directory / "bed.f32", bed_bytes);
    write_bytes(fixture.directory / "source-bed.f32", source_bytes);

    Json frame_entries = Json::array();
    Json raw_hashes = Json::array();
    for (std::size_t index = 0U; index < fixture.frames.size(); ++index) {
        const std::string relative = "frames/frame-" + std::string(index < 10U ? "00000" : "") +
                                     std::to_string(index) + ".f32";
        const std::vector<std::uint8_t> bytes = encoded_frame(fixture.frames[index]);
        write_bytes(fixture.directory / relative, bytes);
        Json entry = frame_stats(fixture.frames[index], static_cast<double>(index) * 10.0);
        entry["path"] = relative;
        entry["sha256"] = sha256(bytes);
        frame_entries.push_back(entry);
        raw_hashes.push_back(Json{{"time_s", static_cast<double>(index) * 10.0},
                                  {"h", std::string(64U, 'a')},
                                  {"hUx", std::string(64U, 'b')},
                                  {"hUy", std::string(64U, 'c')}});
    }

    fixture.manifest = Json{
        {"schema", "cubey.fluid25d.recording.v1"},
        {"encoding", "float32-little-endian"},
        {"fields", Json::array({"h_m", "qx_m2_per_s", "qz_m2_per_s"})},
        {"field_layout", "planar"},
        {"grid", Json{{"width", 2},
                      {"height", 2},
                      {"cell_size_m", 10.0},
                      {"storage_order", "row-major"},
                      {"row_direction", "world-z-positive"},
                      {"sample_location", "cell-center"}}},
        {"bed", Json{{"path", "bed.f32"}, {"sha256", sha256(bed_bytes)}}},
        {"source_bed", Json{{"path", "source-bed.f32"}, {"sha256", sha256(source_bytes)}}},
        {"frames", frame_entries},
        {"protocol", Json{{"cols", 2},
                          {"rows", 2},
                          {"dx_m", 10.0},
                          {"duration_s", 30.0},
                          {"output_interval_s", 10.0},
                          {"rain_rate_m_per_s", 3.3333333333333333e-6}}},
        {"provenance",
         Json{{"source_sha256", Json{{"case_spec", std::string(64U, '1')},
                                     {"case_protocol", std::string(64U, '2')},
                                     {"case_result", std::string(64U, '3')},
                                     {"dem_asc", std::string(64U, '4')},
                                     {"native_numerical_bed", std::string(64U, '5')}}},
              {"raw_asc_sha256", raw_hashes},
              {"terrain_source_identity", Json{{"elevation_sha256", std::string(64U, '5')},
                                               {"fixture_sha256", std::string(64U, '6')},
                                               {"manifest_sha256", std::string(64U, '7')},
                                               {"transformed_crop_sha256", std::string(64U, '8')},
                                               {"transformed_halo_sha256", std::string(64U, '9')}}},
              {"precision", Json{{"native_numerical_bed_format", "%g (six significant digits)"},
                                 {"native_bed_vs_source_bed_max_abs_error_m", 1.0},
                                 {"native_bed_vs_source_bed_rms_error_m", 0.5},
                                 {"native_output_ascii_decimal_places", 6},
                                 {"native_output_ascii_rounding_half_step", 0.0000005},
                                 {"native_world_z_momentum_sign", -1}}}}},
    };
    write_manifest(fixture.directory / "recording.json", fixture.manifest);
    return fixture;
}

void test_reader_loads_checked_planar_states_and_bounded_cache() {
    TestRecording fixture = make_recording();
    Fluid25DRecording recording(fixture.directory);
    require(recording.grid_width() == 2U && recording.grid_height() == 2U,
            "recording grid dimensions were not read");
    require_close(recording.cell_size_m(), 10.0, 0.0, "recording cell size was not read");
    require(recording.bed().size() == 4U && recording.source_bed().size() == 4U,
            "bed planes were not read");
    require_close(recording.bed()[2], 102.0, 0.0, "native bed value is wrong");
    require_close(recording.source_bed()[1], 100.0, 0.0, "source bed value is wrong");
    require(recording.frame_count() == 4U && recording.times_s().size() == 4U,
            "recording timestamps were not read");
    require_close(recording.rain_rate_mm_per_hour(), 12.0, 1.0e-12,
                  "protocol rain rate conversion is wrong");
    const auto legacy_rain = recording.rain_status_at(15.0);
    require(legacy_rain.phase == Fluid25DRecordedRainPhase::On,
            "legacy constant-rain recording did not report rain On");
    require_close(legacy_rain.rate_mm_per_hour, 12.0, 1.0e-12,
                  "legacy constant-rain schedule changed its rate");
    require_close(legacy_rain.cumulative_scheduled_rain_depth_mm, 0.05, 1.0e-12,
                  "legacy constant-rain cumulative schedule is wrong");
    require(recording.rain_status_at(-10.0).time_s == 0.0 &&
                recording.rain_status_at(100.0).time_s == 30.0,
            "rain-history query did not clamp to its recorded range");
    require(recording.protocol().at("duration_s") == 30.0,
            "copied protocol metadata is not exposed");
    require(recording.provenance().at("terrain_source_identity").at("elevation_sha256") ==
                std::string(64U, '5'),
            "native source metadata is not exposed");

    require(recording.frame_index_at(-1.0) == 0U && recording.frame_index_at(9.999) == 0U &&
                recording.frame_index_at(10.0) == 1U && recording.frame_index_at(100.0) == 3U,
            "frame lookup does not floor and clamp");
    require_throws(
        [&] { (void)recording.frame_index_at(std::numeric_limits<double>::quiet_NaN()); },
        "frame lookup accepted NaN time");

    const auto first = recording.frame(1U);
    require_close(first->depth_m[0], 0.1, 1.0e-7, "depth plane is wrong");
    require_close(first->momentum[0].x_m2_per_s, 0.2, 1.0e-7, "qx plane is wrong");
    require_close(first->momentum[0].y_m2_per_s, 0.3, 1.0e-7, "world-Z sign conversion is wrong");
    require_close(first->velocity[0].x_m_per_s, 2.0, 1.0e-6, "derived x velocity is wrong");
    require_close(first->velocity[0].y_m_per_s, 3.0, 1.0e-6, "derived world-Z velocity is wrong");
    require_close(first->stats.water_volume_m3, 85.0, 1.0e-5, "water volume statistic is wrong");
    require(first->stats.dry_nonzero_momentum_cells == 0U, "dry momentum count should be zero");
    require(recording.frame(0U)->time_s == 0.0, "time-zero frame did not load");
    (void)recording.frame(2U);
    (void)recording.frame(3U);
    require(recording.cached_frame_count() == 3U, "frame cache exceeded or missed its bound");
    (void)recording.frame(0U);
    require(recording.cached_frame_count() == 3U, "cache reload changed its bound");
    require(first->time_s == 10.0, "an evicted shared frame did not remain valid");
    require_throws([&] { (void)recording.frame(4U); }, "out-of-range frame was accepted");

    Fluid25DRecording moved(std::move(recording));
    require(moved.grid_width() == 2U && moved.frame_count() == 4U,
            "recording move construction lost loaded metadata");
}

void test_reader_rejects_bad_schema_paths_geometry_and_timestamps() {
    {
        TestRecording fixture = make_recording();
        fixture.manifest["schema"] = "unsupported";
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "unsupported recording schema was accepted");
    }
    {
        TestRecording fixture = make_recording();
        fixture.manifest["frames"][1]["path"] = "../escape.f32";
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "parent-escaping frame path was accepted");
    }
    {
        TestRecording fixture = make_recording();
        fixture.manifest["protocol"]["cols"] = 3;
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "mismatched protocol geometry was accepted");
    }
    {
        TestRecording fixture = make_recording();
        fixture.manifest["frames"][2]["time_s"] = 10.0;
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "duplicate frame timestamp was accepted");
    }
    {
        TestRecording fixture = make_recording();
        fixture.manifest["frames"][3]["time_s"] = 29.0;
        fixture.manifest["provenance"]["raw_asc_sha256"][3]["time_s"] = 29.0;
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "final timestamp mismatch was accepted");
    }
    {
        TestRecording fixture = make_recording();
        std::ofstream stream(fixture.directory / "recording.json");
        stream << "{";
        stream.close();
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "malformed JSON was accepted");
    }
}

void test_reader_checks_hash_nonfinite_depth_and_velocity_representation() {
    {
        TestRecording fixture = make_recording();
        const std::filesystem::path frame_path = fixture.directory / "frames/frame-000001.f32";
        std::fstream stream(frame_path, std::ios::binary | std::ios::in | std::ios::out);
        char original = 0;
        stream.read(&original, 1);
        stream.seekp(0);
        const char changed = static_cast<char>(original ^ 0x01);
        stream.write(&changed, 1);
        stream.close();
        Fluid25DRecording recording(fixture.directory);
        require_throws([&] { (void)recording.frame(1U); }, "corrupt frame checksum was accepted");
    }
    {
        TestRecording fixture = make_recording();
        FrameFixture corrupt = fixture.frames[1];
        corrupt.depth[0] = std::numeric_limits<float>::quiet_NaN();
        const auto bytes = encoded_frame(corrupt);
        write_bytes(fixture.directory / "frames/frame-000001.f32", bytes);
        fixture.manifest["frames"][1]["sha256"] = sha256(bytes);
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        Fluid25DRecording recording(fixture.directory);
        require_throws([&] { (void)recording.frame(1U); },
                       "non-finite field value with valid checksum was accepted");
    }
    {
        TestRecording fixture = make_recording();
        FrameFixture corrupt = fixture.frames[1];
        corrupt.depth[0] = 0.0F;
        const auto bytes = encoded_frame(corrupt);
        write_bytes(fixture.directory / "frames/frame-000001.f32", bytes);
        fixture.manifest["frames"][1]["sha256"] = sha256(bytes);
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        Fluid25DRecording recording(fixture.directory);
        require_throws([&] { (void)recording.frame(1U); },
                       "zero-depth nonzero-momentum state was accepted");
    }
    {
        TestRecording fixture = make_recording();
        FrameFixture corrupt = fixture.frames[1];
        corrupt.depth[0] = 1.0e-38F;
        corrupt.qx[0] = std::numeric_limits<float>::max();
        const auto bytes = encoded_frame(corrupt);
        write_bytes(fixture.directory / "frames/frame-000001.f32", bytes);
        fixture.manifest["frames"][1]["sha256"] = sha256(bytes);
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        Fluid25DRecording recording(fixture.directory);
        require_throws([&] { (void)recording.frame(1U); },
                       "unrepresentable derived velocity was accepted");
    }
}

void test_recorded_rainfall_history_interpolation_integral_phase_and_seek() {
    TestRecording fixture = make_recording();
    const double rate = 3.3333333333333333e-6;
    fixture.manifest["protocol"]["rainfall_history"] =
        Json::array({Json::array({0.0, rate}), Json::array({10.0, rate}), Json::array({20.0, 0.0}),
                     Json::array({30.0, 0.0})});
    write_manifest(fixture.directory / "recording.json", fixture.manifest);

    Fluid25DRecording recording(fixture.directory);
    require(recording.rainfall_history().size() == 4U,
            "recorded rainfall history knots were not retained");
    const auto on = recording.rain_status_at(5.0);
    require(on.phase == Fluid25DRecordedRainPhase::On,
            "positive constant rainfall segment was not On");
    require_close(on.rate_mm_per_hour, 12.0, 1.0e-10, "constant rain rate is wrong");
    require_close(on.cumulative_scheduled_rain_depth_mm, 0.0166666666666667, 1.0e-12,
                  "constant rain cumulative depth is wrong");

    const auto taper = recording.rain_status_at(15.0);
    require(taper.phase == Fluid25DRecordedRainPhase::Tapering,
            "falling positive rainfall segment was not Tapering");
    require_close(taper.rate_mm_per_hour, 6.0, 1.0e-10, "linear rain taper interpolation is wrong");
    require_close(taper.cumulative_scheduled_rain_depth_mm, 0.0458333333333333, 1.0e-12,
                  "piecewise-linear rain integral is wrong during taper");

    const auto off = recording.rain_status_at(20.0);
    require(off.phase == Fluid25DRecordedRainPhase::Off && off.rate_mm_per_hour == 0.0,
            "zero rainfall knot was not Off");
    require_close(off.cumulative_scheduled_rain_depth_mm, 0.05, 1.0e-12,
                  "taper-to-zero cumulative integral is wrong");
    const auto beyond = recording.rain_status_at(100.0);
    require(beyond.time_s == 30.0 && beyond.phase == Fluid25DRecordedRainPhase::Off,
            "rain history did not clamp after its terminal time");
    require_throws(
        [&] { (void)recording.rain_status_at(std::numeric_limits<double>::quiet_NaN()); },
        "rain-history query accepted NaN time");
    require(std::string(fluid_25d_recorded_rain_phase_name(taper.phase)) == "Tapering",
            "rain phase label is wrong");

    Fluid25DRecordingPlayback playback(recording.times_s());
    playback.set_paused(false);
    playback.seek(20.0);
    require(playback.paused() &&
                recording.rain_status_at(playback.time_s()).phase == Fluid25DRecordedRainPhase::Off,
            "rain-zero seek did not pause at the recorded Off state");
}

void test_reader_rejects_malformed_recorded_rainfall_histories() {
    const double rate = 3.3333333333333333e-6;
    const std::array<Json, 6U> malformed{
        Json::array(),
        Json::array({Json::array({1.0, rate}), Json::array({30.0, 0.0})}),
        Json::array(
            {Json::array({0.0, rate}), Json::array({10.0, -1.0}), Json::array({30.0, 0.0})}),
        Json::array({Json::array({0.0, rate}), Json::array({10.0, 0.0}), Json::array({10.0, 0.0}),
                     Json::array({30.0, 0.0})}),
        Json::array({Json::array({0.0, rate * 2.0}), Json::array({30.0, 0.0})}),
        Json::array({Json::array({0.0, rate}), Json::array({29.0, 0.0})}),
    };
    for (const Json& history : malformed) {
        TestRecording fixture = make_recording();
        fixture.manifest["protocol"]["rainfall_history"] = history;
        write_manifest(fixture.directory / "recording.json", fixture.manifest);
        require_throws([&] { Fluid25DRecording invalid(fixture.directory); },
                       "malformed recorded rainfall history was accepted");
    }
}

void test_pure_playback_clock_controls_and_end_state() {
    const std::array<double, 4U> times{0.0, 10.0, 20.0, 30.0};
    Fluid25DRecordingPlayback playback(times);
    require(playback.paused() && !playback.ended() && playback.frame_index() == 0U,
            "playback did not start paused at frame zero");
    playback.advance(5.0);
    require(playback.time_s() == 0.0, "paused playback advanced");
    playback.set_rate(2.0);
    playback.set_paused(false);
    playback.advance(2.0);
    require_close(playback.time_s(), 4.0, 1.0e-12, "playback rate was not applied");
    require(playback.frame_index() == 0U, "playback frame lookup did not floor");
    playback.next();
    require(playback.time_s() == 10.0 && playback.paused() && !playback.ended(),
            "next did not pause on the next saved state");
    playback.next();
    require(playback.time_s() == 20.0, "next did not advance one saved state");
    playback.previous();
    require(playback.time_s() == 10.0, "previous did not go to the prior saved state");
    playback.seek(-2.0);
    require(playback.time_s() == 0.0 && playback.paused(), "seek did not clamp and pause");
    require_throws([&] { playback.seek(std::numeric_limits<double>::infinity()); },
                   "non-finite seek was accepted");
    require_throws([&] { playback.advance(std::numeric_limits<double>::quiet_NaN()); },
                   "non-finite wall delta was accepted");
    require_throws([&] { playback.set_rate(0.124); }, "rate below minimum was accepted");
    require_throws([&] { playback.set_rate(301.0); }, "rate above maximum was accepted");
    playback.set_rate(0.125);
    require_close(playback.rate(), 0.125, 0.0, "minimum supported rate was not accepted");
    playback.seek(30.0);
    require(playback.ended() && playback.paused(),
            "seeking to the end did not mark playback ended");
    playback.set_paused(false);
    playback.advance(100.0);
    require(playback.ended() && playback.time_s() == 30.0,
            "unpausing an ended recording restarted its clock");
    playback.restart();
    require(playback.time_s() == 0.0 && playback.paused() && !playback.ended(),
            "explicit restart did not reset the playback clock");
    playback.set_paused(false);
    playback.advance(1.0e100);
    require(playback.time_s() == 30.0 && playback.ended() && playback.paused(),
            "playback did not clamp and end at its final saved time");
    require_throws(
        [] {
            const std::array<double, 0U> empty{};
            Fluid25DRecordingPlayback invalid(empty);
        },
        "empty saved times were accepted");
    require_throws(
        [] {
            const std::array<double, 3U> duplicate{0.0, 5.0, 5.0};
            Fluid25DRecordingPlayback invalid(duplicate);
        },
        "duplicate saved times were accepted");
}

void test_live_prefix_identity_lifecycle_and_checked_frames() {
    auto fixture = make_recording();
    const Json complete = fixture.manifest;
    Json live = complete;
    live["schema"] = "cubey.fluid25d.stream.v1";
    live["session_id"] = std::string(32U, 'a');
    live["revision"] = 1;
    live["producer"] = Json{{"state", "running"},
                            {"pid", 123},
                            {"message", "running"},
                            {"native_running", true},
                            {"latest_native_time_s", 10.0}};
    live["provenance"]["source_sha256"].erase("case_result");
    live["provenance"]["source_sha256"]["pre_solver_audit"] = std::string(64U, '3');
    for (auto& frame : live["frames"])
        frame["published_unix_s"] = 1000.0;
    live["frames"].erase(live["frames"].begin() + 1, live["frames"].end());
    live["provenance"]["raw_asc_sha256"].erase(live["provenance"]["raw_asc_sha256"].begin() + 1,
                                               live["provenance"]["raw_asc_sha256"].end());
    const auto path = fixture.directory / "stream.json";
    write_manifest(path, live);
    Fluid25DRecording first(path, true);
    require(first.is_live_stream() && first.frame_count() == 1U &&
                first.producer_state() == "running",
            "valid live prefix was rejected");
    require(first.frame(0)->published_unix_s == 1000.0, "live ready time was lost");
    require_throws([&] { Fluid25DRecording wrong(path); },
                   "recording reader silently accepted live schema");
    Json grown = live;
    grown["revision"] = 2;
    grown["producer"]["latest_native_time_s"] = 20.0;
    Json next_frame = complete["frames"][1];
    next_frame["published_unix_s"] = 1001.0;
    grown["frames"].push_back(next_frame);
    grown["provenance"]["raw_asc_sha256"].push_back(complete["provenance"]["raw_asc_sha256"][1]);
    write_manifest(path, grown);
    Fluid25DRecording second(path, true);
    second.validate_successor_of(first);
    require(second.frame_count() == 2U && second.frame(1)->time_s == 10.0,
            "live prefix growth changed field decoding");
    const auto rejects = [&](Json bad, const char* label) {
        write_manifest(path, bad);
        require_throws(
            [&] {
                Fluid25DRecording candidate(path, true);
                candidate.validate_successor_of(first);
            },
            label);
    };
    auto bad = grown;
    bad["session_id"] = std::string(32U, 'b');
    rejects(bad, "stale/new session substituted");
    bad = grown;
    bad["revision"] = 0;
    rejects(bad, "revision regression accepted");
    bad = grown;
    bad["revision"] = 1;
    rejects(bad, "revision reused for different metadata");
    bad = grown;
    bad["frames"][0]["published_unix_s"] = 999.0;
    rejects(bad, "published history mutated");
    bad = grown;
    bad["producer"]["state"] = "completed";
    rejects(bad, "incomplete prefix claimed completion");
    bad = grown;
    bad["producer"]["latest_native_time_s"] = 0.0;
    rejects(bad, "native time regressed behind prefix");
    bad = grown;
    bad["provenance"]["source_sha256"]["pre_solver_audit"] = std::string(64U, '4');
    rejects(bad, "source audit changed");
    bad = grown;
    bad["frames"][1]["path"] = "../escape.f32";
    rejects(bad, "live payload path escaped session");
    bad = grown;
    bad["producer"]["state"] = "failed";
    bad["producer"]["native_running"] = false;
    bad["revision"] = 3;
    write_manifest(path, bad);
    Fluid25DRecording failed(path, true);
    failed.validate_successor_of(second);
    write_manifest(path, grown);
    require_throws(
        [&] {
            Fluid25DRecording resumed(path, true);
            resumed.validate_successor_of(failed);
        },
        "terminated producer resumed");

    Fluid25DRecordingPlayback clock(first.times_s());
    clock.set_paused(false);
    clock.advance(1.0);
    require(clock.ended(), "single-prefix clock did not reach available boundary");
    clock.extend(second.times_s());
    clock.set_paused(false);
    clock.advance(0.01);
    require(!clock.ended() && clock.time_s() > 0.0, "growing timeline did not resume");
    require_throws([&] { clock.extend(first.times_s()); }, "growing timeline shrank");
}

} // namespace

int main() {
    try {
        test_reader_loads_checked_planar_states_and_bounded_cache();
        test_reader_rejects_bad_schema_paths_geometry_and_timestamps();
        test_reader_checks_hash_nonfinite_depth_and_velocity_representation();
        test_recorded_rainfall_history_interpolation_integral_phase_and_seek();
        test_reader_rejects_malformed_recorded_rainfall_histories();
        test_pure_playback_clock_controls_and_end_state();
        test_live_prefix_identity_lifecycle_and_checked_frames();
    } catch (const std::exception& error) {
        std::cerr << "fluid_25d_recording_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
