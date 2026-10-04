#include "fluid_25d_external_session.h"

#include <cubey/asset/file_digest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace cubey::projects::fluid::fluid_25d {
namespace {
using Json = nlohmann::json;
constexpr std::array<unsigned char, 8> kMagic{'C', 'B', 'W', 'T', 'R', 'V', '1', 0};

[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("fluid external session: " + message);
}

// Fixed basenames only: never follow payload paths supplied by a producer.
std::vector<std::byte> read(const std::filesystem::path& directory, const char* name,
                            std::size_t maximum, std::optional<std::size_t> exact = {}) {
    const auto path = directory / name;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        !std::filesystem::is_regular_file(path))
        invalid(std::string(name) + " must be a regular non-symlink file");
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto end = file.tellg();
    if (!file || end < 0 || static_cast<std::uint64_t>(end) > maximum ||
        (exact && static_cast<std::uint64_t>(end) != *exact))
        invalid(std::string(name) + " size is invalid");
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file || file.peek() != std::char_traits<char>::eof())
        invalid(std::string(name) + " changed or could not be read");
    return bytes;
}

std::string document(const std::filesystem::path& directory, const char* name) {
    const auto bytes = read(directory, name, kFluid25DBackendMaximumJsonBytes);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

Json parse(const std::string& text) {
    std::vector<std::set<std::string>> keys;
    bool duplicate = false;
    auto result = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& item) {
        if (depth > static_cast<int>(kFluid25DBackendMaximumJsonDepth))
            invalid("state nesting exceeds limit");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        else if (event == Json::parse_event_t::key) {
            if (keys.empty() || !keys.back().insert(item.get<std::string>()).second)
                duplicate = true;
        } else if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    if (duplicate)
        invalid("duplicate state JSON key");
    return result;
}

template <class UInt> UInt unsigned_le(std::span<const std::byte> bytes, std::size_t offset) {
    UInt result = 0;
    for (std::size_t index = 0; index < sizeof(UInt); ++index)
        result |= static_cast<UInt>(std::to_integer<unsigned char>(bytes[offset + index]))
                  << (8U * index);
    return result;
}

float float_le(std::span<const std::byte> bytes, std::size_t offset) {
    return std::bit_cast<float>(unsigned_le<std::uint32_t>(bytes, offset));
}

double finite_number(const Json& object, const char* name, bool positive = false) {
    if (!object.at(name).is_number())
        invalid(std::string(name) + " is not a number");
    const double value = object.at(name).get<double>();
    if (!std::isfinite(value) || value < 0.0 || (positive && value == 0.0))
        invalid(std::string(name) + " is outside its range");
    return value;
}

Fluid25DBackendMetadata startup(const std::filesystem::path& directory) {
    auto metadata = decode_fluid_25d_backend_metadata_json(document(directory, "metadata.json"));
    if (metadata.kind != Fluid25DBackendKind::External ||
        metadata.profile != Fluid25DBackendProfile::ExternalService ||
        metadata.capability_source != Fluid25DCapabilitySource::ServiceHandshake ||
        !metadata.fields.momentum_x_m2_per_s || !metadata.fields.momentum_z_m2_per_s ||
        metadata.fields.tracer_depth_equivalent_m || metadata.fields.face_discharge_m3_per_s ||
        metadata.fields.water_ledger || metadata.fields.tracer_ledger ||
        metadata.session.frame_sequence != 0U || metadata.session.reset_generation != 1U ||
        metadata.session.physical_time_s != 0.0 ||
        metadata.session.lifecycle != Fluid25DLifecycle::Ready)
        invalid("unsupported binary service handshake");
    return metadata;
}

} // namespace

Fluid25DExternalSession::Fluid25DExternalSession(const std::filesystem::path& directory)
    : directory_(std::filesystem::canonical(directory)), immutable_metadata_(startup(directory_)),
      guard_(immutable_metadata_) {
    const auto& grid = metadata().grid;
    const auto count = static_cast<std::size_t>(grid.width) * grid.height;
    const auto bytes = read(directory_, "bed.f32", count * 4U, count * 4U);
    if (cubey::asset::sha256_hex(bytes) != grid.solver_bed_sha256)
        invalid("solver bed SHA-256 mismatch");
    bed_.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float value = float_le(bytes, index * 4U);
        if (!std::isfinite(value))
            invalid("nonfinite solver bed");
        bed_.push_back(value);
    }
}

Fluid25DExternalSession::~Fluid25DExternalSession() {
#ifndef _WIN32
    if (control_lock_ >= 0)
        ::close(control_lock_);
#endif
}

const Fluid25DBackendMetadata& Fluid25DExternalSession::metadata() const noexcept {
    return guard_.metadata();
}
const Fluid25DSessionGuard& Fluid25DExternalSession::guard() const noexcept {
    return guard_;
}
std::span<const float> Fluid25DExternalSession::bed() const noexcept {
    return bed_;
}
const std::filesystem::path& Fluid25DExternalSession::directory() const noexcept {
    return directory_;
}

