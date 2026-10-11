#include "fluid_25d_terrain_demo.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
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
constexpr std::size_t kMaximumJson = 64U * 1024U;

[[noreturn]] void invalid(const std::string& message) {
    throw std::runtime_error("terrain demo: " + message);
}

Json read_json(const std::filesystem::path& path) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)) ||
        !std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > kMaximumJson)
        invalid("document is missing, unsafe or oversized");
    std::ifstream file(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (!file || text.size() > kMaximumJson)
        invalid("document read failed");
    std::vector<std::set<std::string>> keys;
    bool duplicate = false;
    auto result = Json::parse(text, [&](int depth, Json::parse_event_t event, Json& item) {
        if (depth > 8)
            invalid("document nesting exceeds limit");
        if (event == Json::parse_event_t::object_start)
            keys.emplace_back();
        else if (event == Json::parse_event_t::key) {
            if (keys.empty() || !keys.back().insert(item.get<std::string>()).second)
                duplicate = true;
        } else if (event == Json::parse_event_t::object_end)
            keys.pop_back();
        return true;
    });
    if (duplicate || !result.is_object())
        invalid("document must be an object with unique keys");
    return result;
}

std::string string(const Json& value, const char* name, std::size_t maximum = 128) {
    if (!value.at(name).is_string())
        invalid(std::string(name) + " must be a string");
    auto result = value.at(name).get<std::string>();
    if (result.size() > maximum || result.find('\0') != std::string::npos)
        invalid(std::string(name) + " is too long or contains NUL");
    return result;
}

double number(const Json& value, const char* name, double minimum, double maximum) {
    if (!value.at(name).is_number())
        invalid(std::string(name) + " must be numeric");
    const double result = value.at(name).get<double>();
    if (!std::isfinite(result) || result < minimum || result > maximum)
        invalid(std::string(name) + " is outside its supported range");
    return result;
}

