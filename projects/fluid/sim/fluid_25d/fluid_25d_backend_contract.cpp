#include "fluid_25d_backend_contract.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {
namespace {

using Json = nlohmann::json;

[[noreturn]] void invalid(std::string message) {
    throw std::invalid_argument("fluid 2.5D backend contract: " + std::move(message));
}

[[nodiscard]] bool valid_identifier(std::string_view value) noexcept {
    if (value.empty() || value.size() > kFluid25DBackendMaximumIdentifierBytes) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') || character == '.' || character == '_' ||
               character == ':' || character == '-';
    });
}

void validate_identifier(std::string_view value, std::string_view label) {
    if (!valid_identifier(value)) {
        invalid(std::string(label) + " must be a bounded ASCII identifier");
    }
}

[[nodiscard]] bool valid_message(std::string_view value) noexcept {
    return value.size() <= kFluid25DBackendMaximumMessageBytes &&
           value.find('\0') == std::string_view::npos;
}

void validate_message(std::string_view value, std::string_view label) {
    if (!valid_message(value)) {
        invalid(std::string(label) + " is too long or contains NUL");
    }
}

[[nodiscard]] bool valid_sha256(std::string_view value) noexcept {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

[[nodiscard]] std::string_view name(Fluid25DBackendKind value) {
    switch (value) {
    case Fluid25DBackendKind::Builtin:
        return "builtin";
    case Fluid25DBackendKind::External:
        return "external";
    case Fluid25DBackendKind::Recorded:
        return "recorded";
    }
    invalid("unknown backend kind");
}

[[nodiscard]] Fluid25DBackendKind backend_kind_from_name(std::string_view value) {
    if (value == "builtin")
        return Fluid25DBackendKind::Builtin;
    if (value == "external")
        return Fluid25DBackendKind::External;
    if (value == "recorded")
        return Fluid25DBackendKind::Recorded;
    invalid("unknown backend kind");
}

[[nodiscard]] std::string_view name(Fluid25DBackendProfile value) {
    switch (value) {
    case Fluid25DBackendProfile::BuiltinVirtualPipes:
        return "builtin-virtual-pipes";
    case Fluid25DBackendProfile::BuiltinFiniteVolume:
        return "builtin-finite-volume";
    case Fluid25DBackendProfile::RecordingPlayback:
        return "recording-playback";
    case Fluid25DBackendProfile::StockExternalBridge:
        return "stock-external-bridge";
    case Fluid25DBackendProfile::ExternalService:
        return "external-service";
    }
    invalid("unknown backend profile");
}

[[nodiscard]] Fluid25DBackendProfile profile_from_name(std::string_view value) {
    if (value == "builtin-virtual-pipes")
        return Fluid25DBackendProfile::BuiltinVirtualPipes;
    if (value == "builtin-finite-volume")
        return Fluid25DBackendProfile::BuiltinFiniteVolume;
    if (value == "recording-playback")
        return Fluid25DBackendProfile::RecordingPlayback;
    if (value == "stock-external-bridge")
        return Fluid25DBackendProfile::StockExternalBridge;
    if (value == "external-service")
        return Fluid25DBackendProfile::ExternalService;
    invalid("unknown backend profile");
}

[[nodiscard]] std::string_view name(Fluid25DCapabilitySource value) {
    switch (value) {
    case Fluid25DCapabilitySource::BuiltinAdapter:
        return "builtin-adapter";
    case Fluid25DCapabilitySource::RecordingFormat:
        return "recording-format";
    case Fluid25DCapabilitySource::ViewingOnlyBridge:
        return "viewing-only-bridge";
    case Fluid25DCapabilitySource::ServiceHandshake:
        return "service-handshake";
    }
    invalid("unknown capability source");
}

[[nodiscard]] Fluid25DCapabilitySource capability_source_from_name(std::string_view value) {
    if (value == "builtin-adapter")
        return Fluid25DCapabilitySource::BuiltinAdapter;
    if (value == "recording-format")
        return Fluid25DCapabilitySource::RecordingFormat;
    if (value == "viewing-only-bridge")
        return Fluid25DCapabilitySource::ViewingOnlyBridge;
    if (value == "service-handshake")
        return Fluid25DCapabilitySource::ServiceHandshake;
    invalid("unknown capability source");
}

[[nodiscard]] std::string_view name(Fluid25DGridOrientation value) {
    switch (value) {
    case Fluid25DGridOrientation::RowMajorXFastestZRows:
        return "row-major-x-fastest-z-rows";
    }
    invalid("unknown grid orientation");
}

[[nodiscard]] Fluid25DGridOrientation orientation_from_name(std::string_view value) {
    if (value == "row-major-x-fastest-z-rows") {
        return Fluid25DGridOrientation::RowMajorXFastestZRows;
    }
    invalid("unknown grid orientation");
}

[[nodiscard]] std::string_view name(Fluid25DLifecycle value) {
    switch (value) {
    case Fluid25DLifecycle::Ready:
        return "ready";
    case Fluid25DLifecycle::Running:
        return "running";
    case Fluid25DLifecycle::Paused:
        return "paused";
    case Fluid25DLifecycle::Completed:
        return "completed";
    case Fluid25DLifecycle::Failed:
        return "failed";
    case Fluid25DLifecycle::Stopped:
        return "stopped";
    }
    invalid("unknown lifecycle");
}

[[nodiscard]] Fluid25DLifecycle lifecycle_from_name(std::string_view value) {
    if (value == "ready")
        return Fluid25DLifecycle::Ready;
    if (value == "running")
        return Fluid25DLifecycle::Running;
    if (value == "paused")
        return Fluid25DLifecycle::Paused;
    if (value == "completed")
        return Fluid25DLifecycle::Completed;
    if (value == "failed")
        return Fluid25DLifecycle::Failed;
    if (value == "stopped")
        return Fluid25DLifecycle::Stopped;
    invalid("unknown lifecycle");
}

[[nodiscard]] std::string_view name(Fluid25DControlDomain value) {
    switch (value) {
    case Fluid25DControlDomain::Solver:
        return "solver";
    case Fluid25DControlDomain::Playback:
        return "playback";
    }
    invalid("unknown control domain");
}

[[nodiscard]] Fluid25DControlDomain control_domain_from_name(std::string_view value) {
    if (value == "solver")
        return Fluid25DControlDomain::Solver;
    if (value == "playback")
        return Fluid25DControlDomain::Playback;
    invalid("unknown control domain");
}

[[nodiscard]] std::string_view name(Fluid25DCommandKind value) {
    switch (value) {
    case Fluid25DCommandKind::Pause:
        return "pause";
    case Fluid25DCommandKind::Resume:
        return "resume";
    case Fluid25DCommandKind::Reset:
        return "reset";
    case Fluid25DCommandKind::Stop:
        return "stop";
    case Fluid25DCommandKind::Step:
        return "step";
    case Fluid25DCommandKind::Seek:
        return "seek";
    case Fluid25DCommandKind::SetRain:
        return "set-rain";
    case Fluid25DCommandKind::SetTimeScale:
        return "set-time-scale";
    }
    invalid("unknown command kind");
}

[[nodiscard]] Fluid25DCommandKind command_kind_from_name(std::string_view value) {
    if (value == "pause")
        return Fluid25DCommandKind::Pause;
    if (value == "resume")
        return Fluid25DCommandKind::Resume;
    if (value == "reset")
        return Fluid25DCommandKind::Reset;
    if (value == "stop")
        return Fluid25DCommandKind::Stop;
    if (value == "step")
        return Fluid25DCommandKind::Step;
    if (value == "seek")
        return Fluid25DCommandKind::Seek;
    if (value == "set-rain")
        return Fluid25DCommandKind::SetRain;
    if (value == "set-time-scale")
        return Fluid25DCommandKind::SetTimeScale;
    invalid("unknown command kind");
}

[[nodiscard]] std::string_view name(Fluid25DAcknowledgementState value) {
    switch (value) {
    case Fluid25DAcknowledgementState::Accepted:
        return "accepted";
    case Fluid25DAcknowledgementState::Applied:
        return "applied";
    case Fluid25DAcknowledgementState::Rejected:
        return "rejected";
    }
    invalid("unknown acknowledgement state");
}

[[nodiscard]] Fluid25DAcknowledgementState acknowledgement_state_from_name(std::string_view value) {
    if (value == "accepted")
        return Fluid25DAcknowledgementState::Accepted;
    if (value == "applied")
        return Fluid25DAcknowledgementState::Applied;
    if (value == "rejected")
        return Fluid25DAcknowledgementState::Rejected;
    invalid("unknown acknowledgement state");
}

void validate_grid(const Fluid25DGridContract& grid) {
    if (grid.width < 2U || grid.height < 2U || grid.width > kFluid25DBackendMaximumDimension ||
        grid.height > kFluid25DBackendMaximumDimension) {
        invalid("grid dimensions must be between 2 and 16384");
    }
    const std::uint64_t cell_count =
        static_cast<std::uint64_t>(grid.width) * static_cast<std::uint64_t>(grid.height);
    if (cell_count > kFluid25DBackendMaximumCells) {
        invalid("grid exceeds the 4194304-cell limit");
    }
    if (!std::isfinite(grid.spacing_m) || grid.spacing_m <= 0.0) {
        invalid("grid spacing must be finite and positive metres");
    }
    if (grid.orientation != Fluid25DGridOrientation::RowMajorXFastestZRows) {
        invalid("grid orientation is not declared");
    }
    if (!valid_sha256(grid.input_sha256) || !valid_sha256(grid.solver_bed_sha256)) {
        invalid("input and solver-bed identities must be lowercase SHA-256 values");
    }
}

[[nodiscard]] bool any_capability(const Fluid25DControlCapabilities& value) noexcept {
    return value.pause || value.resume || value.reset || value.stop || value.step || value.seek ||
           value.set_rain || value.set_time_scale;
}

void validate_profile_contract(const Fluid25DBackendMetadata& metadata) {
    switch (metadata.profile) {
    case Fluid25DBackendProfile::BuiltinVirtualPipes:
    case Fluid25DBackendProfile::BuiltinFiniteVolume:
        if (metadata.kind != Fluid25DBackendKind::Builtin ||
            metadata.capability_source != Fluid25DCapabilitySource::BuiltinAdapter ||
            any_capability(metadata.capabilities.playback)) {
            invalid("built-in profiles require adapter-declared solver controls only");
        }
        return;
    case Fluid25DBackendProfile::RecordingPlayback:
        if (metadata.kind != Fluid25DBackendKind::Recorded ||
            metadata.capability_source != Fluid25DCapabilitySource::RecordingFormat ||
            any_capability(metadata.capabilities.solver)) {
            invalid("recording playback cannot advertise solver controls");
        }
        return;
    case Fluid25DBackendProfile::StockExternalBridge:
        if (metadata.kind != Fluid25DBackendKind::External ||
            metadata.capability_source != Fluid25DCapabilitySource::ViewingOnlyBridge ||
            any_capability(metadata.capabilities.solver)) {
            invalid("the stock external bridge may expose viewer controls only");
        }
        return;
    case Fluid25DBackendProfile::ExternalService:
        if (metadata.kind != Fluid25DBackendKind::External ||
            metadata.capability_source != Fluid25DCapabilitySource::ServiceHandshake) {
            invalid("external-service capabilities must be declared by its handshake");
        }
        return;
    }
    invalid("unknown backend profile");
}

void validate_control_capability_domains(const Fluid25DBackendCapabilities& capabilities) {
    if (capabilities.solver.seek || capabilities.playback.set_rain) {
        invalid("Seek is playback-only and SetRain is solver-only");
    }
}

void validate_session_metadata(const Fluid25DSessionMetadata& session) {
    validate_identifier(session.session_id, "session_id");
    if (session.reset_generation == 0U) {
        invalid("reset_generation must be nonzero");
    }
    if (!std::isfinite(session.physical_time_s) || session.physical_time_s < 0.0) {
        invalid("physical_time_s must be finite and nonnegative seconds");
    }
    validate_message(session.failure_message, "failure_message");
    if ((session.lifecycle == Fluid25DLifecycle::Failed) != !session.failure_message.empty()) {
        invalid("failed lifecycle requires a message and other lifecycles cannot carry one");
    }
    (void)name(session.lifecycle);
}

void validate_lifecycle_value(Fluid25DLifecycle lifecycle, std::string_view message) {
    (void)name(lifecycle);
    validate_message(message, "failure_message");
    if ((lifecycle == Fluid25DLifecycle::Failed) != !message.empty()) {
        invalid("failed lifecycle requires a message and other lifecycles cannot carry one");
    }
}

[[nodiscard]] bool lifecycle_can_advance(Fluid25DLifecycle from, Fluid25DLifecycle to) noexcept {
    if (from == to) {
        return from == Fluid25DLifecycle::Ready || from == Fluid25DLifecycle::Running ||
               from == Fluid25DLifecycle::Paused;
    }
    switch (from) {
    case Fluid25DLifecycle::Ready:
        return to == Fluid25DLifecycle::Running || to == Fluid25DLifecycle::Paused ||
               to == Fluid25DLifecycle::Completed || to == Fluid25DLifecycle::Failed ||
               to == Fluid25DLifecycle::Stopped;
    case Fluid25DLifecycle::Running:
        return to == Fluid25DLifecycle::Paused || to == Fluid25DLifecycle::Completed ||
               to == Fluid25DLifecycle::Failed || to == Fluid25DLifecycle::Stopped;
    case Fluid25DLifecycle::Paused:
        return to == Fluid25DLifecycle::Running || to == Fluid25DLifecycle::Completed ||
               to == Fluid25DLifecycle::Failed || to == Fluid25DLifecycle::Stopped;
    case Fluid25DLifecycle::Completed:
    case Fluid25DLifecycle::Failed:
    case Fluid25DLifecycle::Stopped:
        return false;
    }
    return false;
}

[[nodiscard]] const Json& required_member(const Json& object, std::string_view key,
                                          std::string_view context) {
    if (!object.is_object() || !object.contains(std::string(key))) {
        invalid(std::string(context) + " is missing " + std::string(key));
    }
    return object.at(std::string(key));
}

void require_exact_keys(const Json& object, std::initializer_list<std::string_view> expected,
                        std::string_view context) {
    if (!object.is_object() || object.size() != expected.size()) {
        invalid(std::string(context) + " has missing or unexpected fields");
    }
    for (const auto& [key, value] : object.items()) {
        (void)value;
        if (std::find(expected.begin(), expected.end(), key) == expected.end()) {
            invalid(std::string(context) + " contains unknown field " + key);
        }
    }
    for (const std::string_view key : expected) {
        if (!object.contains(std::string(key))) {
            invalid(std::string(context) + " is missing " + std::string(key));
        }
    }
}

[[nodiscard]] std::string required_string(const Json& value, std::string_view label) {
    if (!value.is_string()) {
        invalid(std::string(label) + " must be a string");
    }
    return value.get<std::string>();
}

[[nodiscard]] std::uint64_t required_u64(const Json& value, std::string_view label) {
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const std::int64_t parsed = value.get<std::int64_t>();
        if (parsed >= 0) {
            return static_cast<std::uint64_t>(parsed);
        }
    }
    invalid(std::string(label) + " must be a nonnegative integer");
}

