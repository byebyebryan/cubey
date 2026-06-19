#include <cubey/procedural/artifact_metadata.h>
#include <cubey/procedural/field_2d.h>
#include <cubey/procedural/field_metadata.h>
#include <cubey/procedural/field_set_2d.h>
#include <cubey/procedural/hash.h>
#include <cubey/procedural/noise.h>
#include <cubey/procedural/operators.h>
#include <cubey/procedural/patch_domain.h>
#include <cubey/procedural/sample_domain.h>
#include <cubey/procedural/seed.h>
#include <cubey/procedural/source_fields.h>

#include "source_file_test_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_near(float actual, float expected, float tolerance, const char* message) {
    if (std::fabs(actual - expected) > tolerance) {
        throw std::runtime_error(message);
    }
}

void require_finite_unit(float value, const char* message) {
    require(std::isfinite(value), message);
    require(value >= -1.0001F && value <= 1.0001F, message);
}

template <typename Fn> void require_throws(Fn&& fn, const char* message) {
    try {
        fn();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

cubey::procedural::NoiseSource2D unit_legacy_source(std::uint64_t seed) {
    return cubey::procedural::NoiseSource2D{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Unit,
        .seed = seed,
        .legacy_fbm = {.octaves = 4},
        .domain = {.x_scale = 0.4F, .y_scale = 0.7F, .x_offset = 1.5F, .y_offset = -0.25F},
    };
}

} // namespace

void test_procedural_scalar_field_indexes_centered_samples() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 5,
        .height = 3,
        .cell_size = 2.0F,
        .origin_x = 10.0F,
        .origin_y = -4.0F,
    };
    cubey::procedural::ScalarField2D field(desc, 1.0F);

    require(field.sample_count() == 15U, "scalar field should allocate width * height samples");
    require(field.index(2U, 1U) == 7U, "scalar field should use row-major indexing");
    require_near(cubey::procedural::grid_sample_x(desc, 0U), 6.0F, 0.0001F,
                 "grid x samples should be centered around origin");
    require_near(cubey::procedural::grid_sample_x(desc, 4U), 14.0F, 0.0001F,
                 "grid x samples should be centered around origin");
    require_near(cubey::procedural::grid_sample_y(desc, 0U), -6.0F, 0.0001F,
                 "grid y samples should be centered around origin");
    require_near(cubey::procedural::grid_sample_y(desc, 2U), -2.0F, 0.0001F,
                 "grid y samples should be centered around origin");

    field.at(2U, 1U) = 5.0F;
    require_near(field.at(2U, 1U), 5.0F, 0.0001F,
                 "scalar field should expose writable indexed samples");
    require_throws([&field] { (void)field.at(5U, 0U); },
                   "scalar field should reject out-of-range samples");
}

void test_procedural_sample_domains_wrap_2d_grids() {
    const cubey::procedural::SampleDomain2D domain{
        .grid =
            {
                .width = 4,
                .height = 3,
                .cell_size = 2.5F,
                .origin_x = 10.0F,
                .origin_y = -20.0F,
            },
        .seed = 1234U,
        .space = cubey::procedural::ProceduralDomainSpace::Atlas,
    };

    require(cubey::procedural::sample_count(domain) == 12U,
            "2D sample domains should delegate sample count to the grid descriptor");
    require_near(cubey::procedural::grid_sample_x(domain.grid, 0U), 6.25F, 0.0001F,
                 "2D sample domains should keep centered grid x samples");
    require_near(cubey::procedural::grid_sample_y(domain.grid, 2U), -17.5F, 0.0001F,
                 "2D sample domains should keep centered grid y samples");
    require(domain.seed == 1234U, "2D sample domains should preserve seed metadata");
    require(domain.space == cubey::procedural::ProceduralDomainSpace::Atlas,
            "2D sample domains should preserve semantic space metadata");

    require_throws(
        [] {
            (void)cubey::procedural::sample_count(cubey::procedural::SampleDomain2D{
                .grid = {.width = 0U, .height = 2U},
            });
        },
        "2D sample domains should reject zero dimensions");
}

void test_procedural_sample_domains_index_3d_samples() {
    const cubey::procedural::SampleDomain3D domain{
        .width = 4,
        .height = 3,
        .depth = 2,
        .cell_size = 2.0F,
        .origin_x = 10.0F,
        .origin_y = -4.0F,
        .origin_z = 100.0F,
        .seed = 9876U,
        .space = cubey::procedural::ProceduralDomainSpace::Volume,
    };

    require(cubey::procedural::sample_count(domain) == 24U,
            "3D sample domains should count width * height * depth samples");
    require(cubey::procedural::sample_index(domain, 2U, 1U, 1U) == 18U,
            "3D sample domains should use z-major layer then row-major indexing");
    require_near(cubey::procedural::sample_domain_x(domain, 0U), 7.0F, 0.0001F,
                 "3D sample domains should center x samples around origin");
    require_near(cubey::procedural::sample_domain_x(domain, 3U), 13.0F, 0.0001F,
                 "3D sample domains should center max x samples around origin");
    require_near(cubey::procedural::sample_domain_y(domain, 0U), -6.0F, 0.0001F,
                 "3D sample domains should center y samples around origin");
    require_near(cubey::procedural::sample_domain_z(domain, 1U), 101.0F, 0.0001F,
                 "3D sample domains should center z samples around origin");
    require(domain.seed == 9876U, "3D sample domains should preserve seed metadata");
    require(domain.space == cubey::procedural::ProceduralDomainSpace::Volume,
            "3D sample domains should preserve semantic space metadata");
}

void test_procedural_sample_domains_reject_invalid_3d_samples() {
    const cubey::procedural::SampleDomain3D domain{.width = 2, .height = 2, .depth = 2};
    require_throws([&] { (void)cubey::procedural::sample_index(domain, 2U, 0U, 0U); },
                   "3D sample domains should reject out-of-range x indexes");
    require_throws([&] { (void)cubey::procedural::sample_domain_y(domain, 2U); },
                   "3D sample domains should reject out-of-range coordinate indexes");
    require_throws(
        [] {
            (void)cubey::procedural::sample_count(cubey::procedural::SampleDomain3D{
                .width = 2U,
                .height = 0U,
                .depth = 2U,
            });
        },
        "3D sample domains should reject zero dimensions");
}

