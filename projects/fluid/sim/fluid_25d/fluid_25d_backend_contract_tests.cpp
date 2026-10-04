#include "fluid_25d_backend_contract.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

[[nodiscard]] std::string digest(char value) {
    return std::string(64U, value);
}

[[nodiscard]] Fluid25DBackendMetadata metadata_for(Fluid25DBackendProfile profile) {
    Fluid25DBackendMetadata metadata{};
    metadata.profile = profile;
    switch (profile) {
    case Fluid25DBackendProfile::BuiltinVirtualPipes:
        metadata.kind = Fluid25DBackendKind::Builtin;
        metadata.capability_source = Fluid25DCapabilitySource::BuiltinAdapter;
        metadata.id = "builtin.virtual-pipes";
        break;
    case Fluid25DBackendProfile::BuiltinFiniteVolume:
        metadata.kind = Fluid25DBackendKind::Builtin;
        metadata.capability_source = Fluid25DCapabilitySource::BuiltinAdapter;
        metadata.id = "builtin.finite-volume";
        metadata.fields.momentum_x_m2_per_s = true;
        metadata.fields.momentum_z_m2_per_s = true;
        metadata.fields.water_ledger = true;
        break;
    case Fluid25DBackendProfile::RecordingPlayback:
        metadata.kind = Fluid25DBackendKind::Recorded;
        metadata.capability_source = Fluid25DCapabilitySource::RecordingFormat;
        metadata.id = "recording.playback";
        metadata.capabilities = fluid_25d_recording_playback_capabilities();
        metadata.fields.momentum_x_m2_per_s = true;
        metadata.fields.momentum_z_m2_per_s = true;
        break;
    case Fluid25DBackendProfile::StockExternalBridge:
        metadata.kind = Fluid25DBackendKind::External;
        metadata.capability_source = Fluid25DCapabilitySource::ViewingOnlyBridge;
        metadata.id = "external.stock-bridge";
        metadata.capabilities = fluid_25d_stock_external_bridge_capabilities();
        break;
    case Fluid25DBackendProfile::ExternalService:
        metadata.kind = Fluid25DBackendKind::External;
        metadata.capability_source = Fluid25DCapabilitySource::ServiceHandshake;
        metadata.id = "external.service.test";
        break;
    }
    metadata.fields.depth_m = true;
    metadata.fields.horizontal_velocity_x_m_per_s = true;
    metadata.fields.horizontal_velocity_z_m_per_s = true;
    metadata.grid = {.width = 2U,
                     .height = 2U,
                     .spacing_m = 30.0,
                     .orientation = Fluid25DGridOrientation::RowMajorXFastestZRows,
                     .input_sha256 = digest('a'),
                     .solver_bed_sha256 = digest('b')};
    metadata.session = {.session_id = "session-001",
                        .reset_generation = 1U,
                        .frame_sequence = 0U,
                        .physical_time_s = 0.0,
                        .lifecycle = Fluid25DLifecycle::Ready,
                        .failure_message = {}};
    return metadata;
}

[[nodiscard]] Fluid25DControlCommand
command(std::uint64_t id, std::uint64_t generation, Fluid25DCommandKind kind,
        Fluid25DControlDomain domain = Fluid25DControlDomain::Solver,
        std::optional<double> value = std::nullopt) {
    return {.command_id = id,
            .session_id = "session-001",
            .reset_generation = generation,
            .domain = domain,
            .kind = kind,
            .value = value};
}

[[nodiscard]] Fluid25DCommandAcknowledgement
acknowledgement(std::uint64_t id, std::uint64_t generation, Fluid25DAcknowledgementState state,
                Fluid25DLifecycle lifecycle, std::optional<double> time = std::nullopt,
                std::string message = {}) {
    return {.command_id = id,
            .session_id = "session-001",
            .reset_generation = generation,
            .state = state,
            .application_time_s = time,
            .lifecycle = lifecycle,
            .message = std::move(message)};
}

[[nodiscard]] Fluid25DFrameHeader frame(std::string session, std::uint64_t generation,
                                        std::uint64_t sequence, double time,
                                        Fluid25DLifecycle lifecycle = Fluid25DLifecycle::Running,
                                        std::string failure_message = {}) {
    return {.session_id = std::move(session),
            .reset_generation = generation,
            .sequence = sequence,
            .physical_time_s = time,
            .lifecycle = lifecycle,
            .failure_message = std::move(failure_message)};
}

