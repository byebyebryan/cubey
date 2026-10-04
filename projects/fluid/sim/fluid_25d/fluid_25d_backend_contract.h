#pragma once

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::string_view kFluid25DBackendSessionSchema =
    "cubey.fluid25d.backend-session.v1";
inline constexpr std::size_t kFluid25DBackendMaximumCells = 4'194'304U;
inline constexpr std::uint32_t kFluid25DBackendMaximumDimension = 16'384U;
inline constexpr std::size_t kFluid25DBackendMaximumJsonBytes = 64U * 1024U;
inline constexpr std::size_t kFluid25DBackendMaximumJsonDepth = 8U;
inline constexpr std::size_t kFluid25DBackendMaximumIdentifierBytes = 128U;
inline constexpr std::size_t kFluid25DBackendMaximumMessageBytes = 512U;
inline constexpr std::size_t kFluid25DBackendMaximumOutstandingCommands = 1U;

enum class Fluid25DBackendKind : std::uint8_t {
    Builtin,
    External,
    Recorded,
};

// Profiles keep solver methods and data playback distinct while sharing the
// same metadata and control envelope. ExternalService capabilities must come
// from that service's handshake; they are never inferred from its name.
enum class Fluid25DBackendProfile : std::uint8_t {
    BuiltinVirtualPipes,
    BuiltinFiniteVolume,
    RecordingPlayback,
    StockExternalBridge,
    ExternalService,
};

enum class Fluid25DCapabilitySource : std::uint8_t {
    BuiltinAdapter,
    RecordingFormat,
    ViewingOnlyBridge,
    ServiceHandshake,
};

enum class Fluid25DGridOrientation : std::uint8_t {
    RowMajorXFastestZRows,
};

enum class Fluid25DLifecycle : std::uint8_t {
    Ready,
    Running,
    Paused,
    Completed,
    Failed,
    Stopped,
};

enum class Fluid25DControlDomain : std::uint8_t {
    // Mutates the numerical producer and its physical timeline.
    Solver,
    // Mutates a viewer/playback cursor only. For a live bridge, this viewer
    // timeline is a separate logical session from the native producer's
    // timeline; viewer pause/seek/rate never imply producer pause/seek/rate.
    Playback,
};

enum class Fluid25DCommandKind : std::uint8_t {
    Pause,
    Resume,
    Reset,
    Stop,
    Step,
    Seek,
    SetRain,
    SetTimeScale,
};

enum class Fluid25DAcknowledgementState : std::uint8_t {
    Accepted,
    Applied,
    Rejected,
};

struct Fluid25DGridContract {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    double spacing_m = 0.0;
    Fluid25DGridOrientation orientation = Fluid25DGridOrientation::RowMajorXFastestZRows;
    // SHA-256 of the complete immutable input identity and of the exact solver
    // bed, encoded as row-major little-endian float32 metres, respectively.
    std::string input_sha256{};
    std::string solver_bed_sha256{};
};

struct Fluid25DFieldAvailability {
    bool depth_m = false;
    bool horizontal_velocity_x_m_per_s = false;
    bool horizontal_velocity_z_m_per_s = false;
    bool momentum_x_m2_per_s = false;
    bool momentum_z_m2_per_s = false;
    bool tracer_depth_equivalent_m = false;
    // If present, this is directed per-face volumetric discharge in m^3/s,
    // not a per-width flux. Native VP diagnostics are per-cell left/right/
    // down/up faces; current FV metadata must leave this unavailable unless
    // it exposes an equivalent native diagnostic.
    bool face_discharge_m3_per_s = false;
    bool water_ledger = false;
    bool tracer_ledger = false;
};

struct Fluid25DControlCapabilities {
    bool pause = false;
    bool resume = false;
    bool reset = false;
    bool stop = false;
    bool step = false;
    bool seek = false;
    // Rain availability is run/scenario-specific and must not be inferred
    // solely from a built-in VP/FV backend profile.
    bool set_rain = false;
    bool set_time_scale = false;
};

struct Fluid25DBackendCapabilities {
    // Solver controls advance or alter the numerical producer.
    Fluid25DControlCapabilities solver{};
    // Playback controls only move through already-published frames.
    Fluid25DControlCapabilities playback{};
};

struct Fluid25DSessionMetadata {
    // Unique to this logical session; do not reuse it after a worker/session
    // restart, so commands or frames from the prior incarnation stay stale.
    std::string session_id{};
    std::uint64_t reset_generation = 0U;
    // Zero is the pre-first-frame cursor. Once frames arrive, this remains
    // strictly increasing for the lifetime of the session, including resets.
    std::uint64_t frame_sequence = 0U;
    // Producer physical time for solver sessions; selected-frame/playhead time
    // for playback sessions. Keep live producer and viewer sessions distinct.
    double physical_time_s = 0.0;
    Fluid25DLifecycle lifecycle = Fluid25DLifecycle::Ready;
    std::string failure_message{};
};

struct Fluid25DBackendMetadata {
    Fluid25DBackendKind kind = Fluid25DBackendKind::Builtin;
    Fluid25DBackendProfile profile = Fluid25DBackendProfile::BuiltinVirtualPipes;
    Fluid25DCapabilitySource capability_source = Fluid25DCapabilitySource::BuiltinAdapter;
    std::string id{};
    Fluid25DGridContract grid{};
    Fluid25DFieldAvailability fields{};
    Fluid25DBackendCapabilities capabilities{};
    Fluid25DSessionMetadata session{};
};