void test_procedural_hash_builder_encodes_stable_values() {
    const std::array<std::uint8_t, 5> cubey_bytes{'c', 'u', 'b', 'e', 'y'};
    require(cubey::procedural::procedural_hash_bytes({}) ==
                cubey::procedural::kProceduralFnv1a64Offset,
            "empty byte hashes should preserve the FNV offset");
    require(cubey::procedural::procedural_hash_bytes(cubey_bytes) == 8786454520568773403ULL,
            "byte hashes should match stable string FNV hashing");
    require(cubey::procedural::procedural_hash_bytes(cubey_bytes) ==
                cubey::procedural::stable_hash_string("cubey"),
            "raw byte hashes should match stable_hash_string for ASCII text");

    cubey::procedural::ProceduralHashBuilder string_hash;
    string_hash.append_string("cubey");
    require(string_hash.value() != cubey::procedural::procedural_hash_bytes(cubey_bytes),
            "builder strings should include a length prefix");

    cubey::procedural::ProceduralHashBuilder first;
    first.append_u32(7U);
    first.append_u64(0x1234'5678'9abc'def0ULL);
    first.append_float32(1.5F);
    first.append_string("domain");

    cubey::procedural::ProceduralHashBuilder repeat;
    repeat.append_u32(7U);
    repeat.append_u64(0x1234'5678'9abc'def0ULL);
    repeat.append_float32(1.5F);
    repeat.append_string("domain");
    require(first.value() == repeat.value(), "hash builders should repeat matching sequences");

    cubey::procedural::ProceduralHashBuilder changed;
    changed.append_u32(8U);
    changed.append_u64(0x1234'5678'9abc'def0ULL);
    changed.append_float32(1.5F);
    changed.append_string("domain");
    require(first.value() != changed.value(), "hash builders should include appended values");
}

void test_procedural_patch_domains_hash_addresses_and_seeds() {
    const cubey::procedural::PatchAddress2D address{.x = 3, .y = -2, .level = 5};
    const std::uint64_t first_hash = cubey::procedural::patch_address_hash(address);
    const std::uint64_t repeat_hash = cubey::procedural::patch_address_hash(address);
    require(first_hash == repeat_hash, "patch address hashes should repeat exactly");
    require(first_hash !=
                cubey::procedural::patch_address_hash({.x = 4, .y = -2, .level = 5}),
            "patch address hashes should include x");
    require(first_hash !=
                cubey::procedural::patch_address_hash({.x = 3, .y = -1, .level = 5}),
            "patch address hashes should include y");
    require(first_hash !=
                cubey::procedural::patch_address_hash({.x = 3, .y = -2, .level = 6}),
            "patch address hashes should include level");

    const std::uint64_t seed =
        cubey::procedural::derive_patch_seed(42U, "terrain.height", address);
    require(seed == cubey::procedural::derive_patch_seed(42U, "terrain.height", address),
            "patch seeds should repeat for matching base seed, domain, and address");
    require(seed != cubey::procedural::derive_patch_seed(43U, "terrain.height", address),
            "patch seeds should include base seed");
    require(seed != cubey::procedural::derive_patch_seed(42U, "terrain.moisture", address),
            "patch seeds should include domain names");
    require(seed !=
                cubey::procedural::derive_patch_seed(
                    42U, "terrain.height", {.x = 3, .y = -2, .level = 6}),
            "patch seeds should include patch address hashes");
}

void test_procedural_patch_domains_expand_bordered_sample_grids() {
    const cubey::procedural::PatchAddress2D address{.x = -7, .y = 11, .level = 2};
    const std::uint64_t seed =
        cubey::procedural::derive_patch_seed(1234U, "terrain.height", address);
    const cubey::procedural::PatchDomain2D domain{
        .address = address,
        .interior_grid =
            {
                .width = 16,
                .height = 8,
                .cell_size = 4.0F,
                .origin_x = 100.0F,
                .origin_y = -40.0F,
            },
        .border_samples = 2,
        .seed = seed,
        .space = cubey::procedural::ProceduralDomainSpace::World,
    };

    cubey::procedural::validate_patch_domain(domain);
    const cubey::procedural::Grid2DDesc sample_grid =
        cubey::procedural::patch_sample_grid(domain);
    require(sample_grid.width == 20U && sample_grid.height == 12U,
            "patch sample grids should expand by border samples on each side");
    require_near(sample_grid.cell_size, 4.0F, 0.0F,
                 "patch sample grids should preserve cell size");
    require_near(sample_grid.origin_x, 100.0F, 0.0F,
                 "patch sample grids should preserve origin x");
    require_near(sample_grid.origin_y, -40.0F, 0.0F,
                 "patch sample grids should preserve origin y");

    const cubey::procedural::SampleDomain2D sample_domain =
        cubey::procedural::patch_sample_domain(domain);
    require(sample_domain.grid.width == sample_grid.width &&
                sample_domain.grid.height == sample_grid.height,
            "patch sample domains should wrap the expanded grid");
    require(sample_domain.seed == seed, "patch sample domains should preserve seeds");
    require(sample_domain.space == cubey::procedural::ProceduralDomainSpace::World,
            "patch sample domains should preserve spaces");
    require(cubey::procedural::sample_count(sample_domain) == 240U,
            "patch sample domains should count the expanded sample grid");

    require_throws(
        [] {
            cubey::procedural::validate_patch_domain(cubey::procedural::PatchDomain2D{
                .interior_grid = {.width = 0U, .height = 8U},
            });
        },
        "patch domains should reject zero interior dimensions");
    require_throws(
        [] {
            (void)cubey::procedural::patch_sample_grid(cubey::procedural::PatchDomain2D{
                .interior_grid =
                    {
                        .width = std::numeric_limits<std::uint32_t>::max(),
                        .height = 1U,
                    },
                .border_samples = 1U,
            });
        },
        "patch domains should reject bordered grid overflow");
}

void test_procedural_artifact_metadata_counts_mipped_samples() {
    const cubey::procedural::ProceduralArtifactExtent extent{
        .width = 8,
        .height = 4,
        .depth = 1,
        .faces = 6,
        .mip_levels = 4,
    };

    require(cubey::procedural::procedural_artifact_mip_dimension(8U, 0U) == 8U,
            "artifact mip dimension should preserve base level");
    require(cubey::procedural::procedural_artifact_mip_dimension(8U, 2U) == 2U,
            "artifact mip dimension should halve each level");
    require(cubey::procedural::procedural_artifact_mip_dimension(1U, 4U) == 1U,
            "artifact mip dimension should clamp at one");
    require(cubey::procedural::procedural_artifact_mip_sample_count(extent, 0U) == 192U,
            "artifact mip sample count should include faces");
    require(cubey::procedural::procedural_artifact_mip_sample_count(extent, 1U) == 48U,
            "artifact mip sample count should shrink dimensions per mip");
    require(cubey::procedural::procedural_artifact_sample_count(extent) == 258U,
            "artifact sample count should include all mip levels");
}

void test_procedural_artifact_metadata_validates_identity_and_layout() {
    const cubey::procedural::ProceduralArtifactMetadata metadata{
        .name = "night sky atlas",
        .generator = "cubey::render::generate_night_sky_atlas",
        .formula_version = "atmosphere-night-sky-atlas-v1",
        .domain = "atmosphere.night_sky_atlas",
        .seed = 12345U,
        .space = cubey::procedural::ProceduralDomainSpace::Spherical,
        .kind = cubey::procedural::ProceduralArtifactKind::TextureCube,
        .format = cubey::procedural::ProceduralArtifactValueFormat::Rgba32Float,
        .extent = {.width = 64, .height = 64, .depth = 1, .faces = 6, .mip_levels = 7},
        .content_hash = 0x1234U,
    };

    cubey::procedural::validate_procedural_artifact_metadata(metadata);
    require(metadata.space == cubey::procedural::ProceduralDomainSpace::Spherical,
            "artifact metadata should preserve semantic domain space");
    require(metadata.kind == cubey::procedural::ProceduralArtifactKind::TextureCube,
            "artifact metadata should preserve artifact kind");
    require(metadata.format == cubey::procedural::ProceduralArtifactValueFormat::Rgba32Float,
            "artifact metadata should preserve value format");

    require_throws(
        [] {
            cubey::procedural::validate_procedural_artifact_metadata(
                cubey::procedural::ProceduralArtifactMetadata{
                    .generator = "generator",
                    .formula_version = "v1",
                    .domain = "domain",
                    .format = cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
                });
        },
        "artifact metadata should reject empty names");
    require_throws(
        [] {
            cubey::procedural::validate_procedural_artifact_metadata(
                cubey::procedural::ProceduralArtifactMetadata{
                    .name = "texture",
                    .generator = "generator",
                    .formula_version = "v1",
                    .domain = "domain",
                    .format = cubey::procedural::ProceduralArtifactValueFormat::Unknown,
                });
        },
        "artifact metadata should reject unknown formats");
    require_throws(
        [] {
            cubey::procedural::validate_procedural_artifact_metadata(
                cubey::procedural::ProceduralArtifactMetadata{
                    .name = "texture",
                    .generator = "generator",
                    .formula_version = "v1",
                    .domain = "domain",
                    .format = cubey::procedural::ProceduralArtifactValueFormat::Rgba8Unorm,
                    .extent = {.width = 1, .height = 1, .depth = 1, .faces = 0, .mip_levels = 1},
                });
        },
        "artifact metadata should reject invalid extents");
    require_throws(
        [] {
            (void)cubey::procedural::procedural_artifact_mip_sample_count(
                {.width = 4, .height = 4, .depth = 1, .faces = 1, .mip_levels = 2}, 2U);
        },
        "artifact metadata should reject out-of-range mip levels");
}

void test_procedural_field_metadata_hashes_scalar_fields() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 2,
        .height = 2,
        .cell_size = 3.0F,
        .origin_x = 10.0F,
        .origin_y = -5.0F,
    };
    cubey::procedural::ScalarField2D field(desc, 0.0F);
    field.at(0U, 0U) = 1.0F;
    field.at(1U, 0U) = 2.0F;
    field.at(0U, 1U) = 3.0F;
    field.at(1U, 1U) = 4.0F;

    const std::uint64_t first_hash = cubey::procedural::scalar_field_content_hash(field);
    const std::uint64_t repeat_hash = cubey::procedural::scalar_field_content_hash(field);
    require(first_hash == repeat_hash, "scalar field hashes should repeat exactly");

    cubey::procedural::ScalarField2D changed_value = field;
    changed_value.at(1U, 1U) = 4.25F;
    require(cubey::procedural::scalar_field_content_hash(changed_value) != first_hash,
            "scalar field hashes should include sample values");

    cubey::procedural::ScalarField2D changed_grid(
        {.width = 2, .height = 2, .cell_size = 3.0F, .origin_x = 11.0F, .origin_y = -5.0F},
        0.0F);
    changed_grid.at(0U, 0U) = 1.0F;
    changed_grid.at(1U, 0U) = 2.0F;
    changed_grid.at(0U, 1U) = 3.0F;
    changed_grid.at(1U, 1U) = 4.0F;
    require(cubey::procedural::scalar_field_content_hash(changed_grid) != first_hash,
            "scalar field hashes should include grid descriptors");

    const cubey::procedural::ProceduralArtifactMetadata metadata =
        cubey::procedural::make_scalar_field_artifact_metadata(
            {
                .name = "height",
                .generator = "cubey::tests::field_metadata",
                .formula_version = "field-metadata-test-v1",
                .domain = "tests.height",
                .seed = 77U,
                .space = cubey::procedural::ProceduralDomainSpace::World,
            },
            field);
    require(metadata.name == "height", "scalar field metadata should preserve names");
    require(metadata.generator == "cubey::tests::field_metadata",
            "scalar field metadata should preserve generators");
    require(metadata.formula_version == "field-metadata-test-v1",
            "scalar field metadata should preserve formula versions");
    require(metadata.domain == "tests.height", "scalar field metadata should preserve domains");
    require(metadata.seed == 77U, "scalar field metadata should preserve seeds");
    require(metadata.space == cubey::procedural::ProceduralDomainSpace::World,
            "scalar field metadata should preserve spaces");
    require(metadata.kind == cubey::procedural::ProceduralArtifactKind::ScalarField2D,
            "scalar field metadata should report scalar field kind");
    require(metadata.format == cubey::procedural::ProceduralArtifactValueFormat::ScalarFloat32,
            "scalar field metadata should report float scalar format");
    require(metadata.extent.width == desc.width && metadata.extent.height == desc.height &&
                metadata.extent.depth == 1U && metadata.extent.faces == 1U &&
                metadata.extent.mip_levels == 1U,
            "scalar field metadata should use 2D field extents");
    require(metadata.content_hash == first_hash,
            "scalar field metadata should include the content hash");
}

void test_procedural_field_metadata_hashes_field_sets_by_name() {
    const cubey::procedural::Grid2DDesc desc{.width = 2, .height = 1, .cell_size = 2.0F};
    cubey::procedural::ScalarField2D height(desc, 0.0F);
    height.at(0U, 0U) = 10.0F;
    height.at(1U, 0U) = 12.0F;
    cubey::procedural::ScalarField2D wetness(desc, 0.0F);
    wetness.at(0U, 0U) = 0.25F;
    wetness.at(1U, 0U) = 0.75F;

    cubey::procedural::FieldSet2D first(desc);
    first.add_field("height", height);
    first.add_field("wetness", wetness);

    cubey::procedural::FieldSet2D reordered(desc);
    reordered.add_field("wetness", wetness);
    reordered.add_field("height", height);

    const std::uint64_t first_hash = cubey::procedural::field_set_content_hash(first);
    require(first_hash == cubey::procedural::field_set_content_hash(reordered),
            "field set hashes should not depend on insertion order");

    cubey::procedural::FieldSet2D renamed(desc);
    renamed.add_field("height", height);
    renamed.add_field("moisture", wetness);
    require(cubey::procedural::field_set_content_hash(renamed) != first_hash,
            "field set hashes should include field names");

    cubey::procedural::ScalarField2D changed_wetness = wetness;
    changed_wetness.at(1U, 0U) = 0.5F;
    cubey::procedural::FieldSet2D changed(desc);
    changed.add_field("height", height);
    changed.add_field("wetness", changed_wetness);
    require(cubey::procedural::field_set_content_hash(changed) != first_hash,
            "field set hashes should include named field content");

    const cubey::procedural::ProceduralArtifactMetadata metadata =
        cubey::procedural::make_field_set_artifact_metadata(
            {
                .name = "terrain debug fields",
                .generator = "cubey::tests::field_set_metadata",
                .formula_version = "field-set-metadata-test-v1",
                .domain = "tests.terrain_fields",
                .seed = 99U,
                .space = cubey::procedural::ProceduralDomainSpace::Local,
            },
            first);
    require(metadata.kind == cubey::procedural::ProceduralArtifactKind::FieldSet2D,
            "field set metadata should report field set kind");
    require(metadata.format == cubey::procedural::ProceduralArtifactValueFormat::ScalarFloat32,
            "field set metadata should report float scalar format");
    require(metadata.extent.width == desc.width && metadata.extent.height == desc.height,
            "field set metadata should use shared field-set extents");
    require(metadata.content_hash == first_hash,
            "field set metadata should include the content hash");
}

void test_procedural_scalar_field_summarizes_and_normalizes() {
    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    field.at(0U, 0U) = -2.0F;
    field.at(1U, 0U) = 0.0F;
    field.at(0U, 1U) = 2.0F;
    field.at(1U, 1U) = 4.0F;

    const cubey::procedural::ScalarFieldStats stats = field.summarize();
    require_near(stats.min, -2.0F, 0.0001F, "field summary should track minimum");
    require_near(stats.max, 4.0F, 0.0001F, "field summary should track maximum");
    require_near(stats.span, 6.0F, 0.0001F, "field summary should track span");
    require_near(stats.mean, 1.0F, 0.0001F, "field summary should track mean");

    cubey::procedural::normalize_to_unit(field);
    require_near(field.at(0U, 0U), 0.0F, 0.0001F, "normalization should map the minimum to zero");
    require_near(field.at(1U, 1U), 1.0F, 0.0001F, "normalization should map the maximum to one");
}

void test_procedural_field_sets_store_named_scalar_fields() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 2,
        .height = 2,
        .cell_size = 4.0F,
        .origin_x = 10.0F,
        .origin_y = -6.0F,
    };
    cubey::procedural::ScalarField2D height(desc, 0.0F);
    height.at(0U, 0U) = 1.0F;
    height.at(1U, 0U) = 3.0F;
    height.at(0U, 1U) = 5.0F;
    height.at(1U, 1U) = 7.0F;
    cubey::procedural::ScalarField2D wetness(desc, 0.25F);

    cubey::procedural::FieldSet2D fields(desc);
    require(fields.empty(), "new field set should start empty");
    fields.add_field("height", height);
    fields.add_field("wetness", wetness);

    require(fields.field_count() == 2U, "field set should track field count");
    require(fields.has_field("height"), "field set should report known names");
    require(fields.try_field("missing") == nullptr, "field set should return null for missing names");
    require_near(fields.field("height").at(1U, 1U), 7.0F, 0.0001F,
                 "field set should expose named field samples");
    fields.field("wetness").at(0U, 0U) = 0.5F;
    require_near(fields.field("wetness").at(0U, 0U), 0.5F, 0.0001F,
                 "field set should expose writable named fields");

    const std::vector<std::string> names = fields.field_names();
    require(names.size() == 2U && names[0] == "height" && names[1] == "wetness",
            "field set should preserve insertion order for names");

    const cubey::procedural::ScalarFieldStats stats = fields.summarize_field("height");
    require_near(stats.min, 1.0F, 0.0001F, "field set summary should report min");
    require_near(stats.max, 7.0F, 0.0001F, "field set summary should report max");
    require_near(stats.mean, 4.0F, 0.0001F, "field set summary should report mean");
}