[[nodiscard]] std::uint32_t required_u32(const Json& value, std::string_view label) {
    const std::uint64_t parsed = required_u64(value, label);
    if (parsed > std::numeric_limits<std::uint32_t>::max()) {
        invalid(std::string(label) + " is outside uint32 range");
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] double required_finite_number(const Json& value, std::string_view label) {
    if (!value.is_number()) {
        invalid(std::string(label) + " must be numeric");
    }
    const double parsed = value.get<double>();
    if (!std::isfinite(parsed)) {
        invalid(std::string(label) + " must be finite");
    }
    return parsed;
}

[[nodiscard]] bool required_bool(const Json& value, std::string_view label) {
    if (!value.is_boolean()) {
        invalid(std::string(label) + " must be boolean");
    }
    return value.get<bool>();
}

[[nodiscard]] Json parse_document(std::string_view document, std::string_view expected_type) {
    if (document.empty() || document.size() > kFluid25DBackendMaximumJsonBytes) {
        invalid("JSON document is empty or exceeds the 64 KiB limit");
    }
    std::vector<std::set<std::string>> object_keys{};
    Json parsed;
    try {
        parsed = Json::parse(
            document.begin(), document.end(),
            [&object_keys](int depth, Json::parse_event_t event, Json& token) {
                if (depth >= static_cast<int>(kFluid25DBackendMaximumJsonDepth)) {
                    invalid("JSON nesting exceeds the supported depth");
                }
                if (event == Json::parse_event_t::object_start) {
                    object_keys.emplace_back();
                } else if (event == Json::parse_event_t::key) {
                    if (object_keys.empty() ||
                        !object_keys.back().insert(token.get<std::string>()).second) {
                        invalid("JSON object contains a duplicate key");
                    }
                } else if (event == Json::parse_event_t::object_end) {
                    if (!object_keys.empty()) {
                        object_keys.pop_back();
                    }
                }
                return true;
            },
            true, false);
    } catch (const std::invalid_argument&) {
        throw;
    } catch (const std::exception& exception) {
        invalid(std::string("malformed JSON: ") + exception.what());
    }
    if (!parsed.is_object()) {
        invalid("JSON document root must be an object");
    }
    if (required_string(required_member(parsed, "schema", "document"), "schema") !=
        kFluid25DBackendSessionSchema) {
        invalid("unsupported schema version");
    }
    if (required_string(required_member(parsed, "type", "document"), "type") != expected_type) {
        invalid("unexpected document type");
    }
    return parsed;
}

void require_document_header(const Json& document, std::string_view type,
                             std::initializer_list<std::string_view> keys) {
    require_exact_keys(document, keys, "document");
    if (required_string(required_member(document, "schema", "document"), "schema") !=
        kFluid25DBackendSessionSchema) {
        invalid("unsupported schema version");
    }
    if (required_string(required_member(document, "type", "document"), "type") != type) {
        invalid("unexpected document type");
    }
}

[[nodiscard]] Json control_capabilities_to_json(const Fluid25DControlCapabilities& caps) {
    return Json{{"pause", caps.pause},       {"resume", caps.resume},
                {"reset", caps.reset},       {"stop", caps.stop},
                {"step", caps.step},         {"seek", caps.seek},
                {"set_rain", caps.set_rain}, {"set_time_scale", caps.set_time_scale}};
}

[[nodiscard]] Fluid25DControlCapabilities control_capabilities_from_json(const Json& object,
                                                                         std::string_view context) {
    require_exact_keys(
        object, {"pause", "resume", "reset", "stop", "step", "seek", "set_rain", "set_time_scale"},
        context);
    return {.pause = required_bool(object.at("pause"), "pause"),
            .resume = required_bool(object.at("resume"), "resume"),
            .reset = required_bool(object.at("reset"), "reset"),
            .stop = required_bool(object.at("stop"), "stop"),
            .step = required_bool(object.at("step"), "step"),
            .seek = required_bool(object.at("seek"), "seek"),
            .set_rain = required_bool(object.at("set_rain"), "set_rain"),
            .set_time_scale = required_bool(object.at("set_time_scale"), "set_time_scale")};
}

[[nodiscard]] Json capabilities_to_json(const Fluid25DBackendCapabilities& capabilities) {
    return Json{{"solver", control_capabilities_to_json(capabilities.solver)},
                {"playback", control_capabilities_to_json(capabilities.playback)}};
}

[[nodiscard]] Fluid25DBackendCapabilities capabilities_from_json(const Json& object) {
    require_exact_keys(object, {"solver", "playback"}, "capabilities");
    return {.solver = control_capabilities_from_json(object.at("solver"), "solver capabilities"),
            .playback =
                control_capabilities_from_json(object.at("playback"), "playback capabilities")};
}

[[nodiscard]] std::string encode_document(Json document) {
    const std::string encoded = document.dump();
    if (encoded.empty() || encoded.size() > kFluid25DBackendMaximumJsonBytes) {
        invalid("encoded JSON document exceeds the 64 KiB limit");
    }
    return encoded;
}

[[nodiscard]] Json metadata_to_json(const Fluid25DBackendMetadata& metadata) {
    return Json{{"schema", kFluid25DBackendSessionSchema},
                {"type", "metadata"},
                {"backend",
                 {{"kind", name(metadata.kind)},
                  {"profile", name(metadata.profile)},
                  {"capability_source", name(metadata.capability_source)},
                  {"id", metadata.id}}},
                {"grid",
                 {{"width", metadata.grid.width},
                  {"height", metadata.grid.height},
                  {"spacing_m", metadata.grid.spacing_m},
                  {"orientation", name(metadata.grid.orientation)},
                  {"input_sha256", metadata.grid.input_sha256},
                  {"solver_bed_sha256", metadata.grid.solver_bed_sha256}}},
                {"fields",
                 {{"depth_m", metadata.fields.depth_m},
                  {"horizontal_velocity_x_m_per_s", metadata.fields.horizontal_velocity_x_m_per_s},
                  {"horizontal_velocity_z_m_per_s", metadata.fields.horizontal_velocity_z_m_per_s},
                  {"momentum_x_m2_per_s", metadata.fields.momentum_x_m2_per_s},
                  {"momentum_z_m2_per_s", metadata.fields.momentum_z_m2_per_s},
                  {"tracer_depth_equivalent_m", metadata.fields.tracer_depth_equivalent_m},
                  {"face_discharge_m3_per_s", metadata.fields.face_discharge_m3_per_s},
                  {"water_ledger", metadata.fields.water_ledger},
                  {"tracer_ledger", metadata.fields.tracer_ledger}}},
                {"capabilities", capabilities_to_json(metadata.capabilities)},
                {"session",
                 {{"session_id", metadata.session.session_id},
                  {"reset_generation", metadata.session.reset_generation},
                  {"frame_sequence", metadata.session.frame_sequence},
                  {"physical_time_s", metadata.session.physical_time_s},
                  {"lifecycle", name(metadata.session.lifecycle)},
                  {"failure_message", metadata.session.failure_message}}}};
}

[[nodiscard]] Fluid25DBackendMetadata metadata_from_json(const Json& document) {
    require_document_header(
        document, "metadata",
        {"schema", "type", "backend", "grid", "fields", "capabilities", "session"});
    const Json& backend = document.at("backend");
    require_exact_keys(backend, {"kind", "profile", "capability_source", "id"}, "backend");
    const Json& grid = document.at("grid");
    require_exact_keys(
        grid, {"width", "height", "spacing_m", "orientation", "input_sha256", "solver_bed_sha256"},
        "grid");
    const Json& fields = document.at("fields");
    require_exact_keys(fields,
                       {"depth_m", "horizontal_velocity_x_m_per_s", "horizontal_velocity_z_m_per_s",
                        "momentum_x_m2_per_s", "momentum_z_m2_per_s", "tracer_depth_equivalent_m",
                        "face_discharge_m3_per_s", "water_ledger", "tracer_ledger"},
                       "fields");
    const Json& session = document.at("session");
    require_exact_keys(session,
                       {"session_id", "reset_generation", "frame_sequence", "physical_time_s",
                        "lifecycle", "failure_message"},
                       "session");

    Fluid25DBackendMetadata result{};
    result.kind = backend_kind_from_name(required_string(backend.at("kind"), "backend kind"));
    result.profile = profile_from_name(required_string(backend.at("profile"), "backend profile"));
    result.capability_source = capability_source_from_name(
        required_string(backend.at("capability_source"), "capability_source"));
    result.id = required_string(backend.at("id"), "backend id");
    result.grid.width = required_u32(grid.at("width"), "grid width");
    result.grid.height = required_u32(grid.at("height"), "grid height");
    result.grid.spacing_m = required_finite_number(grid.at("spacing_m"), "grid spacing_m");
    result.grid.orientation =
        orientation_from_name(required_string(grid.at("orientation"), "grid orientation"));
    result.grid.input_sha256 = required_string(grid.at("input_sha256"), "input_sha256");
    result.grid.solver_bed_sha256 =
        required_string(grid.at("solver_bed_sha256"), "solver_bed_sha256");
    result.fields = {
        .depth_m = required_bool(fields.at("depth_m"), "depth_m"),
        .horizontal_velocity_x_m_per_s = required_bool(fields.at("horizontal_velocity_x_m_per_s"),
                                                       "horizontal_velocity_x_m_per_s"),
        .horizontal_velocity_z_m_per_s = required_bool(fields.at("horizontal_velocity_z_m_per_s"),
                                                       "horizontal_velocity_z_m_per_s"),
        .momentum_x_m2_per_s =
            required_bool(fields.at("momentum_x_m2_per_s"), "momentum_x_m2_per_s"),
        .momentum_z_m2_per_s =
            required_bool(fields.at("momentum_z_m2_per_s"), "momentum_z_m2_per_s"),
        .tracer_depth_equivalent_m =
            required_bool(fields.at("tracer_depth_equivalent_m"), "tracer_depth_equivalent_m"),
        .face_discharge_m3_per_s =
            required_bool(fields.at("face_discharge_m3_per_s"), "face_discharge_m3_per_s"),
        .water_ledger = required_bool(fields.at("water_ledger"), "water_ledger"),
        .tracer_ledger = required_bool(fields.at("tracer_ledger"), "tracer_ledger")};
    result.capabilities = capabilities_from_json(document.at("capabilities"));
    result.session.session_id = required_string(session.at("session_id"), "session_id");
    result.session.reset_generation = required_u64(session.at("reset_generation"), "generation");
    result.session.frame_sequence = required_u64(session.at("frame_sequence"), "frame_sequence");
    result.session.physical_time_s =
        required_finite_number(session.at("physical_time_s"), "physical_time_s");
    result.session.lifecycle =
        lifecycle_from_name(required_string(session.at("lifecycle"), "lifecycle"));
    result.session.failure_message =
        required_string(session.at("failure_message"), "failure_message");
    validate_fluid_25d_backend_metadata(result);
    return result;
}

[[nodiscard]] Json frame_to_json(const Fluid25DFrameHeader& frame) {
    return Json{{"schema", kFluid25DBackendSessionSchema},
                {"type", "frame"},
                {"session_id", frame.session_id},
                {"reset_generation", frame.reset_generation},
                {"sequence", frame.sequence},
                {"physical_time_s", frame.physical_time_s},
                {"lifecycle", name(frame.lifecycle)},
                {"failure_message", frame.failure_message}};
}

[[nodiscard]] Fluid25DFrameHeader frame_from_json(const Json& document) {
    require_document_header(document, "frame",
                            {"schema", "type", "session_id", "reset_generation", "sequence",
                             "physical_time_s", "lifecycle", "failure_message"});
    Fluid25DFrameHeader result{
        .session_id = required_string(document.at("session_id"), "session_id"),
        .reset_generation = required_u64(document.at("reset_generation"), "generation"),
        .sequence = required_u64(document.at("sequence"), "sequence"),
        .physical_time_s =
            required_finite_number(document.at("physical_time_s"), "physical_time_s"),
        .lifecycle = lifecycle_from_name(required_string(document.at("lifecycle"), "lifecycle")),
        .failure_message = required_string(document.at("failure_message"), "failure_message")};
    validate_fluid_25d_frame_header(result);
    return result;
}

[[nodiscard]] Json command_to_json(const Fluid25DControlCommand& command) {
    return Json{{"schema", kFluid25DBackendSessionSchema},
                {"type", "command"},
                {"command_id", command.command_id},
                {"session_id", command.session_id},
                {"reset_generation", command.reset_generation},
                {"domain", name(command.domain)},
                {"kind", name(command.kind)},
                {"value", command.value ? Json(*command.value) : Json(nullptr)}};
}

[[nodiscard]] Fluid25DControlCommand command_from_json(const Json& document) {
    require_document_header(document, "command",
                            {"schema", "type", "command_id", "session_id", "reset_generation",
                             "domain", "kind", "value"});
    Fluid25DControlCommand result{};
    result.command_id = required_u64(document.at("command_id"), "command_id");
    result.session_id = required_string(document.at("session_id"), "session_id");
    result.reset_generation = required_u64(document.at("reset_generation"), "generation");
    result.domain = control_domain_from_name(required_string(document.at("domain"), "domain"));
    result.kind = command_kind_from_name(required_string(document.at("kind"), "kind"));
    if (!document.at("value").is_null()) {
        result.value = required_finite_number(document.at("value"), "value");
    }
    validate_fluid_25d_control_command(result);
    return result;
}

[[nodiscard]] Json acknowledgement_to_json(const Fluid25DCommandAcknowledgement& acknowledgement) {
    return Json{{"schema", kFluid25DBackendSessionSchema},
                {"type", "ack"},
                {"command_id", acknowledgement.command_id},
                {"session_id", acknowledgement.session_id},
                {"reset_generation", acknowledgement.reset_generation},
                {"state", name(acknowledgement.state)},
                {"application_time_s", acknowledgement.application_time_s
                                           ? Json(*acknowledgement.application_time_s)
                                           : Json(nullptr)},
                {"lifecycle", name(acknowledgement.lifecycle)},
                {"message", acknowledgement.message}};
}

[[nodiscard]] Fluid25DCommandAcknowledgement acknowledgement_from_json(const Json& document) {
    require_document_header(document, "ack",
                            {"schema", "type", "command_id", "session_id", "reset_generation",
                             "state", "application_time_s", "lifecycle", "message"});
    Fluid25DCommandAcknowledgement result{};
    result.command_id = required_u64(document.at("command_id"), "command_id");
    result.session_id = required_string(document.at("session_id"), "session_id");
    result.reset_generation = required_u64(document.at("reset_generation"), "generation");
    result.state = acknowledgement_state_from_name(required_string(document.at("state"), "state"));
    if (!document.at("application_time_s").is_null()) {
        result.application_time_s =
            required_finite_number(document.at("application_time_s"), "application_time_s");
    }
    result.lifecycle = lifecycle_from_name(required_string(document.at("lifecycle"), "lifecycle"));
    result.message = required_string(document.at("message"), "message");
    validate_fluid_25d_command_acknowledgement(result);
    return result;
}

[[nodiscard]] bool generation_barrier(const Fluid25DControlCommand& command) noexcept {
    return command.kind == Fluid25DCommandKind::Reset ||
           (command.domain == Fluid25DControlDomain::Playback &&
            command.kind == Fluid25DCommandKind::Seek);
}

[[nodiscard]] Fluid25DLifecycle expected_lifecycle_after(const Fluid25DControlCommand& command,
                                                         Fluid25DLifecycle current,
                                                         Fluid25DLifecycle acknowledged) {
    switch (command.kind) {
    case Fluid25DCommandKind::Pause:
        if (acknowledged != Fluid25DLifecycle::Paused)
            invalid("Applied Pause acknowledgement must report paused lifecycle");
        break;
    case Fluid25DCommandKind::Resume:
        if (acknowledged != Fluid25DLifecycle::Running)
            invalid("Applied Resume acknowledgement must report running lifecycle");
        break;
    case Fluid25DCommandKind::Reset:
        if (acknowledged != Fluid25DLifecycle::Ready)
            invalid("Applied Reset acknowledgement must report ready lifecycle");
        break;
    case Fluid25DCommandKind::Stop:
        if (acknowledged != Fluid25DLifecycle::Stopped)
            invalid("Applied Stop acknowledgement must report stopped lifecycle");
        break;
    case Fluid25DCommandKind::Step:
    case Fluid25DCommandKind::Seek:
    case Fluid25DCommandKind::SetRain:
    case Fluid25DCommandKind::SetTimeScale:
        break;
    }
    if (acknowledged == Fluid25DLifecycle::Failed && acknowledged == current) {
        return acknowledged;
    }
    if (command.kind == Fluid25DCommandKind::Reset ||
        (command.domain == Fluid25DControlDomain::Playback &&
         command.kind == Fluid25DCommandKind::Seek && current == Fluid25DLifecycle::Completed)) {
        return acknowledged;
    }
    if (!lifecycle_can_advance(current, acknowledged)) {
        invalid("acknowledgement lifecycle transition is not allowed");
    }
    return acknowledged;
}

} // namespace