std::optional<Fluid25DExternalSnapshot> Fluid25DExternalSession::load_latest() const {
    // metadata.json and bed.f32 are immutable for the entire worker incarnation.
    const auto current_metadata = startup(directory_);
    if (encode_fluid_25d_backend_metadata_json(immutable_metadata_) !=
        encode_fluid_25d_backend_metadata_json(current_metadata))
        invalid("worker handshake changed; reopen explicitly for a new session");
    const auto state_text = document(directory_, "state.json");
    const auto state = parse(state_text);
    const std::set<std::string> expected{
        "schema",      "frame",        "payload_sha256", "slot", "published_unix_s",
        "next_step_s", "rain_m_per_s", "pacing",         "ack"};
    std::set<std::string> actual;
    if (!state.is_object())
        invalid("state must be an object");
    for (auto item = state.begin(); item != state.end(); ++item)
        actual.insert(item.key());
    if (actual != expected || state.at("schema") != kFluid25DExternalStateSchema)
        invalid("unknown state schema or fields");
    Fluid25DExternalSnapshot result;
    result.header = decode_fluid_25d_frame_header_json(state.at("frame").dump());
    if (result.header.session_id != immutable_metadata_.session.session_id)
        invalid("state belongs to another worker session");
    result.next_step_s = finite_number(state, "next_step_s");
    result.rain_m_per_s = finite_number(state, "rain_m_per_s");
    result.pacing = finite_number(state, "pacing", true);
    const double published = finite_number(state, "published_unix_s", true);
    if (!state.at("ack").is_null())
        result.acknowledgement =
            decode_fluid_25d_command_acknowledgement_json(state.at("ack").dump());
    if (result.acknowledgement &&
        (result.acknowledgement->session_id != result.header.session_id ||
         result.acknowledgement->reset_generation > result.header.reset_generation ||
         (result.acknowledgement->reset_generation == result.header.reset_generation &&
          result.acknowledgement->application_time_s &&
          *result.acknowledgement->application_time_s > result.header.physical_time_s)))
        invalid("acknowledgement does not belong to this publication");
    if (!state.at("slot").is_number_unsigned() || state.at("slot").get<std::uint64_t>() > 2U)
        invalid("slot is outside the three-slot ring");
    const auto slot = state.at("slot").get<std::size_t>();
    if (slot != result.header.sequence % 3U)
        invalid("slot does not match frame sequence");
    const std::array<const char*, 3> names{"slot-0.bin", "slot-1.bin", "slot-2.bin"};
    const std::size_t count = bed_.size();
    const auto bytes = read(directory_, names[slot], 32U + count * 12U, 32U + count * 12U);
    bool identity_matches = true;
    for (std::size_t index = 0; index < kMagic.size(); ++index)
        identity_matches &= std::to_integer<unsigned char>(bytes[index]) == kMagic[index];
    identity_matches &= unsigned_le<std::uint64_t>(bytes, 8U) == result.header.reset_generation;
    identity_matches &= unsigned_le<std::uint64_t>(bytes, 16U) == result.header.sequence;
    identity_matches &= std::bit_cast<double>(unsigned_le<std::uint64_t>(bytes, 24U)) ==
                        result.header.physical_time_s;
    const auto digest = state.at("payload_sha256").get<std::string>();
    if (!identity_matches || !cubey::asset::is_sha256_hex(digest) ||
        cubey::asset::sha256_hex(bytes) != digest) {
        // Atomic rename can replace this slot while an older state is loaded.
        // Only a changed state descriptor permits retry; persistent corruption fails.
        if (document(directory_, "state.json") != state_text)
            return std::nullopt;
        invalid("current payload identity or SHA-256 mismatch");
    }
    auto fields = std::make_shared<Fluid25DRecordedFrame>();
    fields->time_s = result.header.physical_time_s;
    fields->published_unix_s = published;
    fields->depth_m.reserve(count);
    fields->momentum.reserve(count);
    fields->velocity.reserve(count);
    const double area = immutable_metadata_.grid.spacing_m * immutable_metadata_.grid.spacing_m;
    for (std::size_t index = 0; index < count; ++index) {
        const float h = float_le(bytes, 32U + index * 4U);
        const float qx = float_le(bytes, 32U + (count + index) * 4U);
        const float qz = float_le(bytes, 32U + (2U * count + index) * 4U);
        if (!std::isfinite(h) || h < 0.0F || !std::isfinite(qx) || !std::isfinite(qz))
            invalid("nonfinite fields or negative depth");
        const float vx = h > 0.0F ? qx / h : 0.0F;
        const float vz = h > 0.0F ? qz / h : 0.0F;
        const float speed = std::hypot(vx, vz);
        if (!std::isfinite(speed))
            invalid("nonfinite derived velocity");
        fields->depth_m.push_back(h);
        fields->momentum.push_back({qx, qz});
        fields->velocity.push_back({vx, vz});
        fields->stats.water_volume_m3 += h * area;
        fields->stats.max_depth_m = std::max(fields->stats.max_depth_m, h);
        fields->stats.max_speed_m_per_s = std::max(fields->stats.max_speed_m_per_s, speed);
        if (h == 0.0F && (qx != 0.0F || qz != 0.0F))
            ++fields->stats.dry_nonzero_momentum_cells;
    }
    result.fields = std::move(fields);
    return result;
}

