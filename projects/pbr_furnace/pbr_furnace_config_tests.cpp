#include "pbr_furnace_config.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Action> void require_throws(Action&& action, const char* message) {
    try {
        action();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

void test_conformance_case_config() {
    const char* default_arguments[] = {"pbr_furnace"};
    const auto defaults = cubey::projects::pbr_furnace::parse_pbr_furnace_config(
        1, const_cast<char**>(default_arguments));
    require(defaults.conformance_case == "none",
            "PBR furnace should preserve the white-furnace default layout");

    const char* named_arguments[] = {"pbr_furnace", "--conformance-case", "specular"};
    const auto named = cubey::projects::pbr_furnace::parse_pbr_furnace_config(
        3, const_cast<char**>(named_arguments));
    require(named.conformance_case == "specular",
            "PBR furnace should select its opt-in specular conformance layout");

    const char* clearcoat_arguments[] = {"pbr_furnace", "--conformance-case", "clearcoat"};
    const auto clearcoat = cubey::projects::pbr_furnace::parse_pbr_furnace_config(
        3, const_cast<char**>(clearcoat_arguments));
    require(clearcoat.conformance_case == "clearcoat",
            "PBR furnace should select its opt-in clearcoat conformance layout");

    const char* anisotropy_arguments[] = {"pbr_furnace", "--conformance-case", "anisotropy"};
    const auto anisotropy = cubey::projects::pbr_furnace::parse_pbr_furnace_config(
        3, const_cast<char**>(anisotropy_arguments));
    require(anisotropy.conformance_case == "anisotropy",
            "PBR furnace should select its opt-in anisotropy conformance layout");

    const char* iridescence_arguments[] = {"pbr_furnace", "--conformance-case", "iridescence"};
    const auto iridescence = cubey::projects::pbr_furnace::parse_pbr_furnace_config(
        3, const_cast<char**>(iridescence_arguments));
    require(iridescence.conformance_case == "iridescence",
            "PBR furnace should select its opt-in iridescence conformance layout");

    cubey::projects::pbr_furnace::PbrFurnaceConfig deferred;
    const auto schema = cubey::projects::pbr_furnace::pbr_furnace_config_schema(deferred);
    schema.set("conformance_case", "ior");
    require(deferred.conformance_case == "ior",
            "PBR furnace conformance case should bind through config v2 paths");
    require_throws([&] { schema.set("conformance_case", "unknown"); },
                   "PBR furnace should reject unknown conformance case values");
}

} // namespace

int main() {
    try {
        test_conformance_case_config();
    } catch (const std::exception& error) {
        std::cerr << "pbr_furnace_config_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