void validate_fluid_25d_backend_metadata(const Fluid25DBackendMetadata& metadata) {
    validate_identifier(metadata.id, "backend id");
    (void)name(metadata.kind);
    (void)name(metadata.profile);
    (void)name(metadata.capability_source);
    validate_profile_contract(metadata);
    validate_control_capability_domains(metadata.capabilities);
    validate_grid(metadata.grid);
    if (!metadata.fields.depth_m || !metadata.fields.horizontal_velocity_x_m_per_s ||
        !metadata.fields.horizontal_velocity_z_m_per_s) {
        invalid("depth and both horizontal velocity components are required logical fields");
    }
    validate_session_metadata(metadata.session);
}

void validate_fluid_25d_frame_header(const Fluid25DFrameHeader& frame) {
    validate_identifier(frame.session_id, "session_id");
    if (frame.reset_generation == 0U || frame.sequence == 0U) {
        invalid("frame generation and sequence must be nonzero");
    }
    if (!std::isfinite(frame.physical_time_s) || frame.physical_time_s < 0.0) {
        invalid("frame physical_time_s must be finite and nonnegative seconds");
    }
    validate_lifecycle_value(frame.lifecycle, frame.failure_message);
}

void validate_fluid_25d_control_command(const Fluid25DControlCommand& command) {
    validate_identifier(command.session_id, "session_id");
    if (command.command_id == 0U || command.reset_generation == 0U) {
        invalid("command_id and reset_generation must be nonzero");
    }
    (void)name(command.domain);
    (void)name(command.kind);
    const bool requires_value = command.kind == Fluid25DCommandKind::SetRain ||
                                command.kind == Fluid25DCommandKind::SetTimeScale ||
                                command.kind == Fluid25DCommandKind::Seek;
    if (requires_value != command.value.has_value()) {
        invalid("command value is required only for rain, time scale, and seek");
    }
    if (!command.value.has_value()) {
        return;
    }
    const double value = *command.value;
    if (!std::isfinite(value)) {
        invalid("command value must be finite");
    }
    switch (command.kind) {
    case Fluid25DCommandKind::SetRain:
        if (command.domain != Fluid25DControlDomain::Solver || value < 0.0) {
            invalid("SetRain requires solver domain and finite nonnegative metres/second");
        }
        break;
    case Fluid25DCommandKind::SetTimeScale:
        if (value <= 0.0) {
            invalid("SetTimeScale requires a finite positive multiplier");
        }
        break;
    case Fluid25DCommandKind::Seek:
        if (command.domain != Fluid25DControlDomain::Playback || value < 0.0) {
            invalid("Seek requires playback domain and finite nonnegative physical seconds");
        }
        break;
    case Fluid25DCommandKind::Pause:
    case Fluid25DCommandKind::Resume:
    case Fluid25DCommandKind::Reset:
    case Fluid25DCommandKind::Stop:
    case Fluid25DCommandKind::Step:
        invalid("command unexpectedly carries a value");
    }
}