void test_metadata_and_codec_roundtrips() {
    Fluid25DBackendMetadata metadata = metadata_for(Fluid25DBackendProfile::BuiltinFiniteVolume);
    metadata.capabilities.solver.pause = true;
    metadata.capabilities.solver.resume = true;
    metadata.capabilities.solver.reset = true;
    metadata.capabilities.solver.step = true;
    metadata.capabilities.solver.set_rain = true;
    metadata.capabilities.solver.set_time_scale = true;

    const std::string encoded_metadata = encode_fluid_25d_backend_metadata_json(metadata);
    const Fluid25DBackendMetadata decoded_metadata =
        decode_fluid_25d_backend_metadata_json(encoded_metadata);
    require(
        decoded_metadata.kind == Fluid25DBackendKind::Builtin &&
            decoded_metadata.profile == Fluid25DBackendProfile::BuiltinFiniteVolume &&
            decoded_metadata.grid.width == 2U && decoded_metadata.grid.height == 2U &&
            decoded_metadata.grid.spacing_m == 30.0 &&
            decoded_metadata.grid.orientation == Fluid25DGridOrientation::RowMajorXFastestZRows &&
            decoded_metadata.grid.input_sha256 == digest('a') &&
            decoded_metadata.grid.solver_bed_sha256 == digest('b') &&
            decoded_metadata.fields.momentum_x_m2_per_s &&
            decoded_metadata.fields.momentum_z_m2_per_s && decoded_metadata.fields.water_ledger &&
            !decoded_metadata.fields.face_discharge_m3_per_s &&
            decoded_metadata.capabilities.solver.set_rain &&
            decoded_metadata.session.session_id == "session-001",
        "backend metadata did not round-trip");

    const Fluid25DFrameHeader expected_frame = frame("session-001", 1U, 1U, 0.25);
    require(decode_fluid_25d_frame_header_json(encode_fluid_25d_frame_header_json(expected_frame))
                    .sequence == expected_frame.sequence,
            "frame header did not round-trip");

    const Fluid25DControlCommand rain =
        command(3U, 1U, Fluid25DCommandKind::SetRain, Fluid25DControlDomain::Solver, 0.001);
    const Fluid25DControlCommand decoded_rain =
        decode_fluid_25d_control_command_json(encode_fluid_25d_control_command_json(rain));
    require(decoded_rain.command_id == 3U && decoded_rain.value == 0.001 &&
                decoded_rain.kind == Fluid25DCommandKind::SetRain,
            "control command did not round-trip");

    const Fluid25DCommandAcknowledgement applied = acknowledgement(
        3U, 1U, Fluid25DAcknowledgementState::Applied, Fluid25DLifecycle::Running, 0.25);
    const Fluid25DCommandAcknowledgement decoded_applied =
        decode_fluid_25d_command_acknowledgement_json(
            encode_fluid_25d_command_acknowledgement_json(applied));
    require(decoded_applied.command_id == applied.command_id &&
                decoded_applied.application_time_s == applied.application_time_s &&
                decoded_applied.reset_generation == applied.reset_generation,
            "command acknowledgement did not round-trip");
}

void test_strict_json_rejection() {
    const std::string valid = encode_fluid_25d_backend_metadata_json(
        metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes));
    Json document = Json::parse(valid);
    document["schema"] = "cubey.fluid25d.backend-session.v2";
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(document.dump()); },
                   "unknown schema version was accepted");

    document = Json::parse(valid);
    document["type"] = "unknown";
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(document.dump()); },
                   "unknown document type was accepted");

    document = Json::parse(valid);
    document["backend"]["profile"] = "mystery-solver";
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(document.dump()); },
                   "unknown backend profile was accepted");

    document = Json::parse(valid);
    document["fields"].erase("tracer_ledger");
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(document.dump()); },
                   "missing availability field was accepted");

    document = Json::parse(valid);
    document["extra"] = true;
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(document.dump()); },
                   "unknown metadata field was accepted");

    const std::string duplicate_key = "{\"schema\":\"cubey.fluid25d.backend-session.v1\","
                                      "\"schema\":\"cubey.fluid25d.backend-session.v1\"}";
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(duplicate_key); },
                   "duplicate JSON keys were accepted");
    const std::string duplicate_nested_key =
        "{\"schema\":\"cubey.fluid25d.backend-session.v1\",\"type\":\"metadata\","
        "\"backend\":{\"kind\":\"builtin\",\"kind\":\"recorded\"}}";
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(duplicate_nested_key); },
                   "duplicate nested JSON keys were accepted");

    const std::string deeply_nested = std::string(9U, '[') + "0" + std::string(9U, ']');
    require_throws([&] { (void)decode_fluid_25d_backend_metadata_json(deeply_nested); },
                   "excessively nested JSON was accepted");

    require_throws(
        [] {
            const std::string oversized(kFluid25DBackendMaximumJsonBytes + 1U, ' ');
            (void)decode_fluid_25d_backend_metadata_json(oversized);
        },
        "oversized JSON was parsed");
}