void test_procedural_field_sets_reject_invalid_fields() {
    const cubey::procedural::Grid2DDesc desc{.width = 2, .height = 2};
    cubey::procedural::FieldSet2D fields(desc);
    cubey::procedural::ScalarField2D field(desc, 0.0F);
    cubey::procedural::ScalarField2D other_size({.width = 3, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D other_origin({.width = 2, .height = 2, .origin_x = 1.0F},
                                                  0.0F);

    require_throws([&] { cubey::procedural::FieldSet2D({.width = 0, .height = 2}); },
                   "field set should reject zero-width descriptors");
    require_throws([&] { fields.add_field("", field); },
                   "field set should reject empty field names");
    fields.add_field("height", field);
    require_throws([&] { fields.add_field("height", field); },
                   "field set should reject duplicate field names");
    require_throws([&] { fields.add_field("other_size", other_size); },
                   "field set should reject mismatched dimensions");
    require_throws([&] { fields.add_field("other_origin", other_origin); },
                   "field set should reject mismatched origins");
    require_throws([&] { (void)fields.field("missing"); },
                   "field set should reject missing required fields");
}

void test_procedural_box_blur_preserves_dimensions_and_smooths_impulse() {
    cubey::procedural::ScalarField2D field({.width = 3, .height = 3}, 0.0F);
    field.at(1U, 1U) = 16.0F;

    const cubey::procedural::ScalarField2D blurred = cubey::procedural::box_blur_3x3(field);
    require(blurred.desc().width == 3U && blurred.desc().height == 3U,
            "box blur should preserve field dimensions");
    require_near(blurred.at(1U, 1U), 4.0F, 0.0001F,
                 "box blur should apply the weighted 3x3 kernel at the center");
    require_near(blurred.at(0U, 0U), 16.0F / 9.0F, 0.0001F,
                 "box blur should renormalize weights at edges");
}

void test_procedural_field_composition_transforms_values() {
    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    field.at(0U, 0U) = -2.0F;
    field.at(1U, 0U) = -0.5F;
    field.at(0U, 1U) = 0.5F;
    field.at(1U, 1U) = 2.0F;

    const cubey::procedural::ScalarField2D clamped =
        cubey::procedural::clamp_field(field, -1.0F, 1.0F);
    require_near(clamped.at(0U, 0U), -1.0F, 0.0001F, "clamp should clamp low values");
    require_near(clamped.at(1U, 1U), 1.0F, 0.0001F, "clamp should clamp high values");

    const cubey::procedural::ScalarField2D remapped =
        cubey::procedural::remap_field(field, -1.0F, 1.0F, 10.0F, 20.0F);
    require_near(remapped.at(0U, 0U), 10.0F, 0.0001F, "remap should clamp below input range");
    require_near(remapped.at(1U, 0U), 12.5F, 0.0001F, "remap should scale in-range values");
    require_near(remapped.at(1U, 1U), 20.0F, 0.0001F, "remap should clamp above input range");

    const cubey::procedural::ScalarField2D stepped =
        cubey::procedural::smoothstep_field(field, -1.0F, 1.0F);
    require_near(stepped.at(1U, 0U), 0.15625F, 0.0001F,
                 "smoothstep field should apply scalar smoothstep");

    const cubey::procedural::ScalarField2D inverted = cubey::procedural::invert_unit_field(stepped);
    require_near(inverted.at(1U, 0U), 0.84375F, 0.0001F,
                 "unit inversion should saturate and invert field samples");

    const cubey::procedural::ScalarField2D ridged =
        cubey::procedural::ridge_profile_field(field, 2.0F);
    require_near(ridged.at(0U, 0U), 0.0F, 0.0001F,
                 "ridge profile should clamp values outside the ridge");
    require_near(ridged.at(0U, 1U), 0.25F, 0.0001F, "ridge profile should shape values near zero");
}

void test_procedural_distribution_summarizes_percentiles() {
    cubey::procedural::ScalarField2D field({.width = 5, .height = 1}, 0.0F);
    field.at(0U, 0U) = 0.0F;
    field.at(1U, 0U) = 10.0F;
    field.at(2U, 0U) = 20.0F;
    field.at(3U, 0U) = 30.0F;
    field.at(4U, 0U) = 40.0F;

    const cubey::procedural::ScalarFieldDistribution distribution =
        cubey::procedural::summarize_scalar_field_distribution(field);
    require_near(distribution.stats.min, 0.0F, 0.0001F,
                 "distribution should include scalar field stats");
    require_near(distribution.stats.max, 40.0F, 0.0001F,
                 "distribution should include scalar field stats");
    require_near(distribution.p01, 0.4F, 0.0001F, "distribution should interpolate p01");
    require_near(distribution.p05, 2.0F, 0.0001F, "distribution should interpolate p05");
    require_near(distribution.p10, 4.0F, 0.0001F, "distribution should interpolate p10");
    require_near(distribution.p25, 10.0F, 0.0001F, "distribution should interpolate p25");
    require_near(distribution.p50, 20.0F, 0.0001F, "distribution should interpolate p50");
    require_near(distribution.p75, 30.0F, 0.0001F, "distribution should interpolate p75");
    require_near(distribution.p90, 36.0F, 0.0001F, "distribution should interpolate p90");
    require_near(distribution.p95, 38.0F, 0.0001F, "distribution should interpolate p95");
    require_near(distribution.p99, 39.6F, 0.0001F, "distribution should interpolate p99");
}

void test_procedural_percentile_remap_shapes_distribution() {
    cubey::procedural::ScalarField2D field({.width = 5, .height = 1}, 0.0F);
    field.at(0U, 0U) = 0.0F;
    field.at(1U, 0U) = 10.0F;
    field.at(2U, 0U) = 20.0F;
    field.at(3U, 0U) = 30.0F;
    field.at(4U, 0U) = 40.0F;

    const cubey::procedural::ScalarField2D remapped =
        cubey::procedural::percentile_remap_field(field, 0.25F, 0.75F, -1.0F, 1.0F);
    require_near(remapped.at(0U, 0U), -1.0F, 0.0001F,
                 "percentile remap should clamp below the low percentile");
    require_near(remapped.at(1U, 0U), -1.0F, 0.0001F,
                 "percentile remap should map low percentile to output min");
    require_near(remapped.at(2U, 0U), 0.0F, 0.0001F,
                 "percentile remap should scale median into the output range");
    require_near(remapped.at(3U, 0U), 1.0F, 0.0001F,
                 "percentile remap should map high percentile to output max");
    require_near(remapped.at(4U, 0U), 1.0F, 0.0001F,
                 "percentile remap should clamp above the high percentile");

    cubey::procedural::ScalarField2D flat({.width = 2, .height = 2}, 3.0F);
    require_throws(
        [&] { (void)cubey::procedural::percentile_remap_field(field, 0.5F, 0.5F, 0.0F, 1.0F); },
        "percentile remap should reject equal percentiles");
    require_throws(
        [&] { (void)cubey::procedural::percentile_remap_field(field, -0.1F, 0.9F, 0.0F, 1.0F); },
        "percentile remap should reject low percentiles below zero");
    require_throws(
        [&] { (void)cubey::procedural::percentile_remap_field(field, 0.1F, 1.1F, 0.0F, 1.0F); },
        "percentile remap should reject high percentiles above one");
    require_throws(
        [&] { (void)cubey::procedural::percentile_remap_field(flat, 0.1F, 0.9F, 0.0F, 1.0F); },
        "percentile remap should reject zero input span");
}

void test_procedural_field_shaping_converts_and_terraces_unit_values() {
    require_near(cubey::procedural::signed_to_unit(-1.0F), 0.0F, 0.0001F,
                 "signed-to-unit should map negative one to zero");
    require_near(cubey::procedural::signed_to_unit(2.0F), 1.0F, 0.0001F,
                 "signed-to-unit should saturate high values");
    require_near(cubey::procedural::unit_to_signed(0.25F), -0.5F, 0.0001F,
                 "unit-to-signed should remap unit values");
    require_near(cubey::procedural::pow_unit(0.25F, 0.5F), 0.5F, 0.0001F,
                 "pow-unit should apply exponent after saturation");
    require_near(cubey::procedural::terrace_unit(0.32F, 4U, 0.0F), 0.0F, 0.0001F,
                 "hard terrace should hold the lower step");
    require_near(cubey::procedural::terrace_unit(0.34F, 4U, 0.0F), 1.0F / 3.0F, 0.0001F,
                 "hard terrace should advance at interval boundaries");
    require_near(cubey::procedural::terrace_unit(0.30F, 4U, 0.25F), 0.216F, 0.001F,
                 "soft terrace should blend near the upper interval edge");

    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    field.at(0U, 0U) = -1.0F;
    field.at(1U, 0U) = 0.0F;
    field.at(0U, 1U) = 0.25F;
    field.at(1U, 1U) = 1.0F;

    const cubey::procedural::ScalarField2D unit = cubey::procedural::signed_to_unit_field(field);
    require_near(unit.at(0U, 0U), 0.0F, 0.0001F,
                 "signed-to-unit field should convert negative samples");
    require_near(unit.at(1U, 0U), 0.5F, 0.0001F,
                 "signed-to-unit field should convert zero samples");

    const cubey::procedural::ScalarField2D signed_field =
        cubey::procedural::unit_to_signed_field(unit);
    require_near(signed_field.at(1U, 0U), 0.0F, 0.0001F,
                 "unit-to-signed field should invert signed-to-unit around zero");

    const cubey::procedural::ScalarField2D powered = cubey::procedural::pow_unit_field(unit, 2.0F);
    require_near(powered.at(1U, 0U), 0.25F, 0.0001F, "pow-unit field should shape each sample");

    const cubey::procedural::ScalarField2D terraced =
        cubey::procedural::terrace_unit_field(unit, 4U, 0.0F);
    require_near(terraced.at(1U, 0U), 1.0F / 3.0F, 0.0001F,
                 "terrace field should quantize each sample");
}

void test_procedural_field_composition_combines_matching_fields() {
    cubey::procedural::ScalarField2D lhs({.width = 2, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D rhs(lhs.desc(), 0.0F);
    cubey::procedural::ScalarField2D mask(lhs.desc(), 0.0F);
    lhs.at(0U, 0U) = 1.0F;
    lhs.at(1U, 0U) = 2.0F;
    lhs.at(0U, 1U) = 3.0F;
    lhs.at(1U, 1U) = 4.0F;
    rhs.at(0U, 0U) = 8.0F;
    rhs.at(1U, 0U) = 6.0F;
    rhs.at(0U, 1U) = 4.0F;
    rhs.at(1U, 1U) = 2.0F;
    mask.at(0U, 0U) = 0.0F;
    mask.at(1U, 0U) = 0.25F;
    mask.at(0U, 1U) = 0.75F;
    mask.at(1U, 1U) = 1.0F;

    require_near(cubey::procedural::add_fields(lhs, rhs).at(0U, 0U), 9.0F, 0.0001F,
                 "add field should add samples");
    require_near(cubey::procedural::subtract_fields(rhs, lhs).at(1U, 0U), 4.0F, 0.0001F,
                 "subtract field should subtract samples");
    require_near(cubey::procedural::multiply_fields(lhs, rhs).at(1U, 1U), 8.0F, 0.0001F,
                 "multiply field should multiply samples");
    require_near(cubey::procedural::min_fields(lhs, rhs).at(0U, 1U), 3.0F, 0.0001F,
                 "min field should select smaller samples");
    require_near(cubey::procedural::max_fields(lhs, rhs).at(0U, 1U), 4.0F, 0.0001F,
                 "max field should select larger samples");

    const cubey::procedural::ScalarField2D blended =
        cubey::procedural::blend_fields(lhs, rhs, mask);
    require(blended.desc().width == lhs.desc().width && blended.desc().height == lhs.desc().height,
            "blend should preserve field dimensions");
    require_near(blended.at(0U, 0U), 1.0F, 0.0001F, "blend mask zero should keep lhs");
    require_near(blended.at(1U, 0U), 3.0F, 0.0001F, "blend mask should interpolate between fields");
    require_near(blended.at(1U, 1U), 2.0F, 0.0001F, "blend mask one should keep rhs");
}

void test_procedural_field_composition_rejects_invalid_inputs() {
    cubey::procedural::ScalarField2D field({.width = 2, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D other_size({.width = 3, .height = 2}, 0.0F);
    cubey::procedural::ScalarField2D other_origin({.width = 2, .height = 2, .origin_x = 1.0F},
                                                  0.0F);

    require_throws([&] { (void)cubey::procedural::add_fields(field, other_size); },
                   "binary field operators should reject mismatched dimensions");
    require_throws([&] { (void)cubey::procedural::blend_fields(field, other_origin, field); },
                   "blend should reject mismatched descriptors");
    require_throws([&] { (void)cubey::procedural::remap_field(field, 1.0F, 1.0F, 0.0F, 1.0F); },
                   "remap should reject zero input range");
    require_throws([&] { (void)cubey::procedural::pow_unit(0.5F, 0.0F); },
                   "pow-unit should reject zero exponent");
    require_throws([&] { (void)cubey::procedural::pow_unit_field(field, -1.0F); },
                   "pow-unit field should reject negative exponents");
    require_throws([&] { (void)cubey::procedural::terrace_unit(0.5F, 1U, 0.0F); },
                   "terrace should reject a single step");
}

void test_procedural_slope_curvature_handles_flat_ramp_and_peak() {
    const cubey::procedural::ScalarField2D flat({.width = 3, .height = 3, .cell_size = 2.0F}, 7.0F);
    const cubey::procedural::SlopeCurvature2D flat_analysis =
        cubey::procedural::compute_slope_curvature(flat);
    require_near(flat_analysis.max_slope, 0.0F, 0.0001F, "flat field should have zero slope");
    require_near(flat_analysis.max_abs_curvature, 0.0F, 0.0001F,
                 "flat field should have zero curvature");

    cubey::procedural::ScalarField2D ramp({.width = 3, .height = 3, .cell_size = 2.0F}, 0.0F);
    for (std::uint32_t y = 0; y < ramp.desc().height; ++y) {
        for (std::uint32_t x = 0; x < ramp.desc().width; ++x) {
            ramp.at(x, y) = static_cast<float>(x) * 4.0F;
        }
    }
    const cubey::procedural::SlopeCurvature2D ramp_analysis =
        cubey::procedural::compute_slope_curvature(ramp);
    require_near(ramp_analysis.slope.at(1U, 1U), 2.0F, 0.0001F,
                 "linear ramp slope should scale by cell size");
    require_near(ramp_analysis.curvature.at(1U, 1U), 0.0F, 0.0001F,
                 "linear ramp center should have zero curvature");

    cubey::procedural::ScalarField2D peak({.width = 3, .height = 3, .cell_size = 1.0F}, 0.0F);
    peak.at(1U, 1U) = 9.0F;
    const cubey::procedural::SlopeCurvature2D peak_analysis =
        cubey::procedural::compute_slope_curvature(peak);
    require_near(peak_analysis.slope.at(1U, 1U), 0.0F, 0.0001F,
                 "symmetric peak center should have zero centered slope");
    require_near(peak_analysis.curvature.at(1U, 1U), -9.0F, 0.0001F,
                 "peak center should be negative convex curvature");
    require_near(peak_analysis.max_abs_curvature, 9.0F, 0.0001F,
                 "peak curvature should drive max absolute curvature");
}

void test_procedural_local_relief_tracks_neighborhood_windows() {
    cubey::procedural::ScalarField2D field({.width = 3, .height = 3, .cell_size = 1.0F}, 0.0F);
    float value = 1.0F;
    for (std::uint32_t y = 0; y < field.desc().height; ++y) {
        for (std::uint32_t x = 0; x < field.desc().width; ++x) {
            field.at(x, y) = value;
            value += 1.0F;
        }
    }

    const cubey::procedural::LocalRelief2D relief =
        cubey::procedural::compute_local_relief(field, 1U);
    require_near(relief.local_min.at(1U, 1U), 1.0F, 0.0001F,
                 "center relief window should see the field minimum");
    require_near(relief.local_max.at(1U, 1U), 9.0F, 0.0001F,
                 "center relief window should see the field maximum");
    require_near(relief.local_mean.at(1U, 1U), 5.0F, 0.0001F,
                 "center relief window should average the full 3x3 region");
    require_near(relief.local_span.at(1U, 1U), 8.0F, 0.0001F,
                 "center relief window should expose local span");
    require_near(relief.local_min.at(0U, 0U), 1.0F, 0.0001F,
                 "corner relief window should clamp to valid samples");
    require_near(relief.local_max.at(0U, 0U), 5.0F, 0.0001F,
                 "corner relief window should clamp to valid samples");
    require_near(relief.local_mean.at(0U, 0U), 3.0F, 0.0001F,
                 "corner relief window should average only valid samples");
    require_near(relief.local_span.at(0U, 0U), 4.0F, 0.0001F,
                 "corner relief window should expose clamped local span");

    const cubey::procedural::LocalRelief2D radius_zero =
        cubey::procedural::compute_local_relief(field, 0U);
    require_near(radius_zero.local_mean.at(2U, 2U), 9.0F, 0.0001F,
                 "zero-radius relief should preserve the source sample");
    require_near(radius_zero.local_span.at(2U, 2U), 0.0F, 0.0001F,
                 "zero-radius relief should have zero span");
}

void test_procedural_operators_include_smootherstep() {
    require_near(cubey::procedural::smootherstep01(-1.0F), 0.0F, 0.0001F,
                 "smootherstep should saturate below zero");
    require_near(cubey::procedural::smootherstep01(0.5F), 0.5F, 0.0001F,
                 "smootherstep midpoint should stay centered");
    require_near(cubey::procedural::smootherstep01(2.0F), 1.0F, 0.0001F,
                 "smootherstep should saturate above one");
}

void test_procedural_seed_domains_are_stable() {
    require(cubey::procedural::stable_hash_string("") == 14695981039346656037ULL,
            "stable string hash should keep the FNV offset basis for empty strings");
    require(cubey::procedural::stable_hash_string("cubey") == 8786454520568773403ULL,
            "stable string hash should keep golden values for project strings");
    require(cubey::procedural::stable_hash_string("cloud.base-density") ==
                12429434235878212328ULL,
            "stable string hash should keep golden values for named procedural domains");
    require(cubey::procedural::stable_hash_string("atmosphere.night-sky") ==
                17705195627440025645ULL,
            "stable string hash should keep golden values for atlas domains");

    const std::uint64_t first = cubey::procedural::derive_seed(42U, "cloud.base-density");
    const std::uint64_t second = cubey::procedural::derive_seed(42U, "cloud.base-density");
    const std::uint64_t changed_domain = cubey::procedural::derive_seed(42U, "cloud.detail");
    const std::uint64_t changed_seed = cubey::procedural::derive_seed(43U, "cloud.base-density");
    require(first == second, "derived seeds should repeat for the same base seed and domain");
    require(first != changed_domain, "derived seeds should separate named domains");
    require(first != changed_seed, "derived seeds should include the base seed");

    const std::uint64_t salted = cubey::procedural::derive_seed(42U, "cloud.base-density", 7U);
    const std::uint64_t salted_repeat =
        cubey::procedural::derive_seed(42U, "cloud.base-density", 7U);
    const std::uint64_t changed_salt =
        cubey::procedural::derive_seed(42U, "cloud.base-density", 8U);
    require(salted == salted_repeat, "salted derived seeds should repeat");
    require(salted != first, "salted derived seeds should differ from unsalted domains");
    require(salted != changed_salt, "salted derived seeds should include the salt");
}

void test_procedural_seed_domain_random_is_stable_and_bounded() {
    const float first = cubey::procedural::random01(77U, "fluid.emitter", 12U, 2U);
    const float second = cubey::procedural::random01(77U, "fluid.emitter", 12U, 2U);
    const float changed_index = cubey::procedural::random01(77U, "fluid.emitter", 13U, 2U);
    const float changed_channel = cubey::procedural::random01(77U, "fluid.emitter", 12U, 3U);
    const float changed_domain = cubey::procedural::random01(77U, "fluid.transfer", 12U, 2U);

    require_near(first, second, 0.0F, "domain random should repeat exactly");
    require(first >= 0.0F && first <= 1.0F, "domain random should stay in [0, 1]");
    require(changed_index >= 0.0F && changed_index <= 1.0F,
            "changed-index domain random should stay in [0, 1]");
    require(changed_channel >= 0.0F && changed_channel <= 1.0F,
            "changed-channel domain random should stay in [0, 1]");
    require(changed_domain >= 0.0F && changed_domain <= 1.0F,
            "changed-domain random should stay in [0, 1]");
    require(first != changed_index, "domain random should include the index");
    require(first != changed_channel, "domain random should include the channel");
    require(first != changed_domain, "domain random should include the named domain");

    require_near(cubey::procedural::random01(77U, 12U, 2U),
                 cubey::procedural::random01(77U, 12U, 2U), 0.0F,
                 "legacy random01 overload should remain stable");
}

void test_procedural_shader_random_helpers_are_shared() {
    const std::filesystem::path root{CUBEY_SOURCE_DIR};
    const std::string random_source =
        cubey::tests::read_source_file(root / "shaders/cubey/procedural/random.glsl");
    const std::string noise_source =
        cubey::tests::read_source_file(root / "shaders/cubey/procedural/noise.glsl");

    cubey::tests::require_contains(random_source, "cubey_proc_hash01_u32",
                                   "shader random helpers should expose uint hash-to-unit");
    cubey::tests::require_contains(random_source, "cubey_proc_hash_pcg_2d",
                                   "shader random helpers should expose shared 2D PCG hash");
    cubey::tests::require_contains(random_source, "cubey_proc_hash_pcg_3d",
                                   "shader random helpers should expose shared 3D PCG hash");
    cubey::tests::require_contains(noise_source, "#include \"cubey/procedural/random.glsl\"",
                                   "shader noise helpers should consume shared random helpers");
}

void test_procedural_noise_is_deterministic_and_bounded() {
    const float first = cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U);
    const float second = cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U);
    require_near(first, second, 0.000001F, "value noise should be deterministic");
    require(first >= -1.0F && first <= 1.0F, "value noise should remain in signed unit range");

    const float changed_seed = cubey::procedural::value_noise_2d(1.25F, -3.75F, 18U);
    require(std::fabs(first - changed_seed) > 0.000001F,
            "value noise should vary when the seed changes");

    const float fbm = cubey::procedural::fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5});
    require(fbm >= -1.0F && fbm <= 1.0F, "fbm should remain in signed unit range");

    const float ridged = cubey::procedural::ridged_fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5});
    require(ridged >= 0.0F && ridged <= 1.0F, "ridged fbm should remain in unit range");
}

