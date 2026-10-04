#pragma once

#include "fluid_25d_backend_contract.h"

#include <string>

namespace cubey::projects::fluid::fluid_25d {

struct Fluid25DProjectConfig;
struct Fluid25DScenarioData;
class Fluid25DRecording;

// Build immutable startup metadata for an existing built-in simulation. This
// describes the supplied inputs and statically known UI controls; it does not
// claim that a GPU solver has completed initialization or expose frame data.
[[nodiscard]] Fluid25DBackendMetadata
make_fluid_25d_builtin_backend_metadata(const Fluid25DProjectConfig& config,
                                        const Fluid25DScenarioData& scenario,
                                        std::string session_id);

// Build startup metadata for the existing recording reader/viewer. For a live
// stock stream the returned session belongs only to this viewer playhead; the
// producer's stream identity and lifecycle remain separate.
[[nodiscard]] Fluid25DBackendMetadata
make_fluid_25d_recording_backend_metadata(const Fluid25DRecording& recording,
                                          std::string viewer_session_id);

// Return a process-local unique, contract-valid session identifier suitable
// for assigning to a new startup/viewer session.
[[nodiscard]] std::string fluid_25d_new_backend_session_id();

} // namespace cubey::projects::fluid::fluid_25d
