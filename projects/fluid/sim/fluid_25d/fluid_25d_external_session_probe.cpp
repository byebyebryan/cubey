#include "fluid_25d_external_session.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

// Explicit study tool: never registered as a native/CUDA test or app launcher.
// It exercises the exact MIT client used by Cubey against a supplied session.
int main(int argc, char** argv) {
    using namespace cubey::projects::fluid::fluid_25d;
    using Json = nlohmann::json;
    try {
        if (argc < 2 || argc > 4)
            throw std::runtime_error("usage: external_session_probe DIR [COMMAND [VALUE]]");
        Fluid25DExternalSession client(argv[1]);
        auto snapshot = client.load_latest();
        if (!snapshot)
            throw std::runtime_error("snapshot slot was overtaken; retry the probe");
        client.accept(*snapshot);
        double acknowledgement_ms = 0;
        if (argc >= 3) {
            const std::string kind = argv[2];
            Fluid25DCommandKind command;
            if (kind == "pause")
                command = Fluid25DCommandKind::Pause;
            else if (kind == "resume")
                command = Fluid25DCommandKind::Resume;
            else if (kind == "reset")
                command = Fluid25DCommandKind::Reset;
            else if (kind == "stop")
                command = Fluid25DCommandKind::Stop;
            else if (kind == "step")
                command = Fluid25DCommandKind::Step;
            else if (kind == "set_rain")
                command = Fluid25DCommandKind::SetRain;
            else if (kind == "set_time_scale")
                command = Fluid25DCommandKind::SetTimeScale;
            else
                throw std::runtime_error("unknown solver command");
            const std::optional<double> value =
                argc == 4 ? std::optional<double>(std::stod(argv[3])) : std::nullopt;
            const auto started = std::chrono::steady_clock::now();
            client.send(command, value);
            while (client.command_pending()) {
                if (std::chrono::steady_clock::now() - started > std::chrono::seconds(5))
                    throw std::runtime_error("native command acknowledgement timed out");
                if (auto next = client.load_latest()) {
                    if (client.accept(*next))
                        snapshot = std::move(next);
                }
                if (client.command_pending())
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            acknowledgement_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
        }
        const auto& fields = *snapshot->fields;
        const double now =
            std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        std::cout << Json{{"schema", "cubey.fluid25d.external-probe.v1"},
                          {"metadata",
                           Json::parse(encode_fluid_25d_backend_metadata_json(client.metadata()))},
                          {"frame",
                           Json::parse(encode_fluid_25d_frame_header_json(snapshot->header))},
                          {"ack", snapshot->acknowledgement
                                      ? Json::parse(encode_fluid_25d_command_acknowledgement_json(
                                            *snapshot->acknowledgement))
                                      : Json(nullptr)},
                          {"acknowledgement_ms", acknowledgement_ms},
                          {"state_age_s", now - fields.published_unix_s},
                          {"rain_m_per_s", snapshot->rain_m_per_s},
                          {"pacing", snapshot->pacing},
                          {"next_step_s", snapshot->next_step_s},
                          {"water_volume_m3", fields.stats.water_volume_m3},
                          {"max_depth_m", fields.stats.max_depth_m},
                          {"max_speed_m_per_s", fields.stats.max_speed_m_per_s},
                          {"dry_nonzero_momentum_cells", fields.stats.dry_nonzero_momentum_cells}}
                         .dump()
                  << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "external_session_probe: " << error.what() << '\n';
        return 1;
    }
}