void test_procedural_legacy_noise_golden_values_are_stable() {
    require_near(cubey::procedural::value_noise_2d(1.25F, -3.75F, 17U), -0.457798481F, 0.000001F,
                 "legacy value noise should keep stable samples");
    require_near(cubey::procedural::fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5}), -0.063320771F,
                 0.000001F, "legacy fbm should keep stable samples");
    require_near(cubey::procedural::ridged_fbm_2d(2.4F, -0.7F, 42U, {.octaves = 5}), 0.877368033F,
                 0.000001F, "legacy ridged fbm should keep stable samples");
}

void test_procedural_3d_noise_is_deterministic_and_stable() {
    const float first = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U);
    const float second = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U);
    require_near(first, second, 0.000001F, "3D value noise should be deterministic");
    require(first >= -1.0F && first <= 1.0F, "3D value noise should remain in signed unit range");

    const float changed_seed = cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 18U);
    require(std::fabs(first - changed_seed) > 0.000001F,
            "3D value noise should vary when the seed changes");

    const float fbm = cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5});
    require(fbm >= -1.0F && fbm <= 1.0F, "3D fbm should remain in signed unit range");

    const float ridged = cubey::procedural::ridged_fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5});
    require(ridged >= 0.0F && ridged <= 1.0F, "3D ridged fbm should remain in unit range");

    require_near(cubey::procedural::hash_to_unit(123456789U), 0.659940481F, 0.000001F,
                 "3D-compatible hash-to-unit should keep stable samples");
    require(cubey::procedural::hash_u32(-2, 7, 4, 19U) == 2469915974U,
            "3D-compatible hash should keep stable samples");
    require_near(cubey::procedural::value_noise_3d(1.25F, -3.75F, 0.5F, 17U), 0.302227825F,
                 0.000001F, "3D value noise should keep stable samples");
    require_near(cubey::procedural::fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5}), -0.123893112F,
                 0.000001F, "3D fbm should keep stable samples");
    require_near(cubey::procedural::ridged_fbm_3d(2.4F, -0.7F, 1.9F, 42U, {.octaves = 5}),
                 0.767563224F, 0.000001F, "3D ridged fbm should keep stable samples");
}