void validate_fluid_25d_command_acknowledgement(
    const Fluid25DCommandAcknowledgement& acknowledgement) {
    validate_identifier(acknowledgement.session_id, "session_id");
    if (acknowledgement.command_id == 0U || acknowledgement.reset_generation == 0U) {
        invalid("acknowledgement command_id and generation must be nonzero");
    }
    (void)name(acknowledgement.state);
    validate_message(acknowledgement.message, "acknowledgement message");
    validate_lifecycle_value(acknowledgement.lifecycle,
                             acknowledgement.lifecycle == Fluid25DLifecycle::Failed
                                 ? acknowledgement.message
                                 : std::string_view{});
    if (acknowledgement.state == Fluid25DAcknowledgementState::Applied) {
        if (!acknowledgement.application_time_s.has_value() ||
            !std::isfinite(*acknowledgement.application_time_s) ||
            *acknowledgement.application_time_s < 0.0) {
            invalid("Applied acknowledgement requires finite nonnegative application_time_s");
        }
    } else if (acknowledgement.application_time_s.has_value()) {
        invalid("only Applied acknowledgement may report application_time_s");
    }
    if (acknowledgement.state == Fluid25DAcknowledgementState::Rejected &&
        acknowledgement.message.empty()) {
        invalid("Rejected acknowledgement requires a reason");
    }
}