void test_grid_identity_and_size_limits() {
    Fluid25DBackendMetadata metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.grid.width = kFluid25DBackendMaximumDimension;
    metadata.grid.height = 256U;
    validate_fluid_25d_backend_metadata(metadata);

    metadata.grid.height = 257U;
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "grid above maximum cell count was accepted");
    metadata.grid.width = 1U;
    metadata.grid.height = 2U;
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "grid dimension below two was accepted");

    metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.grid.spacing_m = 0.0;
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "zero cell spacing was accepted");
    metadata.grid.spacing_m = std::numeric_limits<double>::infinity();
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "infinite cell spacing was accepted");

    metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.grid.input_sha256[0] = 'A';
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "noncanonical input hash was accepted");
    metadata.grid.input_sha256 = digest('a');
    metadata.grid.solver_bed_sha256.clear();
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "missing solver-bed identity was accepted");

    metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.fields.horizontal_velocity_z_m_per_s = false;
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "missing common logical velocity field was accepted");

    metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.fields = {};
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "undeclared required logical fields defaulted to available");

    metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.session.physical_time_s = -0.1;
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "negative session physical time was accepted");
    metadata.session.physical_time_s = std::numeric_limits<double>::quiet_NaN();
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "nonfinite session physical time was accepted");

    metadata = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    metadata.id = std::string(kFluid25DBackendMaximumIdentifierBytes + 1U, 'b');
    require_throws([&] { validate_fluid_25d_backend_metadata(metadata); },
                   "oversized backend id was accepted");
}

void test_capability_profiles() {
    const Fluid25DBackendMetadata vp = metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes);
    const Fluid25DBackendMetadata fv = metadata_for(Fluid25DBackendProfile::BuiltinFiniteVolume);
    require(vp.profile != fv.profile && vp.id != fv.id,
            "built-in virtual-pipes and finite-volume profiles collapsed together");
    require(!fluid_25d_command_supported(vp.capabilities, Fluid25DControlDomain::Solver,
                                         Fluid25DCommandKind::SetRain) &&
                !fluid_25d_command_supported(fv.capabilities, Fluid25DControlDomain::Solver,
                                             Fluid25DCommandKind::SetRain),
            "rain control was inferred from a built-in solver profile");

    const Fluid25DBackendMetadata recording =
        metadata_for(Fluid25DBackendProfile::RecordingPlayback);
    require(!fluid_25d_command_supported(recording.capabilities, Fluid25DControlDomain::Solver,
                                         Fluid25DCommandKind::Pause) &&
                fluid_25d_command_supported(recording.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Pause) &&
                fluid_25d_command_supported(recording.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Seek) &&
                fluid_25d_command_supported(recording.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Step) &&
                !fluid_25d_command_supported(recording.capabilities,
                                             Fluid25DControlDomain::Playback,
                                             Fluid25DCommandKind::SetRain),
            "recording playback capabilities are conflated with solver controls");
    validate_fluid_25d_backend_metadata(recording);
    Fluid25DSessionGuard recording_guard(recording);
    require_throws(
        [&] {
            recording_guard.observe_command(command(1U, 1U, Fluid25DCommandKind::SetRain,
                                                    Fluid25DControlDomain::Solver, 0.001));
        },
        "unsupported recording solver command was accepted");

    const Fluid25DBackendMetadata stock = metadata_for(Fluid25DBackendProfile::StockExternalBridge);
    require(!fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Solver,
                                         Fluid25DCommandKind::Pause) &&
                !fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Solver,
                                             Fluid25DCommandKind::SetRain) &&
                fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Pause) &&
                fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Resume) &&
                fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Reset) &&
                fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Step) &&
                fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::Seek) &&
                fluid_25d_command_supported(stock.capabilities, Fluid25DControlDomain::Playback,
                                            Fluid25DCommandKind::SetTimeScale),
            "stock bridge viewer controls were conflated with solver controls");
    validate_fluid_25d_backend_metadata(stock);

    Fluid25DBackendMetadata service = metadata_for(Fluid25DBackendProfile::ExternalService);
    service.capabilities.solver.pause = true;
    service.capabilities.solver.set_rain = true;
    validate_fluid_25d_backend_metadata(service);
    require(fluid_25d_command_supported(service.capabilities, Fluid25DControlDomain::Solver,
                                        Fluid25DCommandKind::SetRain),
            "handshake-declared service capability was discarded");

    Fluid25DBackendMetadata invalid_profile = service;
    invalid_profile.capability_source = Fluid25DCapabilitySource::BuiltinAdapter;
    require_throws([&] { validate_fluid_25d_backend_metadata(invalid_profile); },
                   "service capability without handshake provenance was accepted");

    invalid_profile = recording;
    invalid_profile.capabilities.solver.pause = true;
    require_throws([&] { validate_fluid_25d_backend_metadata(invalid_profile); },
                   "recording advertised solver pause");

    invalid_profile = service;
    invalid_profile.capabilities.solver.seek = true;
    require_throws([&] { validate_fluid_25d_backend_metadata(invalid_profile); },
                   "solver domain advertised playback seek");
    invalid_profile = service;
    invalid_profile.capabilities.playback.set_rain = true;
    require_throws([&] { validate_fluid_25d_backend_metadata(invalid_profile); },
                   "playback domain advertised solver rainfall control");
}

