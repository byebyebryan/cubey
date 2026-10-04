#pragma once

#include "fluid_25d_backend_contract.h"

#include <limits>
#include <stdexcept>
#include <utility>

namespace cubey::projects::fluid::fluid_25d {

// A completed asynchronous load belongs to both a saved frame and the viewer
// generation that requested it. Returning to the same index after a seek must
// not make work from the previous generation current again.
[[nodiscard]] inline bool
fluid_25d_local_frame_load_is_current(std::size_t loaded_index, std::uint64_t loaded_generation,
                                      std::size_t wanted_index,
                                      std::uint64_t viewer_generation) noexcept {
    return loaded_index == wanted_index && loaded_generation == viewer_generation;
}

// Metadata-only adapter for the existing in-process producers. Applications
// supply a completed GPU boundary or a logical playback boundary; this class
// never advances a solver, reads a full field, or converts momentum/discharge.
class Fluid25DLocalSession {
  public:
    explicit Fluid25DLocalSession(Fluid25DBackendMetadata metadata) : guard_(std::move(metadata)) {}

    [[nodiscard]] const Fluid25DBackendMetadata& metadata() const noexcept {
        return guard_.metadata();
    }

    [[nodiscard]] Fluid25DCommandAcknowledgement
    apply(Fluid25DControlDomain domain, Fluid25DCommandKind kind, double boundary_time_s,
          Fluid25DLifecycle resulting_lifecycle, std::optional<double> value = {}) {
        if (next_command_id_ == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("local backend command identity exhausted");
        const auto& current = guard_.session();
        const bool barrier =
            kind == Fluid25DCommandKind::Reset ||
            (domain == Fluid25DControlDomain::Playback && kind == Fluid25DCommandKind::Seek);
        if (barrier && current.reset_generation == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("local backend generation identity exhausted");
        const Fluid25DControlCommand command{.command_id = next_command_id_,
                                             .session_id = current.session_id,
                                             .reset_generation = current.reset_generation,
                                             .domain = domain,
                                             .kind = kind,
                                             .value = value};
        const Fluid25DCommandAcknowledgement ack{.command_id = command.command_id,
                                                 .session_id = command.session_id,
                                                 .reset_generation =
                                                     current.reset_generation + (barrier ? 1U : 0U),
                                                 .state = Fluid25DAcknowledgementState::Applied,
                                                 .application_time_s = boundary_time_s,
                                                 .lifecycle = resulting_lifecycle,
                                                 .message = "applied at in-process boundary"};
        auto candidate = guard_;
        candidate.observe_command(command);
        candidate.accept_acknowledgement(ack);
        guard_ = std::move(candidate);
        ++next_command_id_;
        return ack;
    }

    [[nodiscard]] Fluid25DFrameHeader publish(double time_s, Fluid25DLifecycle lifecycle) {
        const auto& current = guard_.session();
        if (current.frame_sequence == std::numeric_limits<std::uint64_t>::max())
            throw std::runtime_error("local backend frame identity exhausted");
        const Fluid25DFrameHeader frame{.session_id = current.session_id,
                                        .reset_generation = current.reset_generation,
                                        .sequence = current.frame_sequence + 1U,
                                        .physical_time_s = time_s,
                                        .lifecycle = lifecycle,
                                        .failure_message = {}};
        guard_.accept_frame(frame);
        return frame;
    }

    void fail(std::string_view message) {
        guard_.mark_failed(std::string(message));
    }

  private:
    Fluid25DSessionGuard guard_;
    std::uint64_t next_command_id_ = 1U;
};

} // namespace cubey::projects::fluid::fluid_25d