bool fluid_25d_command_supported(const Fluid25DBackendCapabilities& capabilities,
                                 Fluid25DControlDomain domain, Fluid25DCommandKind kind) noexcept {
    const Fluid25DControlCapabilities* selected = nullptr;
    if (domain == Fluid25DControlDomain::Solver) {
        selected = &capabilities.solver;
    } else if (domain == Fluid25DControlDomain::Playback) {
        selected = &capabilities.playback;
    } else {
        return false;
    }
    if ((kind == Fluid25DCommandKind::Seek && domain != Fluid25DControlDomain::Playback) ||
        (kind == Fluid25DCommandKind::SetRain && domain != Fluid25DControlDomain::Solver)) {
        return false;
    }
    switch (kind) {
    case Fluid25DCommandKind::Pause:
        return selected->pause;
    case Fluid25DCommandKind::Resume:
        return selected->resume;
    case Fluid25DCommandKind::Reset:
        return selected->reset;
    case Fluid25DCommandKind::Stop:
        return selected->stop;
    case Fluid25DCommandKind::Step:
        return selected->step;
    case Fluid25DCommandKind::Seek:
        return selected->seek;
    case Fluid25DCommandKind::SetRain:
        return selected->set_rain;
    case Fluid25DCommandKind::SetTimeScale:
        return selected->set_time_scale;
    }
    return false;
}