bool hash(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

void keys(const Json& value, std::initializer_list<const char*> expected) {
    std::set<std::string> actual, wanted;
    for (auto item = value.begin(); item != value.end(); ++item)
        actual.insert(item.key());
    for (auto key : expected)
        wanted.insert(key);
    if (actual != wanted)
        invalid("unknown/missing document fields");
}

void atomic_json(const std::filesystem::path& path, const Json& document) {
    auto temporary = path;
    temporary.replace_extension(".tmp");
    const auto text = document.dump();
    if (text.size() > kMaximumJson || std::filesystem::exists(temporary) ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        invalid("unsafe publication path");
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.close();
    if (!file)
        invalid("publication failed");
    std::filesystem::rename(temporary, path);
}
} // namespace

Fluid25DTerrainDemoControl::Fluid25DTerrainDemoControl(const std::filesystem::path& directory)
    : directory_(std::filesystem::canonical(directory)) {
    const auto catalogue = read_json(directory_ / "catalog.json");
    keys(catalogue, {"schema", "launch_id", "initial_case", "cases"});
    if (catalogue.at("schema") != kFluid25DTerrainDemoSchema)
        invalid("unsupported catalogue schema");
    launch_id_ = string(catalogue, "launch_id");
    initial_case_id_ = string(catalogue, "initial_case");
    if (launch_id_.empty() || !catalogue.at("cases").is_array() || catalogue.at("cases").empty() ||
        catalogue.at("cases").size() > 32)
        invalid("invalid catalogue identity/count");
    std::set<std::string> ids;
    for (const auto& item : catalogue.at("cases")) {
        keys(item, {"id",
                    "site",
                    "site_label",
                    "configuration",
                    "label",
                    "extent",
                    "width",
                    "height",
                    "spacing_m",
                    "source_spacing_m",
                    "case_dir",
                    "surface_grid",
                    "available",
                    "error",
                    "input_sha256",
                    "solver_bed_sha256",
                    "protocol_sha256",
                    "climate_manifest_sha256",
                    "duration_s",
                    "rain_mm_per_hour"});
        Fluid25DTerrainDemoCase entry;
        entry.id = string(item, "id");
        entry.site = string(item, "site");
        entry.site_label = string(item, "site_label");
        entry.configuration = string(item, "configuration");
        entry.label = string(item, "label");
        entry.extent = string(item, "extent");
        if (entry.id.empty() || !ids.insert(entry.id).second ||
            entry.id != entry.site + "-" + entry.configuration || entry.site.empty() ||
            entry.site.size() > 64 || entry.site.front() == '-' || entry.site.back() == '-' ||
            entry.site.find("--") != std::string::npos ||
            entry.site.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-") !=
                std::string::npos ||
            (entry.extent != "same" && entry.extent != "small" && entry.extent != "large"))
            invalid("unknown/duplicate prepared case");
        if (!item.at("width").is_number_unsigned() || !item.at("height").is_number_unsigned())
            invalid("grid dimensions must be unsigned integers");
        entry.width = static_cast<std::uint32_t>(number(item, "width", 2, 2048));
        entry.height = static_cast<std::uint32_t>(number(item, "height", 2, 2048));
        entry.spacing_m = number(item, "spacing_m", 1, 1024);
        entry.source_spacing_m = number(item, "source_spacing_m", 1, 1024);
        entry.duration_s = number(item, "duration_s", 1, 1e9);
        entry.rain_mm_per_hour = number(item, "rain_mm_per_hour", 0, 36000);
        entry.case_directory = string(item, "case_dir", 4096);
        entry.surface_grid = string(item, "surface_grid", 4096);
        if (!entry.case_directory.is_absolute() || entry.case_directory.filename() != entry.id ||
            entry.surface_grid.parent_path() != entry.case_directory ||
            entry.surface_grid.filename() != "surface-grid.json")
            invalid("case paths do not match the prepared identity");
        entry.available = item.at("available").get<bool>();
        entry.error = string(item, "error", 512);
        entry.input_sha256 = string(item, "input_sha256");
        entry.solver_bed_sha256 = string(item, "solver_bed_sha256");
        if (entry.available && (!hash(entry.input_sha256) || !hash(entry.solver_bed_sha256) ||
                                !hash(string(item, "protocol_sha256")) ||
                                !hash(string(item, "climate_manifest_sha256"))))
            invalid("available case lacks valid pinned identities");
        cases_.push_back(std::move(entry));
    }
    if (!case_by_id(initial_case_id_).available)
        invalid("initial case is unavailable");
#ifndef _WIN32
    lock_ = ::open((directory_ / "control.lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
                   0600);
    if (lock_ < 0 || ::flock(lock_, LOCK_EX | LOCK_NB) != 0) {
        if (lock_ >= 0)
            ::close(lock_);
        lock_ = -1;
        invalid("another controller owns this managed demo");
    }
#else
    invalid("managed demo controls require POSIX locking");
#endif
    try {
        static_cast<void>(poll());
        next_request_id_ = status_.request_id + 1;
        if (status_.phase != "active" || status_.case_id != initial_case_id_)
            invalid("initial managed demo is not active");
    } catch (...) {
#ifndef _WIN32
        ::close(lock_);
        lock_ = -1;
#endif
        throw;
    }
}

Fluid25DTerrainDemoControl::~Fluid25DTerrainDemoControl() {
#ifndef _WIN32
    if (lock_ >= 0)
        ::close(lock_);
#endif
}

const Fluid25DTerrainDemoCase& Fluid25DTerrainDemoControl::case_by_id(const std::string& id) const {
    const auto found =
        std::find_if(cases_.begin(), cases_.end(), [&](const auto& item) { return item.id == id; });
    if (found == cases_.end())
        invalid("case is not in the prepared catalogue");
    return *found;
}

bool Fluid25DTerrainDemoControl::poll() {
    const auto document = read_json(directory_ / "status.json");
    keys(document, {"schema", "launch_id", "request_id", "phase", "case_id", "session_name",
                    "session_id", "error", "published_unix_s", "worker_available"});
    if (document.at("schema") != kFluid25DTerrainDemoSchema ||
        string(document, "launch_id") != launch_id_ ||
        !document.at("request_id").is_number_unsigned())
        invalid("status belongs to a different launch");
    Fluid25DTerrainDemoStatus next;
    next.request_id = document.at("request_id").get<std::uint64_t>();
    if (next.request_id == std::numeric_limits<std::uint64_t>::max() ||
        next.request_id < status_.request_id)
        invalid("status request ID reversed/exhausted");
    next.phase = string(document, "phase");
    const std::array<std::string_view, 9> phases{"active",   "validating", "stopping",
                                                 "starting", "ready",      "completed",
                                                 "stopped",  "error",      "closed"};
    if (next.phase.empty() || std::find(phases.begin(), phases.end(), next.phase) == phases.end())
        invalid("unknown switch phase");
    next.case_id = string(document, "case_id");
    static_cast<void>(case_by_id(next.case_id));
    next.session_name = string(document, "session_name");
    if (!next.session_name.empty() &&
        (next.session_name.size() != 31 || !next.session_name.starts_with("generation-") ||
         !std::all_of(next.session_name.begin() + 11, next.session_name.end(),
                      [](char c) { return c >= '0' && c <= '9'; })))
        invalid("unsafe session basename");
    next.session_id = string(document, "session_id");
    next.error = string(document, "error", 512);
    next.published_unix_s = number(document, "published_unix_s", 0, 1e12);
    next.worker_available = document.at("worker_available").get<bool>();
    if ((next.phase == "active" || next.phase == "ready" || next.phase == "completed" ||
         next.phase == "stopped") &&
        (next.session_name.empty() || next.session_id.empty()))
        invalid("ready/active status lacks session identity");
    if (pending_request_ && next.request_id > *pending_request_)
        invalid("unexpected competing switch request");
    const bool changed = next.phase != status_.phase || next.request_id != status_.request_id ||
                         next.error != status_.error;
    status_ = std::move(next);
    if (pending_request_ && status_.request_id == *pending_request_ &&
        (status_.phase == "active" || status_.phase == "error" || status_.phase == "closed"))
        pending_request_.reset();
    return changed;
}

void Fluid25DTerrainDemoControl::request(const std::string& case_id, double rain, double pacing) {
    if (busy() || !case_by_id(case_id).available || !std::isfinite(rain) || rain < 0 ||
        rain > 36000 || !std::isfinite(pacing) || pacing < .125 || pacing > 300 ||
        next_request_id_ == std::numeric_limits<std::uint64_t>::max())
        invalid("switch is unavailable or settings are invalid");
    atomic_json(directory_ / "request.json", {{"schema", kFluid25DTerrainDemoSchema},
                                              {"launch_id", launch_id_},
                                              {"request_id", next_request_id_},
                                              {"case_id", case_id},
                                              {"rain_mm_per_hour", rain},
                                              {"pacing", pacing}});
    pending_request_ = next_request_id_++;
}

std::filesystem::path Fluid25DTerrainDemoControl::ready_session_directory() const {
    if (status_.phase != "ready")
        invalid("replacement session is not ready");
    const auto path = directory_.parent_path() / "sessions" / status_.session_name;
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        invalid("replacement session is a symlink");
    return path;
}

void Fluid25DTerrainDemoControl::confirm_installed(bool success, const std::string& error) {
    if (!pending_request_ || status_.phase != "ready" || status_.request_id != *pending_request_)
        invalid("no matching replacement to confirm");
    atomic_json(directory_ / "installed.json", {{"schema", kFluid25DTerrainDemoSchema},
                                                {"launch_id", launch_id_},
                                                {"request_id", status_.request_id},
                                                {"session_id", status_.session_id},
                                                {"success", success},
                                                {"error", error.substr(0, 512)}});
}

void Fluid25DTerrainDemoControl::event(const std::string& name, const std::string& case_id,
                                       const std::string& session_id, double physical_s,
                                       double rain, std::uint32_t width, std::uint32_t height,
                                       const std::string& bed_sha256) const {
    const auto path = directory_ / "events.jsonl";
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(path)))
        invalid("unsafe event log path");
    std::ofstream file(path, std::ios::app);
    file << Json{{"event", name},
                 {"request_id", status_.request_id},
                 {"case_id", case_id},
                 {"session_id", session_id},
                 {"physical_s", physical_s},
                 {"rain_mm_per_hour", rain},
                 {"width", width},
                 {"height", height},
                 {"solver_bed_sha256", bed_sha256},
                 {"hydraulic_dispatches", 0}}
                .dump()
         << '\n';
    if (!file)
        invalid("event log write failed");
}