void test_procedural_coherent_noise_wraps_fastnoise_lite() {
    using cubey::procedural::CoherentCellularDistance;
    using cubey::procedural::CoherentCellularReturn;
    using cubey::procedural::CoherentDomainWarpConfig;
    using cubey::procedural::CoherentDomainWarpFractalType;
    using cubey::procedural::CoherentDomainWarpType;
    using cubey::procedural::CoherentFractalType;
    using cubey::procedural::CoherentNoiseConfig;
    using cubey::procedural::CoherentNoiseType;

    const CoherentNoiseConfig base{
        .seed = 47,
        .frequency = 0.035F,
        .noise_type = CoherentNoiseType::OpenSimplex2,
        .fractal_type = CoherentFractalType::Fbm,
        .octaves = 4,
        .lacunarity = 2.08F,
        .gain = 0.52F,
        .weighted_strength = 0.18F,
    };
    const float first = cubey::procedural::sample_coherent_noise_2d(12.5F, -7.25F, base);
    const float second = cubey::procedural::sample_coherent_noise_2d(12.5F, -7.25F, base);
    require_near(first, second, 0.000001F, "coherent noise should be deterministic");
    require_finite_unit(first, "coherent noise should stay in signed unit range");

    CoherentNoiseConfig changed_seed = base;
    changed_seed.seed = 48;
    const float changed = cubey::procedural::sample_coherent_noise_2d(12.5F, -7.25F, changed_seed);
    require(std::fabs(first - changed) > 0.000001F,
            "coherent noise should change when the seed changes");

    constexpr std::array noise_types{
        CoherentNoiseType::OpenSimplex2S,
        CoherentNoiseType::Perlin,
        CoherentNoiseType::ValueCubic,
        CoherentNoiseType::Value,
    };
    for (const CoherentNoiseType noise_type : noise_types) {
        CoherentNoiseConfig config = base;
        config.noise_type = noise_type;
        config.fractal_type = CoherentFractalType::Ridged;
        require_finite_unit(cubey::procedural::sample_coherent_noise_3d(2.0F, -3.0F, 4.0F, config),
                            "coherent 3D noise modes should be finite and bounded");
    }

    CoherentNoiseConfig cellular = base;
    cellular.noise_type = CoherentNoiseType::Cellular;
    cellular.fractal_type = CoherentFractalType::None;
    cellular.cellular_distance = CoherentCellularDistance::Hybrid;
    cellular.cellular_return = CoherentCellularReturn::Distance2Add;
    require_finite_unit(cubey::procedural::sample_coherent_noise_2d(-4.0F, 8.0F, cellular),
                        "coherent cellular noise should be finite and bounded");

    CoherentNoiseConfig ping_pong = base;
    ping_pong.fractal_type = CoherentFractalType::PingPong;
    ping_pong.ping_pong_strength = 1.75F;
    require_finite_unit(cubey::procedural::sample_coherent_noise_2d(9.0F, 3.0F, ping_pong),
                        "coherent ping-pong fractal noise should be finite and bounded");

    const CoherentDomainWarpConfig warp{
        .seed = 91,
        .frequency = 0.08F,
        .warp_type = CoherentDomainWarpType::BasicGrid,
        .fractal_type = CoherentDomainWarpFractalType::Progressive,
        .octaves = 3,
        .amplitude = 0.50F,
    };
    const cubey::procedural::CoherentWarp2D warp_a =
        cubey::procedural::domain_warp_2d(1.50F, -2.25F, warp);
    const cubey::procedural::CoherentWarp2D warp_b =
        cubey::procedural::domain_warp_2d(1.50F, -2.25F, warp);
    require_near(warp_a.x, warp_b.x, 0.000001F, "2D domain warp should be deterministic");
    require_near(warp_a.y, warp_b.y, 0.000001F, "2D domain warp should be deterministic");
    require(std::fabs(warp_a.x - 1.50F) > 0.000001F || std::fabs(warp_a.y + 2.25F) > 0.000001F,
            "2D domain warp should move at least one coordinate");

    CoherentDomainWarpConfig warp3 = warp;
    warp3.warp_type = CoherentDomainWarpType::OpenSimplex2Reduced;
    warp3.fractal_type = CoherentDomainWarpFractalType::Independent;
    const cubey::procedural::CoherentWarp3D warped =
        cubey::procedural::domain_warp_3d(1.0F, 2.0F, 3.0F, warp3);
    require(std::isfinite(warped.x) && std::isfinite(warped.y) && std::isfinite(warped.z),
            "3D domain warp should produce finite coordinates");
}

