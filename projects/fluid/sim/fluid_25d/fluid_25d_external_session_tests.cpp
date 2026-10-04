#include "fluid_25d_external_session.h"

#include <cubey/asset/file_digest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
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
    require(failed, "expected failure did not occur");
}
void write(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    require(static_cast<bool>(file), "fixture write failed");
}
void text(const std::filesystem::path& path, const std::string& value) {
    write(path, std::as_bytes(std::span{value}));
}
template <class UInt> void append(std::vector<std::byte>& bytes, UInt value) {
    for (std::size_t index = 0; index < sizeof(UInt); ++index)
        bytes.push_back(static_cast<std::byte>((value >> (8U * index)) & 255U));
}
void floats(std::vector<std::byte>& bytes, std::span<const float> values) {
    for (float value : values)
        append(bytes, std::bit_cast<std::uint32_t>(value));
}

struct Fixture {
    std::filesystem::path root;
    Fluid25DBackendMetadata metadata;
    std::vector<std::byte> payload;
    Json state;
    explicit Fixture(const std::filesystem::path& path) : root(path) {
        std::filesystem::create_directory(root);
        const std::array<float, 6> elevations{1, 2, 3, 10, 20, 30};
        std::vector<std::byte> bed;
        floats(bed, elevations);
        write(root / "bed.f32", bed);
        metadata.kind = Fluid25DBackendKind::External;
        metadata.profile = Fluid25DBackendProfile::ExternalService;
        metadata.capability_source = Fluid25DCapabilitySource::ServiceHandshake;
        metadata.id = "synxflow-service-v1";
        metadata.grid = {.width = 3,
                         .height = 2,
                         .spacing_m = 2.0,
                         .input_sha256 = std::string(64, '1'),
                         .solver_bed_sha256 = cubey::asset::sha256_hex(bed)};
        metadata.fields.depth_m = metadata.fields.horizontal_velocity_x_m_per_s =
            metadata.fields.horizontal_velocity_z_m_per_s = metadata.fields.momentum_x_m2_per_s =
                metadata.fields.momentum_z_m2_per_s = true;
        auto& caps = metadata.capabilities.solver;
        caps.pause = caps.resume = caps.reset = caps.stop = caps.step = caps.set_rain =
            caps.set_time_scale = true;
        metadata.session.session_id = "synthetic-service-1";
        metadata.session.reset_generation = 1;
        text(root / "metadata.json", encode_fluid_25d_backend_metadata_json(metadata));
        publish(1, 1, 5.0, Fluid25DLifecycle::Running);
    }
    void publish(std::uint64_t sequence, std::uint64_t generation, double time,
                 Fluid25DLifecycle lifecycle,
                 std::optional<Fluid25DCommandAcknowledgement> ack = {}) {
        Fluid25DFrameHeader frame{.session_id = metadata.session.session_id,
                                  .reset_generation = generation,
                                  .sequence = sequence,
                                  .physical_time_s = time,
                                  .lifecycle = lifecycle};
        payload.clear();
        for (unsigned char value :
             std::array<unsigned char, 8>{'C', 'B', 'W', 'T', 'R', 'V', '1', 0})
            payload.push_back(static_cast<std::byte>(value));
        append(payload, generation);
        append(payload, sequence);
        append(payload, std::bit_cast<std::uint64_t>(time));
        const std::array<float, 6> h{1, 2, 0, 4, 5, 6};
        const std::array<float, 6> qx{2, 4, 0, 8, 10, 12};
        const std::array<float, 6> qz{-3, -6, 0, -12, -15, -18};
        floats(payload, h);
        floats(payload, qx);
        floats(payload, qz);
        state = {{"schema", kFluid25DExternalStateSchema},
                 {"frame", Json::parse(encode_fluid_25d_frame_header_json(frame))},
                 {"payload_sha256", cubey::asset::sha256_hex(payload)},
                 {"slot", sequence % 3U},
                 {"published_unix_s", 1791100000.0},
                 {"next_step_s", 0.5},
                 {"rain_m_per_s", 0.0000333333333},
                 {"pacing", 60.0},
                 {"ack", ack ? Json::parse(encode_fluid_25d_command_acknowledgement_json(*ack))
                             : Json(nullptr)}};
        save();
    }
    void save() {
        write(root / ("slot-" + std::to_string(state.at("slot").get<unsigned>()) + ".bin"),
              payload);
        text(root / "state.json", state.dump());
    }
    Fluid25DCommandAcknowledgement ack(std::uint64_t id, std::uint64_t generation, double time,
                                       Fluid25DLifecycle lifecycle) const {
        return {.command_id = id,
                .session_id = metadata.session.session_id,
                .reset_generation = generation,
                .state = Fluid25DAcknowledgementState::Applied,
                .application_time_s = time,
                .lifecycle = lifecycle,
                .message = "boundary applied"};
    }
};