std::vector<Fluid25DTerrainDemoAction>
fluid_25d_load_terrain_demo_script(const std::filesystem::path& path) {
    const auto document = read_json(path);
    keys(document, {"schema", "actions"});
    if (document.at("schema") != kFluid25DTerrainDemoSchema || !document.at("actions").is_array() ||
        document.at("actions").size() > 64)
        invalid("invalid regression script");
    std::vector<Fluid25DTerrainDemoAction> actions;
    for (const auto& item : document.at("actions")) {
        Fluid25DTerrainDemoAction action;
        action.kind = string(item, "kind");
        action.after_s = number(item, "after_s", 0, 3600);
        if (action.kind == "switch") {
            keys(item, {"kind", "after_s", "case_id", "rain_mm_per_hour", "pacing"});
            action.case_id = string(item, "case_id");
            action.rain_mm_per_hour = number(item, "rain_mm_per_hour", 0, 36000);
            action.pacing = number(item, "pacing", .125, 300);
        } else if (action.kind == "set-rain" || action.kind == "set-time-scale") {
            keys(item, {"kind", "after_s", "value"});
            action.value = number(item, "value", action.kind == "set-rain" ? 0 : .125,
                                  action.kind == "set-rain" ? 36000 : 300);
        } else if (action.kind == "resume" || action.kind == "pause" || action.kind == "reset") {
            keys(item, {"kind", "after_s"});
        } else
            invalid("unsupported script action");
        actions.push_back(std::move(action));
    }
    return actions;
}

