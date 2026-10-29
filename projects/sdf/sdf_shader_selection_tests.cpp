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
}
