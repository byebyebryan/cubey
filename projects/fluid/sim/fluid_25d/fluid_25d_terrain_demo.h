#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cubey::projects::fluid::fluid_25d {

inline constexpr const char* kFluid25DTerrainDemoSchema = "cubey.fluid25d.terrain-demo.v1";

struct Fluid25DTerrainDemoCase {
    std::string id, site, site_label, configuration, label, extent;
    std::filesystem::path case_directory, surface_grid;
    std::uint32_t width = 0, height = 0;
    double spacing_m = 0, source_spacing_m = 0, duration_s = 0, rain_mm_per_hour = 0;
    std::string input_sha256, solver_bed_sha256, error;
    bool available = false;
};

struct Fluid25DTerrainDemoStatus {
    std::uint64_t request_id = 0;
    std::string phase, case_id, session_name, session_id, error;
    double published_unix_s = 0;
    bool worker_available = false;
};

// Launch-control transport only, separate from the immutable solver handshake.
// A launcher owns the processes; this client requests/acknowledges replacement.
class Fluid25DTerrainDemoControl {
  public:
    explicit Fluid25DTerrainDemoControl(const std::filesystem::path& directory);
    ~Fluid25DTerrainDemoControl();
    Fluid25DTerrainDemoControl(const Fluid25DTerrainDemoControl&) = delete;
    Fluid25DTerrainDemoControl& operator=(const Fluid25DTerrainDemoControl&) = delete;

    const std::vector<Fluid25DTerrainDemoCase>& cases() const {
        return cases_;
    }
    const Fluid25DTerrainDemoCase& case_by_id(const std::string& id) const;
    const Fluid25DTerrainDemoStatus& status() const {
        return status_;
    }
    const std::string& initial_case_id() const {
        return initial_case_id_;
    }
    const std::filesystem::path& directory() const {
        return directory_;
    }
    std::filesystem::path ready_session_directory() const;
    bool busy() const {
        return pending_request_.has_value();
    }
    bool poll();
    void request(const std::string& case_id, double rain_mm_per_hour, double pacing);
    void confirm_installed(bool success, const std::string& error = {});
    void event(const std::string& name, const std::string& case_id, const std::string& session_id,
               double physical_s, double rain_mm_per_hour, std::uint32_t width,
               std::uint32_t height, const std::string& bed_sha256) const;

  private:
    std::filesystem::path directory_;
    std::string launch_id_, initial_case_id_;
    std::vector<Fluid25DTerrainDemoCase> cases_;
    Fluid25DTerrainDemoStatus status_;
    std::optional<std::uint64_t> pending_request_;
    std::uint64_t next_request_id_ = 1;
    int lock_ = -1;
};

struct Fluid25DTerrainDemoAction {
    double after_s = 0;
    std::optional<double> at_physical_s;
    std::string kind, case_id, camera;
    double rain_mm_per_hour = 512, pacing = 60;
    std::optional<double> value;
};

std::vector<Fluid25DTerrainDemoAction>
fluid_25d_load_terrain_demo_script(const std::filesystem::path& path);

bool fluid_25d_demo_action_due(const Fluid25DTerrainDemoAction& action, double elapsed_wall_s,
                               double physical_s);

// One measured mountain narrative, not a general hydrology or autoplay policy.
std::vector<Fluid25DTerrainDemoAction> fluid_25d_rain_guide_actions();
const char* fluid_25d_rain_guide_caption(double physical_s, bool raining);
bool fluid_25d_rain_guide_site(const Fluid25DTerrainDemoCase& site);
bool fluid_25d_demo_guided_start(bool preferred, const Fluid25DTerrainDemoCase& site,
                                 double physical_s, double water_volume_m3);

std::string fluid_25d_demo_time_label(double physical_s);
bool fluid_25d_demo_restart_allowed(bool restart_pending, bool command_pending,
                                    bool worker_available);

class Fluid25DDemoPaceMeter {
  public:
    void sample(double wall_s, double physical_s, bool running);
    std::optional<double> achieved() const {
        return achieved_;
    }

  private:
    std::vector<std::pair<double, double>> samples_;
    std::optional<double> achieved_;
};

} // namespace cubey::projects::fluid::fluid_25d
