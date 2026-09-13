#pragma once

#include <cubey/asset/gltf_asset.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

struct cgltf_image;
struct cgltf_data;
struct cgltf_accessor;

namespace cubey::asset::gltf_internal {

class ScopedLoadProfilePhase {
  public:
    ScopedLoadProfilePhase(GltfAssetLoadProfile* profile, double GltfAssetLoadProfile::* field)
        : profile_(profile), field_(field), started_(Clock::now()) {}

    ~ScopedLoadProfilePhase() {
        if (profile_ != nullptr) {
            profile_->*field_ +=
                std::chrono::duration<double, std::milli>(Clock::now() - started_).count();
        }
    }

    ScopedLoadProfilePhase(const ScopedLoadProfilePhase&) = delete;
    ScopedLoadProfilePhase& operator=(const ScopedLoadProfilePhase&) = delete;

  private:
    using Clock = std::chrono::steady_clock;

    GltfAssetLoadProfile* profile_ = nullptr;
    double GltfAssetLoadProfile::* field_ = nullptr;
    Clock::time_point started_{};
};

[[nodiscard]] std::runtime_error gltf_error(const std::string& message);
[[nodiscard]] std::string label_or_empty(const char* label);
[[nodiscard]] bool starts_with(std::string_view value, std::string_view prefix) noexcept;
[[nodiscard]] std::string percent_decode(std::string_view uri);
[[nodiscard]] std::vector<std::uint8_t> decode_data_uri(std::string_view uri);
[[nodiscard]] GltfImage decode_image(const cgltf_image& source,
                                     const std::filesystem::path& source_path,
                                     GltfAssetLoadProfile* profile);
[[nodiscard]] std::vector<float> read_float_accessor_values(const cgltf_accessor* accessor,
                                                            std::size_t component_count,
                                                            const char* label);
void assemble_gltf_material_data(GltfAsset& asset, const cgltf_data& data,
                                 const std::filesystem::path& source_path,
                                 GltfAssetLoadProfile* profile);
void assemble_gltf_geometry_data(GltfAsset& asset, const cgltf_data& data,
                                 const GltfLoadConfig& config);

} // namespace cubey::asset::gltf_internal