void test_procedural_source_fields_wrap_legacy_noise_backends() {
    cubey::procedural::NoiseSource2D config{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = 42U,
        .legacy_fbm = {.octaves = 5},
        .domain = {.x_scale = 1.5F, .y_scale = 0.75F, .x_offset = 2.0F, .y_offset = -3.0F},
    };

    const float expected_signed = cubey::procedural::fbm_2d(
        (1.25F * 1.5F) + 2.0F, (-0.5F * 0.75F) - 3.0F, 42U, {.octaves = 5});
    require_near(cubey::procedural::sample_noise_source_2d(1.25F, -0.5F, config), expected_signed,
                 0.000001F, "legacy source should apply domain transform before sampling FBM");

    config.output = cubey::procedural::NoiseSource2DOutput::Unit;
    require_near(cubey::procedural::sample_noise_source_2d(1.25F, -0.5F, config),
                 (expected_signed * 0.5F) + 0.5F, 0.000001F,
                 "legacy source should map signed FBM to unit output");

    config.backend = cubey::procedural::NoiseSource2DBackend::LegacyRidgedFbm;
    config.output = cubey::procedural::NoiseSource2DOutput::Unit;
    const float expected_ridged = cubey::procedural::ridged_fbm_2d(
        (1.25F * 1.5F) + 2.0F, (-0.5F * 0.75F) - 3.0F, 42U, {.octaves = 5});
    require_near(cubey::procedural::sample_noise_source_2d(1.25F, -0.5F, config), expected_ridged,
                 0.000001F, "ridged legacy source should remain naturally unit range");
}