void test_command_units_and_acknowledgement_codec() {
    const Fluid25DControlCommand zero_rain =
        command(1U, 1U, Fluid25DCommandKind::SetRain, Fluid25DControlDomain::Solver, 0.0);
    validate_fluid_25d_control_command(zero_rain);
    require_throws(
        [] {
            validate_fluid_25d_control_command(command(1U, 1U, Fluid25DCommandKind::SetRain,
                                                       Fluid25DControlDomain::Solver, -0.001));
        },
        "negative rain rate was accepted");
    require_throws(
        [] {
            validate_fluid_25d_control_command(command(1U, 1U, Fluid25DCommandKind::SetRain,
                                                       Fluid25DControlDomain::Solver,
                                                       std::numeric_limits<double>::infinity()));
        },
        "nonfinite rain rate was accepted");
    require_throws(
        [] {
            validate_fluid_25d_control_command(command(1U, 1U, Fluid25DCommandKind::SetRain,
                                                       Fluid25DControlDomain::Playback, 0.1));
        },
        "playback rain-rate command was accepted");
    require_throws(
        [] {
            validate_fluid_25d_control_command(command(1U, 1U, Fluid25DCommandKind::SetTimeScale,
                                                       Fluid25DControlDomain::Solver, 0.0));
        },
        "zero time scale was accepted");
    require_throws(
        [] {
            validate_fluid_25d_control_command(
                command(1U, 1U, Fluid25DCommandKind::Seek, Fluid25DControlDomain::Playback, -1.0));
        },
        "negative seek time was accepted");
    require_throws(
        [] {
            validate_fluid_25d_control_command(
                command(1U, 1U, Fluid25DCommandKind::Seek, Fluid25DControlDomain::Solver, 1.0));
        },
        "solver seek was accepted");
    require_throws(
        [] {
            validate_fluid_25d_control_command(
                command(1U, 1U, Fluid25DCommandKind::Pause, Fluid25DControlDomain::Solver, 1.0));
        },
        "parameter on a parameterless command was accepted");

    const Fluid25DCommandAcknowledgement accepted =
        acknowledgement(1U, 1U, Fluid25DAcknowledgementState::Accepted, Fluid25DLifecycle::Ready);
    validate_fluid_25d_command_acknowledgement(accepted);
    require_throws(
        [] {
            validate_fluid_25d_command_acknowledgement(acknowledgement(
                1U, 1U, Fluid25DAcknowledgementState::Accepted, Fluid25DLifecycle::Ready, 0.0));
        },
        "Accepted acknowledgement reported an application time");
    require_throws(
        [] {
            validate_fluid_25d_command_acknowledgement(acknowledgement(
                1U, 1U, Fluid25DAcknowledgementState::Applied, Fluid25DLifecycle::Ready));
        },
        "Applied acknowledgement omitted actual application time");
    require_throws(
        [] {
            validate_fluid_25d_command_acknowledgement(acknowledgement(
                1U, 1U, Fluid25DAcknowledgementState::Applied, Fluid25DLifecycle::Ready, -0.1));
        },
        "Applied acknowledgement accepted negative application time");
    require_throws(
        [] {
            validate_fluid_25d_command_acknowledgement(
                acknowledgement(1U, 1U, Fluid25DAcknowledgementState::Applied,
                                Fluid25DLifecycle::Ready, std::numeric_limits<double>::infinity()));
        },
        "Applied acknowledgement accepted nonfinite application time");
    require_throws(
        [] {
            validate_fluid_25d_command_acknowledgement(
                acknowledgement(1U, 1U, Fluid25DAcknowledgementState::Rejected,
                                Fluid25DLifecycle::Ready, std::nullopt, ""));
        },
        "Rejected acknowledgement omitted its reason");

    const std::string unknown_state =
        "{\"schema\":\"cubey.fluid25d.backend-session.v1\",\"type\":\"ack\","
        "\"command_id\":1,\"session_id\":\"session-001\",\"reset_generation\":1,"
        "\"state\":\"queued\",\"application_time_s\":null,\"lifecycle\":\"ready\","
        "\"message\":\"\"}";
    require_throws([&] { (void)decode_fluid_25d_command_acknowledgement_json(unknown_state); },
                   "unknown acknowledgement state was accepted");
}

