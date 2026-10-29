#include "sdf_shader_selection.h"
#include <iostream>
#include <stdexcept>
using namespace cubey::projects::sdf;
int main() {
    SdfConfig config;
    const auto expect = [&](std::size_t count, SourceShaderRoute expected) {
        if (select_source_shader(config, count) != expected)
            throw std::runtime_error("source shader eligibility boundary failed");
    };
    expect(1, SourceShaderRoute::General);
    if (source_shader_variants_enabled(config))
        throw std::runtime_error("sampled route opted in");
    for (const auto mode : {"analytic", "hybrid"}) {
        config.surface_trace = mode;
        expect(0, SourceShaderRoute::LitGeneral);
        expect(1, SourceShaderRoute::LitSingleMaterial);
        for (std::size_t count : {2U, 16U})
            expect(count, SourceShaderRoute::LitGeneral);
        config.material_fast_path = false;
        expect(1, SourceShaderRoute::LitGeneral);
        config.material_fast_path = true;
        for (unsigned view = 1; view <= 12; ++view) {
            config.debug = view;
            expect(1, SourceShaderRoute::General);
            expect(2, SourceShaderRoute::General);
        }
        config.debug = 0;
        for (const auto lighting :
             {"atmosphere", "material-study", "material-furnace", "material-atmosphere"}) {
            config.lighting = lighting;
            config.ao = true;
            config.clay = true;
            expect(1, SourceShaderRoute::LitSingleMaterial);
            expect(2, SourceShaderRoute::LitGeneral);
        }
        config.source_shaders = "general";
        expect(1, SourceShaderRoute::General);
        config.source_shaders = "automatic";
        config.frame_profile = "specialized";
        expect(1, SourceShaderRoute::General);
        config.frame_profile = "off";
        config.comparison_stage = "full";
        expect(1, SourceShaderRoute::General);
        config.comparison_stage = "off";
        for (std::size_t count : {1U, 2U, 1U, 0U, 2U, 1U})
            expect(count, count == 1 ? SourceShaderRoute::LitSingleMaterial
                                     : SourceShaderRoute::LitGeneral);
    }
    std::cout << "source shader eligibility/default/debug/appearance/history boundaries passed\n";
    config = SdfConfig{};
    const auto rejected = [&] {
        try {
            validate_source_optimization(config);
        } catch (const std::invalid_argument&) {
            return;
        }
        throw std::runtime_error("incompatible optimization was admitted");
    };
    config.source_optimization = "shadow";
    rejected();
    config.surface_trace = "analytic";
    validate_source_optimization(config);
    if (!source_shadow_pipeline_enabled(config))
        throw std::runtime_error("eligible shadow renderer missing candidate");
    for (const auto lighting : {"material-study", "material-furnace", "material-atmosphere"}) {
        config.lighting = lighting;
        if (source_shadow_pipeline_enabled(config))
            throw std::runtime_error("optical family opted into shadow candidate");
    }
    config.lighting = "atmosphere";
    config.source_shaders = "general";
    rejected();
    config.source_optimization = "automatic";
    validate_source_optimization(config);
    if (source_shadow_pipeline_enabled(config))
        throw std::runtime_error("general control opted into candidate");
    config.source_shaders = "automatic";
    config.source_optimization = "reference";
    if (source_shadow_pipeline_enabled(config))
        throw std::runtime_error("reference control opted into candidate");
    config.source_optimization = "shadow";
    config.frame_profile = "opt-full";
    rejected();
    config.frame_profile = "off";
    config.comparison_stage = "primary";
    rejected();
    config.comparison_stage = "off";
    config.source_optimization = "prepared-cones";
    rejected();
    config.common.headless = true;
    config.csg_evaluation = "pruned";
    config.region_axis = 64;
    config.acceleration_variant = "root-far";
    validate_source_optimization(config);
    if (!source_prepared_cones_enabled(config) || source_shadow_pipeline_enabled(config))
        throw std::runtime_error("independent prepared-cone mode failed");
    config.surface_trace = "hybrid";
    rejected();
    config.surface_trace = "analytic";
    config.source_optimization = "automatic";
    if (!source_shadow_pipeline_enabled(config) || source_prepared_cones_enabled(config))
        throw std::runtime_error("automatic did not retain qualified shadow-only behavior");
    config.source_optimization = "combined";
    validate_source_optimization(config);
    if (!source_shadow_pipeline_enabled(config) || !source_prepared_cones_enabled(config))
        throw std::runtime_error("qualified independent candidates did not combine");
    config.common.headless = false;
    rejected();
    config.common.headless = true;
    config.query_workload_output = "private-query.bin";
    config.common.frames = 1;
    validate_source_optimization(config);
    config.common.frames = 2;
    rejected();
    config.common.frames = 1;
    config.ao = true;
    rejected();
    config.ao = false;
    config.comparison_input = "fixtures.json";
    rejected();
    config.comparison_input.clear();
    validate_source_optimization(config);
    config.query_workload_output.clear();
    for (const auto step : {"relaxed-1.2", "relaxed-1.5", "adaptive-0.5"}) {
        config.query_stepping = step;
        validate_source_optimization(config);
        config.surface_trace = "hybrid";
        rejected();
        config.surface_trace = "analytic";
        config.common.headless = false;
        rejected();
        config.common.headless = true;
    }
    config.query_stepping = "unknown";
    rejected();
}