void lifecycle(const std::filesystem::path& root) {
    Fixture fixture(root);
    Fluid25DExternalSession session(root);
    auto frame = session.load_latest();
    require(frame && session.accept(*frame), "initial frame rejected");
    require(frame->fields->depth_m.size() == 6 && frame->fields->velocity[3].x_m_per_s == 2 &&
                frame->fields->velocity[3].y_m_per_s == -3 &&
                frame->fields->stats.water_volume_m3 == 72,
            "binary field mapping or statistics incorrect");
    require(!session.accept(*frame), "duplicate publication accepted");
    session.send(Fluid25DCommandKind::Pause);
    throws([&] { session.send(Fluid25DCommandKind::Resume); });
    fixture.publish(2, 1, 6, Fluid25DLifecycle::Paused,
                    fixture.ack(1, 1, 6, Fluid25DLifecycle::Paused));
    require(session.accept(*session.load_latest()) && !session.command_pending(),
            "pause ack failed");
    session.send(Fluid25DCommandKind::Reset);
    fixture.publish(3, 2, 0, Fluid25DLifecycle::Ready,
                    fixture.ack(2, 2, 0, Fluid25DLifecycle::Ready));
    require(session.accept(*session.load_latest()) &&
                session.guard().session().reset_generation == 2,
            "reset generation not accepted");
    fixture.publish(4, 1, 10, Fluid25DLifecycle::Running);
    throws([&] { session.accept(*session.load_latest()); });
    require(session.guard().session().frame_sequence == 3, "failed accept mutated guard");
    fixture.publish(4, 2, 1, Fluid25DLifecycle::Running);
    require(session.accept(*session.load_latest()), "new generation advance failed");
    session.send(Fluid25DCommandKind::Stop);
    fixture.publish(5, 2, 2, Fluid25DLifecycle::Stopped,
                    fixture.ack(3, 2, 2, Fluid25DLifecycle::Stopped));
    require(session.accept(*session.load_latest()) && !session.command_pending() &&
                session.guard().session().frame_sequence == 5,
            "terminal stop acknowledgement failed");
}

void pending_step(const std::filesystem::path& root) {
    Fixture fixture(root);
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Paused);
    Fluid25DExternalSession session(root);
    session.accept(*session.load_latest());
    session.send(Fluid25DCommandKind::Step);
    auto accepted = fixture.ack(1, 1, 5, Fluid25DLifecycle::Paused);
    accepted.state = Fluid25DAcknowledgementState::Accepted;
    accepted.application_time_s.reset();
    fixture.publish(2, 1, 5, Fluid25DLifecycle::Paused, accepted);
    require(session.accept(*session.load_latest()) && session.command_pending(),
            "Step Accepted lost pending command");
    fixture.publish(3, 1, 5, Fluid25DLifecycle::Paused, accepted);
    require(session.accept(*session.load_latest()),
            "repeated retained Accepted ack was mistaken for a new ack");
    fixture.publish(4, 1, 6, Fluid25DLifecycle::Paused,
                    fixture.ack(1, 1, 6, Fluid25DLifecycle::Paused));
    require(session.accept(*session.load_latest()) && !session.command_pending(),
            "Step Applied did not finish command");
}

void attach_after_reset(const std::filesystem::path& root) {
    Fixture fixture(root);
    fixture.publish(15, 4, 100, Fluid25DLifecycle::Running,
                    fixture.ack(9, 4, 90, Fluid25DLifecycle::Running));
    Fluid25DExternalSession session(root);
    require(session.accept(*session.load_latest()) &&
                session.guard().session().reset_generation == 4,
            "fresh attachment could not adopt current generation");
    session.send(Fluid25DCommandKind::Pause);
    std::ifstream file(root / "command.json");
    std::string raw((std::istreambuf_iterator<char>(file)), {});
    require(decode_fluid_25d_control_command_json(raw).command_id == 10,
            "reattached controller reused an old command id");
}

void corruption(const std::filesystem::path& root) {
    Fixture fixture(root);
    Fluid25DExternalSession session(root);
    fixture.payload[32] ^= std::byte{1};
    fixture.save();
    throws([&] { static_cast<void>(session.load_latest()); });
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Running);
    fixture.payload[0] = std::byte{0};
    fixture.state["payload_sha256"] = cubey::asset::sha256_hex(fixture.payload);
    fixture.save();
    throws([&] { static_cast<void>(session.load_latest()); });
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Running);
    fixture.state["slot"] = 9;
    text(root / "state.json", fixture.state.dump());
    throws([&] { static_cast<void>(session.load_latest()); });
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Running);
    fixture.payload.resize(33);
    fixture.save();
    throws([&] { static_cast<void>(session.load_latest()); });
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Running);
    // A valid hash must not hide a nonfinite field.
    const auto nan = std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN());
    for (std::size_t i = 0; i < 4; ++i)
        fixture.payload[32 + i] = static_cast<std::byte>((nan >> (8U * i)) & 255U);
    fixture.state["payload_sha256"] = cubey::asset::sha256_hex(fixture.payload);
    fixture.save();
    throws([&] { static_cast<void>(session.load_latest()); });
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Running);
    auto duplicate = fixture.state.dump();
    duplicate.insert(1, "\"schema\":\"bad\",");
    text(root / "state.json", duplicate);
    throws([&] { static_cast<void>(session.load_latest()); });
    fixture.publish(1, 1, 5, Fluid25DLifecycle::Running);
    fixture.metadata.session.session_id = "restarted-worker";
    text(root / "metadata.json", encode_fluid_25d_backend_metadata_json(fixture.metadata));
    throws([&] { static_cast<void>(session.load_latest()); });
}

void control_owner(const std::filesystem::path& root) {
    Fixture fixture(root);
    Fluid25DExternalSession first(root), second(root);
    first.accept(*first.load_latest());
    second.accept(*second.load_latest());
    first.send(Fluid25DCommandKind::Pause);
    throws([&] { second.send(Fluid25DCommandKind::Pause); });
    require(!second.command_pending(), "rejected controller queued command");
}
} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("cubey-service-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try {
        lifecycle(root / "lifecycle");
        pending_step(root / "pending-step");
        attach_after_reset(root / "reattach");
        corruption(root / "corruption");
        control_owner(root / "owner");
        std::filesystem::remove_all(root);
        std::cout << "external session binary/lifecycle/integrity controls passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << " | retained fixture " << root << '\n';
        return 1;
    }
}