void test_frame_guard_and_generation_changes() {
    Fluid25DSessionGuard guard(metadata_for(Fluid25DBackendProfile::BuiltinVirtualPipes));
    guard.accept_frame(frame("session-001", 1U, 1U, 0.5));
    guard.accept_frame(frame("session-001", 1U, 2U, 1.0, Fluid25DLifecycle::Paused));
    require_throws([&] { guard.accept_frame(frame("session-001", 1U, 2U, 1.1)); },
                   "duplicate frame sequence was accepted");
    require_throws([&] { guard.accept_frame(frame("stale-session", 1U, 3U, 1.1)); },
                   "frame from another session was accepted");
    require_throws([&] { guard.accept_frame(frame("session-001", 2U, 3U, 0.0)); },
                   "unannounced future generation was accepted");
    require_throws([&] { guard.accept_frame(frame("session-001", 1U, 3U, 0.9)); },
                   "physical time regression was accepted");

    guard.begin_generation(2U, 0.0);
    require(guard.session().reset_generation == 2U && guard.session().frame_sequence == 2U &&
                guard.session().physical_time_s == 0.0 && !guard.failed(),
            "explicit generation change did not reset time while preserving sequence");
    require_throws([&] { guard.accept_frame(frame("session-001", 1U, 3U, 1.1)); },
                   "old-generation frame survived reset");
    guard.accept_frame(frame("session-001", 2U, 3U, 0.1));

    guard.accept_frame(frame("session-001", 2U, 4U, 0.2, Fluid25DLifecycle::Completed));
    require_throws([&] { guard.accept_frame(frame("session-001", 2U, 5U, 0.3)); },
                   "completed session accepted another frame in the same generation");
}

