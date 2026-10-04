#pragma once

#include "fluid_25d_backend_contract.h"
#include "fluid_25d_recording.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr std::string_view kFluid25DExternalStateSchema = "cubey.fluid25d.external-state.v1";

// Three atomically replaced binary slots; no growing recording prefix. Payloads
// are depth/qx/qz float32-LE planes, prefixed by magic/generation/sequence/time.
struct Fluid25DExternalSnapshot {
    Fluid25DFrameHeader header{};
    std::shared_ptr<const Fluid25DRecordedFrame> fields{};
    std::optional<Fluid25DCommandAcknowledgement> acknowledgement{};
    double next_step_s = 0.0;
    double rain_m_per_s = 0.0;
    double pacing = 1.0;
};

// Project-local file transport only. No CUDA API, native process launch, solver
// execution, or numerical-state transfer. Loading can run on an I/O thread;
// acceptance/commands belong to the caller's single control thread.
class Fluid25DExternalSession {
  public:
    explicit Fluid25DExternalSession(const std::filesystem::path& directory);
    ~Fluid25DExternalSession();
    Fluid25DExternalSession(const Fluid25DExternalSession&) = delete;
    Fluid25DExternalSession& operator=(const Fluid25DExternalSession&) = delete;

    [[nodiscard]] const Fluid25DBackendMetadata& metadata() const noexcept;
    [[nodiscard]] const Fluid25DSessionGuard& guard() const noexcept;
    [[nodiscard]] std::span<const float> bed() const noexcept;
    [[nodiscard]] const std::filesystem::path& directory() const noexcept;
    // A slot overtaken by the producer is an expected skipped display frame,
    // not corruption. Integrity errors on a still-current slot fail closed.
    [[nodiscard]] std::optional<Fluid25DExternalSnapshot> load_latest() const;
    // Applies acknowledgement before its same-publication generation/frame.
    // Returns false for an already displayed publication.
    bool accept(const Fluid25DExternalSnapshot& snapshot);
    void send(Fluid25DCommandKind kind, std::optional<double> value = {});
    [[nodiscard]] bool command_pending() const noexcept;
    [[nodiscard]] const std::string& last_command_message() const noexcept;

  private:
    std::filesystem::path directory_{};
    Fluid25DBackendMetadata immutable_metadata_{};
    Fluid25DSessionGuard guard_;
    std::vector<float> bed_{};
    std::optional<Fluid25DControlCommand> pending_{};
    std::string last_command_message_{};
    std::string last_acknowledgement_{};
    std::uint64_t next_command_id_ = 1U;
    int control_lock_ = -1;
};

} // namespace cubey::projects::fluid::fluid_25d