Fluid25DBackendCapabilities fluid_25d_recording_playback_capabilities() noexcept {
    Fluid25DBackendCapabilities result{};
    result.playback.pause = true;
    result.playback.resume = true;
    result.playback.reset = true;
    result.playback.step = true;
    result.playback.seek = true;
    result.playback.set_time_scale = true;
    return result;
}

Fluid25DBackendCapabilities fluid_25d_stock_external_bridge_capabilities() noexcept {
    return fluid_25d_recording_playback_capabilities();
}

std::string encode_fluid_25d_backend_metadata_json(const Fluid25DBackendMetadata& metadata) {
    validate_fluid_25d_backend_metadata(metadata);
    return encode_document(metadata_to_json(metadata));
}

Fluid25DBackendMetadata decode_fluid_25d_backend_metadata_json(std::string_view document) {
    return metadata_from_json(parse_document(document, "metadata"));
}

std::string encode_fluid_25d_frame_header_json(const Fluid25DFrameHeader& frame) {
    validate_fluid_25d_frame_header(frame);
    return encode_document(frame_to_json(frame));
}

Fluid25DFrameHeader decode_fluid_25d_frame_header_json(std::string_view document) {
    return frame_from_json(parse_document(document, "frame"));
}

std::string encode_fluid_25d_control_command_json(const Fluid25DControlCommand& command) {
    validate_fluid_25d_control_command(command);
    return encode_document(command_to_json(command));
}