void test_command_ack_guard_and_reset_invalidation() {
    Fluid25DBackendMetadata metadata = metadata_for(Fluid25DBackendProfile::ExternalService);
    metadata.capabilities.solver.pause = true;
    metadata.capabilities.solver.resume = true;
    metadata.capabilities.solver.reset = true;
    metadata.capabilities.solver.set_rain = true;
    Fluid25DSessionGuard guard(metadata);

    Fluid25DControlCommand pause =
        command(1U, 1U, Fluid25DCommandKind::Pause, Fluid25DControlDomain::Solver);
    Fluid25DControlCommand wrong_session = pause;
    wrong_session.session_id = "other-session";
    require_throws([&] { guard.observe_command(wrong_session); },
                   "command from another session was accepted");
    Fluid25DControlCommand stale_generation = pause;
    stale_generation.reset_generation = 0U;
    require_throws([&] { guard.observe_command(stale_generation); },
                   "zero-generation command was accepted");
    guard.observe_command(pause);
    require_throws([&] { guard.observe_command(pause); }, "duplicate command id was accepted");
    require_throws([&] { guard.observe_command(command(2U, 1U, Fluid25DCommandKind::Resume)); },
                   "second command was accepted while one was outstanding");

    guard.accept_acknowledgement(
        acknowledgement(1U, 1U, Fluid25DAcknowledgementState::Accepted, Fluid25DLifecycle::Ready));
    require_throws(
        [&] {
            guard.accept_acknowledgement(acknowledgement(
                1U, 1U, Fluid25DAcknowledgementState::Accepted, Fluid25DLifecycle::Ready));
        },
        "duplicate Accepted acknowledgement was accepted");
    guard.accept_acknowledgement(acknowledgement(1U, 1U, Fluid25DAcknowledgementState::Applied,
                                                 Fluid25DLifecycle::Paused, 0.5));
    require(guard.session().lifecycle == Fluid25DLifecycle::Paused &&
                guard.session().physical_time_s == 0.5,
            "Applied Pause did not update lifecycle/time");
    require_throws(
        [&] {
            guard.accept_acknowledgement(acknowledgement(
                1U, 1U, Fluid25DAcknowledgementState::Applied, Fluid25DLifecycle::Paused, 0.5));
        },
        "duplicate terminal acknowledgement was accepted");

    guard.accept_frame(frame("session-001", 1U, 1U, 0.5, Fluid25DLifecycle::Paused));
    Fluid25DControlCommand reset =
        command(2U, 1U, Fluid25DCommandKind::Reset, Fluid25DControlDomain::Solver);
    guard.observe_command(reset);
    guard.accept_acknowledgement(acknowledgement(2U, 2U, Fluid25DAcknowledgementState::Applied,
                                                 Fluid25DLifecycle::Ready, 0.0));
    require(guard.session().reset_generation == 2U &&
                guard.session().lifecycle == Fluid25DLifecycle::Ready &&
                guard.last_command_id() == 2U,
            "Applied Reset did not start a new generation");
    require_throws([&] { guard.accept_frame(frame("session-001", 1U, 2U, 0.6)); },
                   "stale-generation frame survived an Applied Reset");
    guard.accept_frame(frame("session-001", 2U, 2U, 0.0, Fluid25DLifecycle::Running));

    Fluid25DControlCommand next =
        command(3U, 2U, Fluid25DCommandKind::SetRain, Fluid25DControlDomain::Solver, 0.002);
    guard.observe_command(next);
    require_throws(
        [&] {
            guard.accept_acknowledgement(acknowledgement(
                3U, 1U, Fluid25DAcknowledgementState::Applied, Fluid25DLifecycle::Running, 0.1));
        },
        "stale-generation command acknowledgement was accepted");
    Fluid25DCommandAcknowledgement wrong_ack_session =
        acknowledgement(3U, 2U, Fluid25DAcknowledgementState::Accepted, Fluid25DLifecycle::Running);
    wrong_ack_session.session_id = "other-session";
    require_throws([&] { guard.accept_acknowledgement(wrong_ack_session); },
                   "acknowledgement from another session was accepted");
    guard.accept_acknowledgement(acknowledgement(3U, 2U, Fluid25DAcknowledgementState::Rejected,
                                                 Fluid25DLifecycle::Running, std::nullopt,
                                                 "rain controls unavailable at this boundary"));
}

