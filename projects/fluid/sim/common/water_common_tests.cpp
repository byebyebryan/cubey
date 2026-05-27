#include "water_common.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    namespace water = cubey::projects::fluid::common;

    require(water::kWaterMaxExactShaderInteger == (1U << 24U),
            "water shader integer cap should stay in the exact float range");
    require(water::water_transfer_mode_from_name("") == water::WaterTransferMode::Apic,
            "empty transfer mode should default to APIC");
    require(water::water_transfer_mode_from_name("pic-flip") == water::WaterTransferMode::PicFlip,
            "transfer mode parser should accept pic-flip");
    require(water::water_transfer_mode_from_name("pic/flip") == water::WaterTransferMode::PicFlip,
            "transfer mode parser should accept pic/flip");
    require(std::string(water::water_transfer_mode_name(water::WaterTransferMode::Apic)) == "APIC",
            "transfer mode name should include APIC");

    bool rejected_transfer_mode = false;
    try {
        static_cast<void>(water::water_transfer_mode_from_name("sph"));
    } catch (const std::runtime_error&) {
        rejected_transfer_mode = true;
    }
    require(rejected_transfer_mode, "transfer mode parser should reject unknown names");

    require(water::checked_mul(std::size_t{7}, std::size_t{9}, "test") == 63U,
            "checked multiply should preserve normal products");
    bool rejected_mul = false;
    try {
        static_cast<void>(
            water::checked_mul(std::numeric_limits<std::size_t>::max(), std::size_t{2},
                               "overflow"));
    } catch (const std::runtime_error&) {
        rejected_mul = true;
    }
    require(rejected_mul, "checked multiply should reject overflow");

    require(water::checked_add(std::size_t{7}, std::size_t{9}, "test") == 16U,
            "checked add should preserve normal sums");
    bool rejected_add = false;
    try {
        static_cast<void>(water::checked_add(std::numeric_limits<std::size_t>::max(),
                                             std::size_t{1}, "overflow"));
    } catch (const std::runtime_error&) {
        rejected_add = true;
    }
    require(rejected_add, "checked add should reject overflow");

    require(water::water_shader_count_float(water::kWaterMaxExactShaderInteger, "test") ==
                static_cast<float>(water::kWaterMaxExactShaderInteger),
            "shader count helper should accept the exact integer cap");
    bool rejected_shader_count = false;
    try {
        static_cast<void>(water::water_shader_count_float(
            static_cast<std::size_t>(water::kWaterMaxExactShaderInteger) + 1U, "overflow"));
    } catch (const std::runtime_error&) {
        rejected_shader_count = true;
    }
    require(rejected_shader_count, "shader count helper should reject inexact float integers");

    require(water::water_fill_axis_cell_count(256U, 2U, 0.08F, 0.92F, 0.50F) == 128U,
            "fill axis helper should size the default half-width slab");
    require(water::water_fill_axis_cell_count(16U, 2U, 0.08F, 0.92F, 1.0F) == 12U,
            "fill axis helper should clamp to usable interior cells");
    require(water::water_runtime_particle_scan_count(100U, 200U, 0U) == 100U,
            "runtime scan count should default to active particles");
    require(water::water_runtime_particle_scan_count(100U, 200U, 150U) == 150U,
            "runtime scan count should keep touched particles");
    require(water::water_runtime_particle_scan_count(100U, 200U, 250U) == 200U,
            "runtime scan count should clamp to capacity");

    return 0;
}