Fluid25DControlCommand decode_fluid_25d_control_command_json(std::string_view document) {
    return command_from_json(parse_document(document, "command"));
}

std::string encode_fluid_25d_command_acknowledgement_json(
    const Fluid25DCommandAcknowledgement& acknowledgement) {
    validate_fluid_25d_command_acknowledgement(acknowledgement);
    return encode_document(acknowledgement_to_json(acknowledgement));
}

Fluid25DCommandAcknowledgement
decode_fluid_25d_command_acknowledgement_json(std::string_view document) {
    return acknowledgement_from_json(parse_document(document, "ack"));
}

Fluid25DSessionGuard::Fluid25DSessionGuard(Fluid25DBackendMetadata metadata)
    : metadata_(std::move(metadata)) {
    validate_fluid_25d_backend_metadata(metadata_);
}

void Fluid25DSessionGuard::accept_frame(const Fluid25DFrameHeader& frame) {
    validate_fluid_25d_frame_header(frame);
    const Fluid25DSessionMetadata& current = metadata_.session;
    if (frame.session_id != current.session_id) {
        invalid("frame belongs to a stale or different session");
    }
    if (frame.reset_generation != current.reset_generation) {
        invalid("frame belongs to a stale or unannounced generation");
    }
    if (current.lifecycle == Fluid25DLifecycle::Failed ||
        current.lifecycle == Fluid25DLifecycle::Completed ||
        current.lifecycle == Fluid25DLifecycle::Stopped) {
        invalid("terminal lifecycle rejects further frames until a new generation");
    }
    if (frame.sequence <= current.frame_sequence) {
        invalid("frame sequence must increase monotonically");
    }
    if (frame.physical_time_s < current.physical_time_s) {
        invalid("physical time cannot move backward within a generation");
    }
    if (!lifecycle_can_advance(current.lifecycle, frame.lifecycle)) {
        invalid("frame lifecycle transition is not allowed");
    }
    metadata_.session.frame_sequence = frame.sequence;
    metadata_.session.physical_time_s = frame.physical_time_s;
    set_session_lifecycle(frame.lifecycle);
    metadata_.session.failure_message = frame.failure_message;
    if (frame.lifecycle == Fluid25DLifecycle::Failed) {
        outstanding_.clear();
    }
}

void Fluid25DSessionGuard::observe_command(const Fluid25DControlCommand& command) {
    validate_fluid_25d_control_command(command);
    const Fluid25DSessionMetadata& current = metadata_.session;
    if (command.session_id != current.session_id) {
        invalid("command belongs to a different session");
    }
    if (command.reset_generation != current.reset_generation) {
        invalid("command belongs to a stale generation");
    }
    if (command.command_id <= last_command_id_) {
        invalid("command_id must increase monotonically and never be zero");
    }
    if (!fluid_25d_command_supported(metadata_.capabilities, command.domain, command.kind)) {
        invalid("command is unsupported by the declared backend capabilities");
    }
    if (current.lifecycle == Fluid25DLifecycle::Failed &&
        command.kind != Fluid25DCommandKind::Reset) {
        invalid("failed lifecycle is sticky until explicit reset or generation change");
    }
    if ((current.lifecycle == Fluid25DLifecycle::Completed ||
         current.lifecycle == Fluid25DLifecycle::Stopped) &&
        command.kind != Fluid25DCommandKind::Reset) {
        invalid("completed or stopped lifecycle accepts only reset");
    }
    if (outstanding_.size() >= kFluid25DBackendMaximumOutstandingCommands) {
        invalid("outstanding command limit has been reached for this session");
    }
    outstanding_.emplace(command.command_id, OutstandingCommand{command, false});
    last_command_id_ = command.command_id;
}