void test_atomic_acknowledged_publication() {
    auto metadata = metadata_for(Fluid25DBackendProfile::ExternalService);
    metadata.capabilities.solver.stop = metadata.capabilities.solver.reset = true;
    Fluid25DSessionGuard guard(metadata);
    guard.accept_frame(frame("session-001", 1U, 1U, 1.0));
    guard.observe_command(command(1U, 1U, Fluid25DCommandKind::Stop));
    const auto ack = acknowledgement(1U, 1U, Fluid25DAcknowledgementState::Applied,
                                     Fluid25DLifecycle::Stopped, 2.0);
    require_throws(
        [&] {
            guard.accept_acknowledged_frame(
                ack, frame("session-001", 1U, 2U, 3.0, Fluid25DLifecycle::Stopped));
        },
        "Stop accepted fields at a different physical boundary");
    require(guard.session().lifecycle == Fluid25DLifecycle::Running &&
                guard.session().frame_sequence == 1U,
            "failed atomic Stop partially mutated guard");
    guard.accept_acknowledged_frame(ack,
                                    frame("session-001", 1U, 2U, 2.0, Fluid25DLifecycle::Stopped));
    require(guard.session().frame_sequence == 2U &&
                guard.session().lifecycle == Fluid25DLifecycle::Stopped,
            "terminal publication lost its frame sequence");
    require_throws(
        [&] { guard.accept_frame(frame("session-001", 1U, 3U, 2.0, Fluid25DLifecycle::Stopped)); },
        "terminal publication allowed later frames");
    guard.observe_command(command(2U, 1U, Fluid25DCommandKind::Reset));
    guard.accept_acknowledged_frame(acknowledgement(2U, 2U, Fluid25DAcknowledgementState::Applied,
                                                    Fluid25DLifecycle::Ready, 0.0),
                                    frame("session-001", 2U, 3U, 0.0, Fluid25DLifecycle::Ready));
    require(guard.session().reset_generation == 2U && guard.session().frame_sequence == 3U,
            "atomic reset publication did not establish new generation");
}

void test_sticky_failure_and_recovery() {
    Fluid25DBackendMetadata metadata = metadata_for(Fluid25DBackendProfile::BuiltinFiniteVolume);
    metadata.capabilities.solver.reset = true;
    metadata.capabilities.solver.resume = true;
    Fluid25DSessionGuard guard(metadata);
    guard.accept_frame(frame("session-001", 1U, 1U, 1.0));
    guard.mark_failed("solver worker exited");
    require(guard.failed() && guard.session().failure_message == "solver worker exited",
            "failure did not become sticky");
    require_throws([&] { guard.accept_frame(frame("session-001", 1U, 2U, 1.1)); },
                   "failed session accepted a frame");
    require_throws([&] { guard.observe_command(command(1U, 1U, Fluid25DCommandKind::Resume)); },
                   "failed session accepted a non-reset command");

    guard.observe_command(command(1U, 1U, Fluid25DCommandKind::Reset));
    guard.accept_acknowledgement(acknowledgement(1U, 2U, Fluid25DAcknowledgementState::Applied,
                                                 Fluid25DLifecycle::Ready, 0.0));
    require(!guard.failed() && guard.session().failure_message.empty() &&
                guard.session().reset_generation == 2U && guard.last_command_id() == 1U,
            "explicit reset did not clear sticky failure or preserve command identity");
    require_throws([&] { guard.observe_command(command(1U, 2U, Fluid25DCommandKind::Reset)); },
                   "command id was reused after reset");
    guard.accept_frame(frame("session-001", 2U, 2U, 0.0, Fluid25DLifecycle::Ready));

    guard.accept_frame(
        frame("session-001", 2U, 3U, 0.1, Fluid25DLifecycle::Failed, "producer failed"));
    require(guard.failed(), "failed frame did not latch failure");
    guard.observe_command(command(2U, 2U, Fluid25DCommandKind::Reset));

    Fluid25DBackendMetadata failed_metadata = metadata_for(Fluid25DBackendProfile::ExternalService);
    failed_metadata.session.lifecycle = Fluid25DLifecycle::Failed;
    failed_metadata.session.failure_message = "handshake reports failure";
    Fluid25DSessionGuard handshake_failed(failed_metadata);
    require(handshake_failed.failed(), "failed handshake state was not retained");
    handshake_failed.begin_generation(2U, 0.0);
    require(!handshake_failed.failed() &&
                handshake_failed.session().lifecycle == Fluid25DLifecycle::Ready,
            "explicit new generation did not clear handshake failure");
}

} // namespace

int main() {
    try {
        test_metadata_and_codec_roundtrips();
        test_strict_json_rejection();
        test_grid_identity_and_size_limits();
        test_capability_profiles();
        test_command_units_and_acknowledgement_codec();
        test_frame_guard_and_generation_changes();
        test_command_ack_guard_and_reset_invalidation();
        test_atomic_acknowledged_publication();
        test_sticky_failure_and_recovery();
    } catch (const std::exception& exception) {
        std::cerr << "fluid_25d_backend_contract_tests: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "fluid_25d_backend_contract_tests: PASS\n";
    return 0;
}