void test_procedural_source_fields_wrap_coherent_noise_backend() {
    const cubey::procedural::NoiseSource2D source{
        .backend = cubey::procedural::NoiseSource2DBackend::CoherentNoise,
        .output = cubey::procedural::NoiseSource2DOutput::Unit,
        .seed = 47U,
        .coherent =
            {
                .frequency = 0.035F,
                .noise_type = cubey::procedural::CoherentNoiseType::OpenSimplex2,
                .fractal_type = cubey::procedural::CoherentFractalType::Fbm,
                .octaves = 4,
                .lacunarity = 2.08F,
                .gain = 0.52F,
                .weighted_strength = 0.18F,
            },
        .domain = {.x_scale = 0.8F, .y_scale = 1.2F, .x_offset = 5.0F, .y_offset = -2.0F},
    };
    cubey::procedural::CoherentNoiseConfig coherent = source.coherent;
    coherent.seed = 47;
    const float expected = cubey::procedural::sample_coherent_noise_2d(
        (12.5F * 0.8F) + 5.0F, (-7.25F * 1.2F) - 2.0F, coherent);
    require_near(cubey::procedural::sample_noise_source_2d(12.5F, -7.25F, source),
                 (expected * 0.5F) + 0.5F, 0.000001F,
                 "coherent source should use shared seed and domain transform");
}