void Fluid25DSessionGuard::accept_acknowledgement(
    const Fluid25DCommandAcknowledgement& acknowledgement) {
    validate_fluid_25d_command_acknowledgement(acknowledgement);
    if (acknowledgement.session_id != metadata_.session.session_id) {
        invalid("acknowledgement belongs to a stale or different session");
    }
    const auto found = outstanding_.find(acknowledgement.command_id);
    if (found == outstanding_.end()) {
        invalid("acknowledgement is stale, duplicated, or has no matching command");
    }
    OutstandingCommand& outstanding = found->second;
    const Fluid25DControlCommand& command = outstanding.command;
    const Fluid25DSessionMetadata& current = metadata_.session;
    if (acknowledgement.state == Fluid25DAcknowledgementState::Accepted) {
        if (outstanding.accepted || acknowledgement.reset_generation != current.reset_generation ||
            acknowledgement.lifecycle != current.lifecycle) {
            invalid("Accepted acknowledgement is duplicate or has stale session state");
        }
        outstanding.accepted = true;
        return;
    }

    if (acknowledgement.state == Fluid25DAcknowledgementState::Rejected) {
        if (acknowledgement.reset_generation != current.reset_generation ||
            acknowledgement.lifecycle != current.lifecycle) {
            invalid("Rejected acknowledgement cannot change generation or lifecycle");
        }
        outstanding_.erase(found);
        return;
    }

    if (!acknowledgement.application_time_s.has_value()) {
        invalid("Applied acknowledgement has no physical application time");
    }
    const double application_time_s = *acknowledgement.application_time_s;
    const bool changes_generation = generation_barrier(command);
    if (changes_generation) {
        if (current.reset_generation == std::numeric_limits<std::uint64_t>::max() ||
            acknowledgement.reset_generation != current.reset_generation + 1U) {
            invalid("Applied reset/seek must advance generation by exactly one");
        }
        if (command.kind == Fluid25DCommandKind::Reset &&
            acknowledgement.lifecycle != Fluid25DLifecycle::Ready) {
            invalid("Applied Reset must begin the new generation in ready lifecycle");
        }
        if (command.kind == Fluid25DCommandKind::Seek &&
            acknowledgement.lifecycle == Fluid25DLifecycle::Failed) {
            invalid("Playback Seek cannot clear or enter failed lifecycle");
        }
    } else if (acknowledgement.reset_generation != current.reset_generation ||
               application_time_s < current.physical_time_s) {
        invalid("Applied acknowledgement has stale generation or regressing physical time");
    }

    (void)expected_lifecycle_after(command, current.lifecycle, acknowledgement.lifecycle);
    outstanding_.erase(found);
    if (changes_generation) {
        apply_generation_boundary(acknowledgement.reset_generation, application_time_s,
                                  acknowledgement.lifecycle);
        return;
    }
    metadata_.session.physical_time_s = application_time_s;
    set_session_lifecycle(acknowledgement.lifecycle);
    metadata_.session.failure_message = acknowledgement.lifecycle == Fluid25DLifecycle::Failed
                                            ? acknowledgement.message
                                            : std::string{};
}

void Fluid25DSessionGuard::mark_failed(std::string message) {
    validate_message(message, "failure message");
    if (message.empty()) {
        invalid("failure message must be nonempty");
    }
    if (metadata_.session.lifecycle == Fluid25DLifecycle::Failed) {
        return;
    }
    if (metadata_.session.lifecycle == Fluid25DLifecycle::Completed ||
        metadata_.session.lifecycle == Fluid25DLifecycle::Stopped) {
        invalid("completed or stopped session cannot enter failed lifecycle");
    }
    outstanding_.clear();
    metadata_.session.lifecycle = Fluid25DLifecycle::Failed;
    metadata_.session.failure_message = std::move(message);
}

void Fluid25DSessionGuard::begin_generation(std::uint64_t reset_generation,
                                            double physical_time_s) {
    if (metadata_.session.reset_generation == std::numeric_limits<std::uint64_t>::max() ||
        reset_generation != metadata_.session.reset_generation + 1U) {
        invalid("explicit generation change must advance by exactly one");
    }
    if (!std::isfinite(physical_time_s) || physical_time_s < 0.0) {
        invalid("new generation physical_time_s must be finite and nonnegative");
    }
    apply_generation_boundary(reset_generation, physical_time_s, Fluid25DLifecycle::Ready);
}

const Fluid25DBackendMetadata& Fluid25DSessionGuard::metadata() const noexcept {
    return metadata_;
}

const Fluid25DSessionMetadata& Fluid25DSessionGuard::session() const noexcept {
    return metadata_.session;
}

std::uint64_t Fluid25DSessionGuard::last_command_id() const noexcept {
    return last_command_id_;
}

bool Fluid25DSessionGuard::failed() const noexcept {
    return metadata_.session.lifecycle == Fluid25DLifecycle::Failed;
}

void Fluid25DSessionGuard::set_session_lifecycle(Fluid25DLifecycle lifecycle) {
    metadata_.session.lifecycle = lifecycle;
}

void Fluid25DSessionGuard::apply_generation_boundary(std::uint64_t generation,
                                                     double physical_time_s,
                                                     Fluid25DLifecycle lifecycle) {
    if (metadata_.session.reset_generation == std::numeric_limits<std::uint64_t>::max() ||
        generation != metadata_.session.reset_generation + 1U) {
        invalid("generation boundary must advance by exactly one");
    }
    if (!std::isfinite(physical_time_s) || physical_time_s < 0.0) {
        invalid("generation boundary physical time must be finite and nonnegative");
    }
    if (lifecycle == Fluid25DLifecycle::Failed || lifecycle == Fluid25DLifecycle::Stopped) {
        invalid("new generation must not begin failed or stopped");
    }
    metadata_.session.reset_generation = generation;
    metadata_.session.physical_time_s = physical_time_s;
    metadata_.session.lifecycle = lifecycle;
    metadata_.session.failure_message.clear();
    outstanding_.clear();
}

} // namespace cubey::projects::fluid::fluid_25d