// A frame header is deliberately metadata-only. This contract transports no
// field arrays and does not require full-field CPU readback from a GPU solver.
struct Fluid25DFrameHeader {
    std::string session_id{};
    std::uint64_t reset_generation = 0U;
    std::uint64_t sequence = 0U;
    double physical_time_s = 0.0;
    Fluid25DLifecycle lifecycle = Fluid25DLifecycle::Ready;
    std::string failure_message{};
};

struct Fluid25DControlCommand {
    std::uint64_t command_id = 0U;
    std::string session_id{};
    std::uint64_t reset_generation = 0U;
    Fluid25DControlDomain domain = Fluid25DControlDomain::Solver;
    Fluid25DCommandKind kind = Fluid25DCommandKind::Pause;
    // SetRain is metres/second, SetTimeScale is a positive multiplier, and
    // Playback Seek is an absolute physical time in seconds. Step requests one
    // domain-specific advance/navigation operation and is only valid when
    // explicitly advertised. Other commands must not carry a value.
    std::optional<double> value{};
};

struct Fluid25DCommandAcknowledgement {
    std::uint64_t command_id = 0U;
    std::string session_id{};
    // The resulting generation. Applied Reset and Playback Seek advance it.
    std::uint64_t reset_generation = 0U;
    Fluid25DAcknowledgementState state = Fluid25DAcknowledgementState::Accepted;
    // Present only for Applied; it is the physical time at the application
    // boundary in the addressed domain (producer time for Solver, viewer
    // playhead for Playback), not message receipt time.
    std::optional<double> application_time_s{};
    Fluid25DLifecycle lifecycle = Fluid25DLifecycle::Ready;
    std::string message{};
};

// Throws std::invalid_argument for malformed or unsupported contract values.
void validate_fluid_25d_backend_metadata(const Fluid25DBackendMetadata& metadata);
void validate_fluid_25d_frame_header(const Fluid25DFrameHeader& frame);
void validate_fluid_25d_control_command(const Fluid25DControlCommand& command);
void validate_fluid_25d_command_acknowledgement(
    const Fluid25DCommandAcknowledgement& acknowledgement);
[[nodiscard]] bool fluid_25d_command_supported(const Fluid25DBackendCapabilities& capabilities,
                                               Fluid25DControlDomain domain,
                                               Fluid25DCommandKind kind) noexcept;

// Capabilities known from the current retained interfaces. The stock stream
// bridge and recording player expose viewer controls but no solver controls;
// these controls do not pause or otherwise mutate a live producer.
[[nodiscard]] Fluid25DBackendCapabilities fluid_25d_recording_playback_capabilities() noexcept;
[[nodiscard]] Fluid25DBackendCapabilities fluid_25d_stock_external_bridge_capabilities() noexcept;

// Strict, bounded JSON codecs. Decoders reject unknown schema versions/types,
// missing or extra fields, invalid values, and documents over the byte limit.
[[nodiscard]] std::string
encode_fluid_25d_backend_metadata_json(const Fluid25DBackendMetadata& metadata);
[[nodiscard]] Fluid25DBackendMetadata
decode_fluid_25d_backend_metadata_json(std::string_view document);
[[nodiscard]] std::string encode_fluid_25d_frame_header_json(const Fluid25DFrameHeader& frame);
[[nodiscard]] Fluid25DFrameHeader decode_fluid_25d_frame_header_json(std::string_view document);
[[nodiscard]] std::string
encode_fluid_25d_control_command_json(const Fluid25DControlCommand& command);
[[nodiscard]] Fluid25DControlCommand
decode_fluid_25d_control_command_json(std::string_view document);
[[nodiscard]] std::string encode_fluid_25d_command_acknowledgement_json(
    const Fluid25DCommandAcknowledgement& acknowledgement);
[[nodiscard]] Fluid25DCommandAcknowledgement
decode_fluid_25d_command_acknowledgement_json(std::string_view document);

// Session guard enforces generation/session ownership, strictly increasing
// frame and command identities, lifecycle transitions, capability gating and
// acknowledgement state progression. At most one command may be outstanding.
// A Reset or Playback Seek application is a generation boundary; old frames
// then remain stale even if they arrive late. Use separate logical sessions
// and guards for a live producer timeline and its independently controlled
// viewer playhead.
class Fluid25DSessionGuard {
  public:
    explicit Fluid25DSessionGuard(Fluid25DBackendMetadata metadata);

    void accept_frame(const Fluid25DFrameHeader& frame);
    void observe_command(const Fluid25DControlCommand& command);
    void accept_acknowledgement(const Fluid25DCommandAcknowledgement& acknowledgement);
    void mark_failed(std::string message);
    // For an explicit reset announced by a service handshake rather than an
    // Applied Reset acknowledgement. Session IDs cannot be replaced in-place.
    void begin_generation(std::uint64_t reset_generation, double physical_time_s = 0.0);

    [[nodiscard]] const Fluid25DBackendMetadata& metadata() const noexcept;
    [[nodiscard]] const Fluid25DSessionMetadata& session() const noexcept;
    [[nodiscard]] std::uint64_t last_command_id() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

  private:
    struct OutstandingCommand {
        Fluid25DControlCommand command{};
        bool accepted = false;
    };

    void set_session_lifecycle(Fluid25DLifecycle lifecycle);
    void apply_generation_boundary(std::uint64_t generation, double physical_time_s,
                                   Fluid25DLifecycle lifecycle);

    Fluid25DBackendMetadata metadata_{};
    std::map<std::uint64_t, OutstandingCommand> outstanding_{};
    std::uint64_t last_command_id_ = 0U;
};

} // namespace cubey::projects::fluid::fluid_25d