void test_procedural_source_fields_apply_optional_domain_warp() {
    cubey::procedural::NoiseSource2D source{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = 31U,
        .legacy_fbm = {.octaves = 4},
        .domain = {.x_scale = 1.25F, .y_scale = 0.75F, .x_offset = 2.0F, .y_offset = -1.0F},
        .warp =
            {
                .enabled = true,
                .seed_offset = 19U,
                .coherent =
                    {
                        .frequency = 0.08F,
                        .warp_type = cubey::procedural::CoherentDomainWarpType::OpenSimplex2,
                        .fractal_type =
                            cubey::procedural::CoherentDomainWarpFractalType::Progressive,
                        .octaves = 2,
                        .lacunarity = 2.0F,
                        .gain = 0.5F,
                        .amplitude = 3.0F,
                    },
            },
    };

    const float sx = (4.0F * 1.25F) + 2.0F;
    const float sy = (-2.0F * 0.75F) - 1.0F;
    cubey::procedural::CoherentDomainWarpConfig warp = source.warp.coherent;
    warp.seed = static_cast<std::int32_t>((source.seed + source.warp.seed_offset) & 0x7fff'ffffULL);
    const cubey::procedural::CoherentWarp2D warped =
        cubey::procedural::domain_warp_2d(sx, sy, warp);
    const float expected =
        cubey::procedural::fbm_2d(warped.x, warped.y, source.seed, {.octaves = 4});

    const float first = cubey::procedural::sample_noise_source_2d(4.0F, -2.0F, source);
    const float second = cubey::procedural::sample_noise_source_2d(4.0F, -2.0F, source);
    require_near(first, expected, 0.000001F, "warped source should sample after domain warp");
    require_near(first, second, 0.000001F, "warped source should remain deterministic");

    source.warp.enabled = false;
    const float unwarped = cubey::procedural::sample_noise_source_2d(4.0F, -2.0F, source);
    require_near(unwarped, cubey::procedural::fbm_2d(sx, sy, source.seed, {.octaves = 4}),
                 0.000001F, "disabled warp should preserve unwarped sampling");
    require(std::fabs(first - unwarped) > 0.000001F,
            "enabled warp should alter the sampled coordinates");
}

void test_procedural_source_fields_fill_scalar_fields() {
    const cubey::procedural::Grid2DDesc desc{
        .width = 2,
        .height = 2,
        .cell_size = 2.0F,
        .origin_x = 10.0F,
        .origin_y = -4.0F,
    };
    const cubey::procedural::NoiseSource2D source{
        .backend = cubey::procedural::NoiseSource2DBackend::LegacyFbm,
        .output = cubey::procedural::NoiseSource2DOutput::Signed,
        .seed = 17U,
        .legacy_fbm = {.octaves = 3},
        .domain = {.x_scale = 0.25F, .y_scale = 0.5F, .x_offset = 1.0F, .y_offset = -1.0F},
    };
    const cubey::procedural::ScalarField2D field =
        cubey::procedural::sample_noise_source_field_2d(desc, source);

    require(field.desc().width == desc.width && field.desc().height == desc.height,
            "source field sampling should preserve grid dimensions");
    const float x = cubey::procedural::grid_sample_x(desc, 1U);
    const float y = cubey::procedural::grid_sample_y(desc, 0U);
    require_near(field.at(1U, 0U), cubey::procedural::sample_noise_source_2d(x, y, source),
                 0.000001F, "source field sampling should use centered grid coordinates");
}

void test_procedural_source_recipes_compose_layers_and_debug_fields() {
    const cubey::procedural::Grid2DDesc desc{.width = 2, .height = 2, .cell_size = 1.0F};
    const cubey::procedural::NoiseSource2D base_source = unit_legacy_source(11U);
    const cubey::procedural::NoiseSource2D disabled_source = unit_legacy_source(12U);
    const cubey::procedural::NoiseSource2D detail_source = unit_legacy_source(13U);
    const cubey::procedural::SourceRecipe2D recipe{
        .name = "masked-add",
        .layers =
            {
                {.name = "base", .source = base_source},
                {.name = "disabled", .enabled = false, .source = disabled_source},
                {
                    .name = "detail",
                    .source = detail_source,
                    .weight = 0.5F,
                    .blend_mode = cubey::procedural::SourceRecipeBlendMode2D::Add,
                    .use_first_layer_as_mask = true,
                },
            },
    };

    const cubey::procedural::SourceRecipe2DResult result =
        cubey::procedural::sample_source_recipe_2d(desc, recipe);
    require(result.output.desc().width == desc.width && result.output.desc().height == desc.height,
            "source recipe should preserve grid dimensions");
    require(result.debug_fields.field_count() == 3U,
            "source recipe debug fields should include enabled layers plus output");
    require(result.debug_fields.has_field("base"), "source recipe debug fields should include base");
    require(!result.debug_fields.has_field("disabled"),
            "source recipe debug fields should skip disabled layers");
    require(result.debug_fields.has_field("detail"),
            "source recipe debug fields should include detail");
    require(result.debug_fields.has_field("output"),
            "source recipe debug fields should include final output");

    const float x = cubey::procedural::grid_sample_x(desc, 1U);
    const float y = cubey::procedural::grid_sample_y(desc, 0U);
    const float base = cubey::procedural::sample_noise_source_2d(x, y, base_source);
    const float detail = cubey::procedural::sample_noise_source_2d(x, y, detail_source);
    const float expected = base + (detail * 0.5F * cubey::procedural::saturate(base));
    require_near(result.output.at(1U, 0U), expected, 0.000001F,
                 "source recipe should add masked weighted detail");
    require_near(result.debug_fields.field("output").at(1U, 0U), expected, 0.000001F,
                 "source recipe debug output should match result output");
    require_near(result.output_stats.mean, result.output.summarize().mean, 0.000001F,
                 "source recipe should summarize output");
}

void test_procedural_source_recipes_apply_blend_modes() {
    const cubey::procedural::Grid2DDesc desc{.width = 2, .height = 2, .cell_size = 1.0F};
    const cubey::procedural::NoiseSource2D base_source = unit_legacy_source(21U);
    const cubey::procedural::NoiseSource2D overlay_source = unit_legacy_source(22U);
    const float x = cubey::procedural::grid_sample_x(desc, 0U);
    const float y = cubey::procedural::grid_sample_y(desc, 1U);
    const float base = cubey::procedural::sample_noise_source_2d(x, y, base_source);
    const float overlay = cubey::procedural::sample_noise_source_2d(x, y, overlay_source);

    const auto sample_mode = [&](cubey::procedural::SourceRecipeBlendMode2D mode) {
        const cubey::procedural::SourceRecipe2D recipe{
            .name = "blend-mode",
            .layers =
                {
                    {.name = "base", .source = base_source},
                    {.name = "overlay", .source = overlay_source, .weight = 0.25F, .blend_mode = mode},
                },
        };
        return cubey::procedural::sample_source_recipe_2d(desc, recipe).output.at(0U, 1U);
    };

    require_near(sample_mode(cubey::procedural::SourceRecipeBlendMode2D::Blend),
                 cubey::procedural::lerp(base, overlay, 0.25F), 0.000001F,
                 "source recipe blend mode should interpolate toward overlay");
    require_near(sample_mode(cubey::procedural::SourceRecipeBlendMode2D::Multiply),
                 base * cubey::procedural::lerp(1.0F, overlay, 0.25F), 0.000001F,
                 "source recipe multiply mode should interpolate multiplier strength");
    require_near(sample_mode(cubey::procedural::SourceRecipeBlendMode2D::Min),
                 cubey::procedural::lerp(base, std::min(base, overlay), 0.25F), 0.000001F,
                 "source recipe min mode should interpolate toward the lower sample");
    require_near(sample_mode(cubey::procedural::SourceRecipeBlendMode2D::Max),
                 cubey::procedural::lerp(base, std::max(base, overlay), 0.25F), 0.000001F,
                 "source recipe max mode should interpolate toward the higher sample");
}

void test_procedural_source_recipes_normalize_outputs() {
    const cubey::procedural::Grid2DDesc desc{.width = 4, .height = 4, .cell_size = 1.0F};
    cubey::procedural::NoiseSource2D source = unit_legacy_source(31U);
    source.output = cubey::procedural::NoiseSource2DOutput::Signed;
    const cubey::procedural::SourceRecipe2D recipe{
        .name = "normalized",
        .layers = {{.name = "base", .source = source}},
        .normalize_output_to_unit = true,
    };

    const cubey::procedural::SourceRecipe2DResult result =
        cubey::procedural::sample_source_recipe_2d(desc, recipe);
    const cubey::procedural::ScalarFieldStats stats = result.output.summarize();
    require_near(stats.min, 0.0F, 0.0001F, "normalized source recipe should map min to zero");
    require_near(stats.max, 1.0F, 0.0001F, "normalized source recipe should map max to one");
    require_near(result.output_stats.min, 0.0F, 0.0001F,
                 "normalized source recipe stats should report normalized min");
    require_near(result.output_stats.max, 1.0F, 0.0001F,
                 "normalized source recipe stats should report normalized max");
}

void test_procedural_source_recipes_reject_invalid_layers() {
    const cubey::procedural::Grid2DDesc desc{.width = 2, .height = 2};
    const cubey::procedural::NoiseSource2D source = unit_legacy_source(41U);

    require_throws([&] { (void)cubey::procedural::sample_source_recipe_2d(desc, {}); },
                   "source recipe should reject empty layer lists");
    require_throws(
        [&] {
            (void)cubey::procedural::sample_source_recipe_2d(
                desc, {.layers = {{.name = "base", .enabled = false, .source = source}}});
        },
        "source recipe should reject recipes with no enabled layers");
    require_throws(
        [&] {
            (void)cubey::procedural::sample_source_recipe_2d(
                desc, {.layers = {{.name = "", .source = source}}});
        },
        "source recipe should reject empty layer names");
    require_throws(
        [&] {
            (void)cubey::procedural::sample_source_recipe_2d(
                desc, {.layers = {{.name = "output", .source = source}}});
        },
        "source recipe should reject reserved output layer names");
    require_throws(
        [&] {
            (void)cubey::procedural::sample_source_recipe_2d(
                desc,
                {.layers = {{.name = "base", .source = source}, {.name = "base", .source = source}}});
        },
        "source recipe should reject duplicate layer names");
    require_throws(
        [&] {
            (void)cubey::procedural::sample_source_recipe_2d(
                desc,
                {
                    .layers =
                        {{
                            .name = "base",
                            .source = source,
                            .weight = std::numeric_limits<float>::quiet_NaN(),
                        }},
                });
        },
        "source recipe should reject non-finite layer weights");
}