bool Fluid25DExternalSession::accept(const Fluid25DExternalSnapshot& snapshot) {
    if (snapshot.header.sequence <= guard_.session().frame_sequence)
        return false;
    if (snapshot.acknowledgement &&
        snapshot.acknowledgement->command_id == std::numeric_limits<std::uint64_t>::max())
        invalid("command identity exhausted");
    auto candidate = guard_; // No partial acceptance if the publication is invalid.
    // A fresh attachment starts from the service's current generation, not a
    // fictional replay of all reset commands since its immutable descriptor.
    if (candidate.session().frame_sequence == 0U && !pending_ &&
        snapshot.header.reset_generation > candidate.session().reset_generation) {
        auto attachment = immutable_metadata_;
        attachment.session.reset_generation = snapshot.header.reset_generation;
        candidate = Fluid25DSessionGuard(std::move(attachment));
    }
    const std::string acknowledgement =
        snapshot.acknowledgement
            ? encode_fluid_25d_command_acknowledgement_json(*snapshot.acknowledgement)
            : std::string{};
    const bool matching_ack = snapshot.acknowledgement && pending_ &&
                              snapshot.acknowledgement->command_id == pending_->command_id &&
                              acknowledgement != last_acknowledgement_;
    if (matching_ack)
        candidate.accept_acknowledged_frame(*snapshot.acknowledgement, snapshot.header);
    else
        candidate.accept_frame(snapshot.header);
    guard_ = std::move(candidate);
    if (snapshot.acknowledgement) {
        next_command_id_ = std::max(next_command_id_, snapshot.acknowledgement->command_id + 1U);
    }
    if (matching_ack) {
        last_acknowledgement_ = acknowledgement;
        last_command_message_ = snapshot.acknowledgement->message;
        if (snapshot.acknowledgement->state != Fluid25DAcknowledgementState::Accepted)
            pending_.reset();
    }
    if (snapshot.header.lifecycle == Fluid25DLifecycle::Failed)
        pending_.reset();
    return true;
}

void Fluid25DExternalSession::send(Fluid25DCommandKind kind, std::optional<double> value) {
    if (pending_)
        invalid("one command is already pending");
#ifdef _WIN32
    invalid("external service controls require POSIX file locking");
#else
    if (control_lock_ < 0) {
        const auto lock_path = directory_ / "control.lock";
        const int fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0)
            invalid("controller lock could not be opened");
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd);
            invalid("another controller owns this session");
        }
        control_lock_ = fd;
        if (std::filesystem::exists(directory_ / "command.json")) {
            const auto previous =
                decode_fluid_25d_control_command_json(document(directory_, "command.json"));
            if (previous.session_id != guard_.session().session_id ||
                previous.command_id >= next_command_id_)
                invalid("previous controller command is unresolved; wait for its acknowledgement");
        }
    }
#endif
    Fluid25DControlCommand command{.command_id = next_command_id_,
                                   .session_id = guard_.session().session_id,
                                   .reset_generation = guard_.session().reset_generation,
                                   .domain = Fluid25DControlDomain::Solver,
                                   .kind = kind,
                                   .value = value};
    auto candidate = guard_;
    candidate.observe_command(command);
    const auto text = encode_fluid_25d_control_command_json(command);
    // Explicit commands only; attaching/closing a viewer does not mutate the solver.
    // Multiple controllers are not supported: the worker must reject nonmonotone IDs.
    const auto temporary = directory_ / "command.json.tmp";
    const auto destination = directory_ / "command.json";
    if (std::filesystem::exists(temporary) || std::filesystem::is_symlink(destination))
        invalid("command path is unsafe or another controller is writing");
    try {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        file.close();
        if (!file)
            invalid("command could not be written");
        std::filesystem::rename(temporary, destination);
        guard_ = std::move(candidate);
        pending_ = command;
        ++next_command_id_;
    } catch (...) {
        guard_.mark_failed("command publication failed");
        throw;
    }
}

bool Fluid25DExternalSession::command_pending() const noexcept {
    return pending_.has_value();
}
const std::string& Fluid25DExternalSession::last_command_message() const noexcept {
    return last_command_message_;
}

} // namespace cubey::projects::fluid::fluid_25d