std::string fluid_25d_demo_time_label(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 1e9)
        return "unknown";
    const auto whole = static_cast<std::uint64_t>(seconds);
    return std::to_string(whole / 3600) + "h " + std::to_string((whole / 60) % 60) + "m " +
           std::to_string(whole % 60) + "s";
}

bool fluid_25d_demo_restart_allowed(bool restart_pending, bool command_pending,
                                    bool worker_available) {
    // A fresh identity can abandon an unresolved command only after the owning
    // launcher confirms the previous producer has exited. Never race a live one.
    return !restart_pending && (!command_pending || !worker_available);
}

void Fluid25DDemoPaceMeter::sample(double wall, double physical, bool running) {
    if (!running || !std::isfinite(wall) || !std::isfinite(physical) ||
        (!samples_.empty() &&
         (wall < samples_.back().first || physical < samples_.back().second))) {
        samples_.clear();
        achieved_.reset();
        return;
    }
    // Keep a time window even at uncapped rendering rates; per-frame storage
    // would otherwise exhaust the bound before one second could be measured.
    if (!samples_.empty() && wall - samples_.back().first < .05)
        return;
    samples_.emplace_back(wall, physical);
    while (samples_.size() > 2 && (wall - samples_[1].first > 3 || samples_.size() > 512))
        samples_.erase(samples_.begin());
    const double elapsed = wall - samples_.front().first;
    if (elapsed >= 1)
        achieved_ = (physical - samples_.front().second) / elapsed;
}
} // namespace cubey::projects::fluid::fluid_25d
