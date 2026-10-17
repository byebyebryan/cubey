#include "fluid_25d_test_support.h"

namespace cubey::projects::fluid::fluid_25d::test_detail {

void test_rain_visual_cli() {
    const auto defaults = parse_project({"fluid_25d"});
    require(!defaults.native_rain_visuals && defaults.native_rain_strength == 1.0F &&
                defaults.native_rain_speed == 3.0F,
            "rain presentation remains opt-in until visual acceptance");
    const auto visible = parse_project(
        {"fluid_25d", "--fluid25d-recording", "recording.json", "--fluid25d-native-presentation",
         "scenic", "--fluid25d-rain-visuals", "--fluid25d-rain-visual-strength", "0.75",
         "--fluid25d-rain-visual-speed", "4"});
    require(visible.native_rain_visuals && visible.native_rain_strength == 0.75F &&
                visible.native_rain_speed == 4.0F &&
                visible.simulation.rainfall_depth_rate_m_per_s ==
                    defaults.simulation.rainfall_depth_rate_m_per_s,
            "visible rain strength/speed never edits solver rainfall");
    for (const auto& arguments : std::vector<std::vector<std::string>>{
             {"fluid_25d", "--fluid25d-rain-visuals"},
             {"fluid_25d", "--fluid25d-recording", "recording.json", "--fluid25d-rain-visuals"},
             {"fluid_25d", "--fluid25d-recording", "recording.json",
              "--fluid25d-native-presentation", "scenic", "--fluid25d-rain-visual-strength", "-1"},
             {"fluid_25d", "--fluid25d-recording", "recording.json",
              "--fluid25d-native-presentation", "scenic", "--fluid25d-rain-visual-strength", "3"},
             {"fluid_25d", "--fluid25d-rain-visual-speed", "2"},
             {"fluid_25d", "--fluid25d-recording", "recording.json", "--fluid25d-rain-visual-speed",
              "2"},
             {"fluid_25d", "--fluid25d-recording", "recording.json",
              "--fluid25d-native-presentation", "scenic", "--fluid25d-rain-visual-speed", "0.1"},
             {"fluid_25d", "--fluid25d-recording", "recording.json",
              "--fluid25d-native-presentation", "scenic", "--fluid25d-rain-visual-speed", "9"},
             {"fluid_25d", "--fluid25d-recording", "recording.json",
              "--fluid25d-native-presentation", "scenic", "--fluid25d-rain-visual-speed", "nan"}})
        require_throws([&] { static_cast<void>(parse_project(arguments)); },
                       "rain visual controls require Scenic/native source and bounded strength");
}

void test_whitewater_sources_and_clock() {
    using namespace cubey::projects::fluid::fluid_25d;
    constexpr std::uint32_t width = 32U, height = 24U;
    std::vector<float> bed(width * height), h(width * height, 0.25F);
    std::vector<Fluid25DVelocity> u(width * height, {-3.0F, 0.0F});
    for (std::size_t i = 0; i < bed.size(); ++i)
        bed[i] = float(i % width) * 10.0F;
    const auto before_bed = bed, before_h = h;
    const auto a = fluid_25d_whitewater_seeds(width, height, 10, bed, h, u, 4096);
    const auto b = fluid_25d_whitewater_seeds(width, height, 10, bed, h, u, 4096);
    require(a.seeds.size() == 4096U && a.requested == (width - 1U) * (height - 1U) * 19U,
            "whitewater must respect budget and reference density on a fast steep plane");
    bool first = false, last = false;
    for (std::size_t i = 0; i < a.seeds.size(); ++i) {
        const auto& seed = a.seeds[i];
        require(seed.cell == b.seeds[i].cell && seed.slot == b.seeds[i].slot &&
                    seed.weight == b.seeds[i].weight && seed.slot < 19U && seed.weight >= 0.0F &&
                    seed.weight <= 1.0F,
                "retained native cell/slot IDs and opacity must be deterministic and bounded");
        first |= seed.cell / width == 0U;
        last |= seed.cell / width == height - 2U;
    }
    require(first && last && bed == before_bed && h == before_h,
            "budget selection must not starve later branches or mutate water/terrain");
    for (const auto velocity :
         {Fluid25DVelocity{0, 0}, Fluid25DVelocity{3, 0}, Fluid25DVelocity{0, 3}}) {
        std::fill(u.begin(), u.end(), velocity);
        require(fluid_25d_whitewater_seeds(width, height, 10, bed, h, u, 4096).seeds.empty(),
                "stationary, uphill and cross-slope controls cannot emit steep flecks");
    }
    std::fill(u.begin(), u.end(), Fluid25DVelocity{-3, 0});
    for (const float depth : {0.0F, 0.06F, 0.08F}) {
        std::fill(h.begin(), h.end(), depth);
        require(fluid_25d_whitewater_seeds(width, height, 10, bed, h, u, 4096).seeds.empty(),
                "dry and thin hillside films must not acquire whitewater sources");
    }
    h[0] = std::numeric_limits<float>::quiet_NaN();
    require_throws(
        [&] { static_cast<void>(fluid_25d_whitewater_seeds(width, height, 10, bed, h, u, 4096)); },
        "non-finite display fields are rejected");
    Fluid25DRainVisualClock thirty, sixty;
    thirty.capture(30, 30, true, 8);
    sixty.capture(60, 60, true, 0.25F);
    require(thirty.seconds() == 1.0 && sixty.seconds() == 1.0,
            "whitewater presentation time is frame-rate and rain-speed independent");
    thirty.advance(0.1, false);
    require(thirty.seconds() == 1.0, "pause holds whitewater presentation time");
    thirty.reset();
    require(thirty.seconds() == 0.0, "seek/reset clears the presentation clock");
    const auto preset = fluid_25d_scenic_material("macro");
    require(preset.water_cascade_strength == 1.0F && preset.water_whitewater_strength == 1.0F &&
                preset.water_whitewater_speed == 2.0F &&
                preset.water_stream_foam_strength == 0.3F &&
                preset.water_stream_foam_patchiness == 1.0F &&
                preset.water_stream_foam_brightness == 0.55F,
            "Macro defaults to the reviewed cascades, moving flecks and subtle irregular foam");
    const auto tuned = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_whitewater_strength":1,"water_whitewater_speed":2,"water_stream_foam_strength":1,"water_stream_foam_patchiness":0,"water_stream_foam_brightness":1})",
        preset);
    require(tuned.water_whitewater_radius_m == 3 && tuned.water_whitewater_lift_m == 1.5F &&
                tuned.water_whitewater_speed == 2 && tuned.water_stream_foam_strength == 1 &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(tuned), preset)) ==
                    fluid_25d_scenic_material_json(tuned),
            "3 m whitewater controls roundtrip without changing physical time");
    require(tuned.water_stream_foam_patchiness == 0 && tuned.water_stream_foam_brightness == 1,
            "existing foam tuning retains its dense pattern and brightness");
    const auto patches = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_stream_foam_patchiness":1,"water_stream_foam_brightness":0.4})",
        tuned);
    require(
        patches.water_stream_foam_patchiness == 1 && patches.water_stream_foam_brightness == 0.4F &&
            patches.water_whitewater_speed == tuned.water_whitewater_speed &&
            patches.water_roughness == tuned.water_roughness &&
            fluid_25d_scenic_material_json(
                fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(patches), preset)) ==
                fluid_25d_scenic_material_json(patches),
        "independent foam controls roundtrip without retiming motion or altering water roughness");
    const auto disabled = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_cascade_strength":0,"water_whitewater_strength":0,"water_stream_foam_strength":0})",
        preset);
    require(disabled.water_cascade_strength == 0.0F && disabled.water_whitewater_strength == 0.0F &&
                disabled.water_stream_foam_strength == 0.0F &&
                disabled.water_extinction_scale == preset.water_extinction_scale &&
                disabled.water_roughness == preset.water_roughness &&
                disabled.daylight_environment == preset.daylight_environment,
            "Macro flow cues can be disabled without changing optics, roughness or daylight");
    for (const auto* text :
         {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_whitewater_budget":4096.5})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_whitewater_speed":0})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_whitewater_radius_m":100})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_stream_foam_strength":2})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_stream_foam_patchiness":-0.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_stream_foam_patchiness":1.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_stream_foam_brightness":-0.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_stream_foam_brightness":1.01})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, preset)); },
                       "whitewater tuning must be finite/bounded with an integer budget");
}

void test_bank_edge_material_contract() {
    using namespace cubey::projects::fluid::fluid_25d;
    for (const auto* name : {"v1", "refined", "terrain", "macro"}) {
        const auto preset = fluid_25d_scenic_material(name);
        require(preset.water_bank_irregularity_m == 0 && preset.water_bank_motion_m == 0 &&
                    preset.water_bank_scale_m == 48 && preset.water_bank_band_m == 12,
                "water-edge trimming is opt-in in every material");
    }
    const auto preset = fluid_25d_scenic_material("macro");
    const auto tuned = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_irregularity_m":3,"water_bank_motion_m":1,"water_bank_scale_m":40,"water_bank_band_m":10})",
        preset);
    require(tuned.water_bank_irregularity_m == 3 && tuned.water_bank_motion_m == 1 &&
                tuned.water_bank_scale_m == 40 && tuned.water_bank_band_m == 10 &&
                tuned.water_stream_foam_strength == preset.water_stream_foam_strength &&
                tuned.water_whitewater_speed == preset.water_whitewater_speed &&
                tuned.water_extinction_scale == preset.water_extinction_scale &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(tuned), preset)) ==
                    fluid_25d_scenic_material_json(tuned),
            "bank edge controls roundtrip without changing approved optics or foam");
    const auto off = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_irregularity_m":0,"water_bank_motion_m":0})",
        tuned);
    require(off.water_bank_irregularity_m == 0 && off.water_bank_motion_m == 0,
            "both bank effects can be disabled independently");
    require(fluid_25d_scenic_water_view("bank-edge") == 18U &&
                fluid_25d_scenic_water_view("no-bank-edge") == 19U,
            "bank edge diagnostic and full-shading ablation append stable view IDs");
    for (const auto* text :
         {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_irregularity_m":-0.1})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_irregularity_m":24.1})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_motion_m":12.1})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_motion_m":true})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_scale_m":0})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_band_m":64.1})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, preset)); },
                       "bank edge control bounds and types reject");
    const auto bold = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_bank_irregularity_m":24,"water_bank_motion_m":12,"water_bank_band_m":64})",
        preset);
    require(bold.water_bank_irregularity_m == 24 && bold.water_bank_motion_m == 12 &&
                bold.water_bank_band_m == 64,
            "pronounced bank requests are accepted without changing preset defaults");
}

void test_scenic_material_contract() {
    using namespace cubey::projects::fluid::fluid_25d;
    const auto v1 = fluid_25d_scenic_material("v1");
    const auto refined = fluid_25d_scenic_material("refined");
    const auto terrain = fluid_25d_scenic_material("terrain");
    const auto macro = fluid_25d_scenic_material("macro");
    require(macro.rain_macro_softening == 0.0F && refined.rain_macro_softening == 0.0F,
            "macro rain softening must not silently change accepted views");
    const auto soft_rain = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","rain_macro_softening":1})", macro);
    require(soft_rain.rain_macro_softening == 1.0F &&
                soft_rain.water_rain_agitation == macro.water_rain_agitation &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(soft_rain), macro)) ==
                    fluid_25d_scenic_material_json(soft_rain),
            "rain softening must roundtrip without altering surface agitation");
    require_throws(
        [&] {
            (void)fluid_25d_parse_scenic_material(
                R"({"schema":"cubey.fluid25d.scenic-material.v2","rain_macro_softening":1.1})",
                macro);
        },
        "macro rain softening must remain bounded");
    require(macro.terrain_readability == 2.0F && v1.terrain_readability == 0.0F &&
                refined.terrain_readability == 0.0F && terrain.terrain_readability == 0.0F,
            "Macro promotes readable ground while older material references stay unchanged");
    const auto previous_ground = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_readability":0})", macro);
    require(previous_ground.terrain_readability == 0.0F &&
                previous_ground.water_roughness == macro.water_roughness &&
                previous_ground.water_shallow_extinction_boost ==
                    macro.water_shallow_extinction_boost &&
                previous_ground.water_stream_foam_strength == macro.water_stream_foam_strength &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(previous_ground), macro)) ==
                    fluid_25d_scenic_material_json(previous_ground),
            "previous ground is selectable without replacing water shading and roundtrips");
    require_throws(
        [&] {
            (void)fluid_25d_parse_scenic_material(
                R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_readability":1.5})",
                macro);
        },
        "fractional terrain detail mode rejected");
    const auto fixed_daylight = fluid_25d_fixed_daylight();
    for (const auto* material : {&v1, &refined, &terrain})
        require(material->water_rapid_strength == 0.0F && material->water_rapid_scale_m == 192.0F &&
                    material->water_cascade_strength == 0.0F &&
                    material->water_landing_strength == 0.0F &&
                    material->water_whitewater_strength == 0.0F &&
                    material->water_whitewater_speed == 1.0F &&
                    material->water_stream_foam_strength == 0.0F &&
                    material->water_stream_foam_patchiness == 0.0F &&
                    material->water_stream_foam_brightness == 1.0F,
                "legacy presets retain their reference flow appearance");
    require(macro.water_rapid_strength == 0.0F && macro.water_landing_strength == 0.0F,
            "Macro does not promote rejected rapid or landing treatments");
    const auto rapid = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rapid_strength":1,"water_rapid_scale_m":384})",
        macro);
    require(rapid.water_rapid_strength == 1.0F && rapid.water_rapid_scale_m == 384.0F &&
                rapid.water_extinction_scale == macro.water_extinction_scale &&
                rapid.water_ripple_strength == macro.water_ripple_strength &&
                rapid.water_shallow_coverage_strength == macro.water_shallow_coverage_strength &&
                fluid_25d_scenic_material_json(
                    fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(rapid), v1)) ==
                    fluid_25d_scenic_material_json(rapid),
            "rapid tuning roundtrips without replacing optics, wind or coverage");
    const auto cascade = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_cascade_strength":1,"water_landing_strength":0.5})",
        macro);
    require(cascade.water_cascade_strength == 1.0F && cascade.water_landing_strength == 0.5F &&
                cascade.water_rapid_strength == 0.0F &&
                cascade.water_shallow_coverage_strength == macro.water_shallow_coverage_strength &&
                fluid_25d_scenic_material_json(
                    fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(cascade), v1)) ==
                    fluid_25d_scenic_material_json(cascade),
            "cascade controls roundtrip independently of previous rapid and coverage options");
    for (const auto* text :
         {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_cascade_strength":-0.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_cascade_strength":1.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_cascade_strength":true})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_landing_strength":-0.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_landing_strength":1.01})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, macro)); },
                       "cascade controls reject invalid bounds and types");
    for (const auto* text :
         {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rapid_strength":-0.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rapid_strength":1.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rapid_strength":true})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rapid_scale_m":63.9})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rapid_scale_m":384.1})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, macro)); },
                       "rapid controls reject invalid bounds and types");
    require(glm::length(cubey::render::atmosphere_environment_sun_direction(fixed_daylight) -
                        glm::normalize(cubey::math::Vec3{-0.35F, 0.42F, 0.84F})) < 1e-6F &&
                !fixed_daylight.reference_geometry_enabled && !fixed_daylight.render_night_sky &&
                !fixed_daylight.moon.enabled && macro.daylight_environment == 1.0F,
            "accepted mountain daylight matches Scenic's retained sun convention");
    for (const auto* material : {&v1, &refined, &terrain})
        require(material->water_wet_normal == 0.0F && material->water_ripple_strength == 0.0F &&
                    material->daylight_environment == 0.0F,
                "legacy presets retain the generated-lighting and no-wind fallback");
    require(macro.daylight_exposure == 0.0F && macro.daylight_sun_scale == 0.45F &&
                macro.terrain_ambient == 2.0F && macro.terrain_direct == 1.0F &&
                macro.water_wet_normal == 1.0F && macro.water_ripple_strength == 0.025F &&
                macro.water_ripple_scale_m == 48.0F && macro.water_roughness == 0.14F &&
                macro.water_scatter_scale == 0.4F && macro.water_clarity == 0.0F &&
                macro.water_extinction_scale == 1.0F &&
                macro.water_shallow_extinction_boost == 3.0F &&
                macro.water_shallow_extinction_end_m == 16.0F,
            "macro preset uses accepted daylight and depth-limited absorption");
    const auto detail = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_wet_normal":1,"water_ripple_strength":0.12,"water_ripple_scale_m":48})",
        macro);
    require(detail.water_wet_normal == 1.0F && detail.water_ripple_strength == 0.12F &&
                fluid_25d_scenic_material_json(
                    fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(detail), v1)) ==
                    fluid_25d_scenic_material_json(detail),
            "water rendering-only tuning is bounded and roundtrips exactly");
    for (const auto* material : {&v1, &refined, &terrain})
        require(material->water_flow_agitation == 0.0F && material->water_rain_agitation == 0.0F,
                "legacy presets retain the no-agitation fallback");
    require(macro.water_flow_agitation == 1.0F && macro.water_rain_agitation == 1.0F,
            "mountain macro default uses the reviewed rough surface setting");
    const auto smooth = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_flow_agitation":0,"water_rain_agitation":0})",
        macro);
    require(smooth.water_flow_agitation == 0.0F && smooth.water_rain_agitation == 0.0F &&
                smooth.water_roughness == macro.water_roughness &&
                smooth.water_extinction_scale == macro.water_extinction_scale &&
                smooth.daylight_exposure == macro.daylight_exposure,
            "zero strengths restore retained shading without changing base optics or daylight");
    const auto agitation = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_flow_agitation":0.75,"water_rain_agitation":1})",
        macro);
    require(agitation.water_flow_agitation == 0.75F && agitation.water_rain_agitation == 1.0F &&
                agitation.water_extinction_scale == macro.water_extinction_scale &&
                agitation.daylight_exposure == macro.daylight_exposure &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(agitation), v1)) ==
                    fluid_25d_scenic_material_json(agitation),
            "agitation strengths roundtrip without replacing optics or lighting");
    for (const auto* material : {&v1, &refined, &terrain})
        require(material->water_shallow_extinction_boost == 0.0F &&
                    material->water_shallow_extinction_end_m == 2.0F,
                "legacy presets leave the shallow absorption boost disabled");
    const auto shallow_optics = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_extinction_scale":1,"water_shallow_extinction_boost":3,"water_shallow_extinction_end_m":2})",
        macro);
    require(shallow_optics.water_extinction_scale == 1.0F &&
                shallow_optics.water_shallow_extinction_boost == 3.0F &&
                shallow_optics.water_shallow_extinction_end_m == 2.0F &&
                shallow_optics.water_clarity == macro.water_clarity &&
                shallow_optics.water_reflection_scale == macro.water_reflection_scale &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(shallow_optics), v1)) ==
                    fluid_25d_scenic_material_json(shallow_optics),
            "custom shallow absorption roundtrips without replacing other material controls");
    const auto global_dark = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_extinction_scale":4,"water_shallow_extinction_boost":0})",
        macro);
    require(global_dark.water_extinction_scale == 4.0F &&
                global_dark.water_shallow_extinction_boost == 0.0F &&
                global_dark.water_shallow_extinction_end_m == 16.0F &&
                global_dark.water_clarity == macro.water_clarity &&
                global_dark.water_reflection_scale == macro.water_reflection_scale &&
                global_dark.daylight_environment == macro.daylight_environment,
            "the previous global-dark appearance remains an explicit material override");
    const auto lighting = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_exposure":-0.5,"daylight_sun_scale":0.45})",
        macro);
    require(lighting.daylight_exposure == -0.5F && lighting.daylight_sun_scale == 0.45F &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(lighting), v1)) ==
                    fluid_25d_scenic_material_json(lighting),
            "shared daylight exposure/balance is bounded, rendering-only and roundtrips");
    for (const auto* material : {&v1, &refined, &terrain, &macro})
        require(material->water_shallow_coverage_strength == 0.0F &&
                    material->water_shallow_coverage_end_m == 0.30F,
                "all presets retain accepted coverage; artist fade is opt-in");
    const auto coverage_fade = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_coverage_strength":0.75,"water_shallow_coverage_end_m":0.6,"film_end_m":0.3,"film_ground_mix":1})",
        macro);
    require(coverage_fade.water_shallow_coverage_strength == 0.75F &&
                coverage_fade.water_shallow_coverage_end_m == 0.6F &&
                coverage_fade.film_end_m == 0.3F && coverage_fade.film_ground_mix == 1.0F &&
                coverage_fade.water_roughness == macro.water_roughness &&
                coverage_fade.water_extinction_scale == macro.water_extinction_scale &&
                fluid_25d_scenic_material_json(fluid_25d_parse_scenic_material(
                    fluid_25d_scenic_material_json(coverage_fade), v1)) ==
                    fluid_25d_scenic_material_json(coverage_fade),
            "independent film/fade controls roundtrip without changing accepted optics");
    for (
        const auto* text :
        {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_coverage_strength":-0.01})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_coverage_strength":1.01})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_coverage_strength":true})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_coverage_end_m":0})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_coverage_end_m":1.01})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, macro)); },
                       "artist coverage fade rejects invalid or non-numeric bounds");
    for (const auto* text :
         {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_wet_normal":2})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_ripple_strength":0.31})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_flow_agitation":1.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rain_agitation":-0.1})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_rain_agitation":true})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","water_ripple_scale_m":0})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, macro)); },
                       "water rendering-only controls reject invalid bounds");
    for (
        const auto* text :
        {R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_extinction_boost":-0.1})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_extinction_boost":3.1})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_extinction_boost":true})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_extinction_end_m":0})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_shallow_extinction_end_m":16.1})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, macro)); },
                       "shallow absorption controls reject invalid/non-numeric bounds");
    require(fluid_25d_scenic_water_view("shaded") == 0U &&
                fluid_25d_scenic_water_view("coverage") == 9U &&
                fluid_25d_scenic_water_view("roughness") == 11U &&
                fluid_25d_scenic_water_view("rapid-activity") == 12U &&
                fluid_25d_scenic_water_view("rapid-foam") == 13U &&
                fluid_25d_scenic_water_view("no-rapid-foam") == 14U &&
                fluid_25d_scenic_water_view("cascade-weight") == 15U &&
                fluid_25d_scenic_water_view("landing-activity") == 16U &&
                fluid_25d_scenic_water_view("no-landing-foam") == 17U,
            "water component diagnostic slots remain stable");
    for (const auto* material : {&v1, &refined, &terrain, &macro})
        require(material->film_begin_m == 0.02F && material->film_end_m == 0.12F &&
                    material->film_roughness == 0.65F && material->film_ground_mix == 0.75F,
                "every Scenic preset uses the accepted wet-ground treatment");
    require_throws([] { static_cast<void>(fluid_25d_scenic_water_view("unknown")); },
                   "unknown water components fail closed");
    for (const auto* text :
         {R"({"schema":"cubey.fluid25d.scenic-material.v2","film_begin_m":0.05,"film_end_m":0.05})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_environment":2})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_environment":0.5})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_exposure":4.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_exposure":-6.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_sun_scale":-0.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","daylight_sun_scale":2.01})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","film_begin_m":0.13})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","film_roughness":0.81})",
          R"({"schema":"cubey.fluid25d.scenic-material.v2","film_ground_mix":1.1})"})
        require_throws([&] { static_cast<void>(fluid_25d_parse_scenic_material(text, macro)); },
                       "bounded film controls and ordered transition required");
    for (const char* source : {"--fluid25d-recording", "--fluid25d-stream"}) {
        const auto water =
            parse_project({"fluid_25d", source, "fixture", "--fluid25d-native-presentation",
                           "scenic", "--fluid25d-scenic-water-view", "coverage"});
        require(water.native_scenic_water_view == "coverage" &&
                    water.native_scenic_material == "refined",
                "recording/stream select shading without replacing material or backend");
    }
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                             "--fluid25d-scenic-water-film", "rough-film"}));
        },
        "retired film selector is rejected rather than silently ignored");
    require_throws(
        [] {
            static_cast<void>(
                parse_project({"fluid_25d", "--fluid25d-scenic-water-view", "coverage"}));
        },
        "water controls require native frontend");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                             "--fluid25d-native-presentation", "scenic",
                                             "--fluid25d-scenic-water-view", "depth-bands",
                                             "--fluid25d-scenic-terrain-view", "albedo"}));
        },
        "conflicting water and terrain component controls fail closed");
    require(macro.terrain_diffuse_convolution == 1.0F && macro.terrain_ambient_softening == 0.0F &&
                macro.water_clarity == 0.0F && macro.water_extinction_scale == 1.0F &&
                macro.water_shallow_extinction_boost == 3.0F &&
                macro.water_shallow_extinction_end_m == 16.0F && macro.water_roughness == 0.14F &&
                terrain.terrain_diffuse_convolution == 0.0F &&
                refined.terrain_specular_scale == 1.0F && v1.terrain_shadow_scale == 1.0F,
            "macro uses the accepted lighting recipe while legacy terrain/reference values remain");
    require(fluid_25d_scenic_material_json(
                fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(macro), v1)) ==
                fluid_25d_scenic_material_json(macro),
            "macro tuning roundtrips exactly");
    require(terrain.terrain_normal_strength == 0.12F && terrain.terrain_material_blend == 1.0F &&
                terrain.terrain_ambient_softening == 1.0F &&
                terrain.water_clarity == refined.water_clarity &&
                terrain.water_roughness == refined.water_roughness &&
                refined.terrain_normal_strength == -1.0F && v1.terrain_material_blend == 0.0F,
            "terrain is opt-in, preserves water controls, and retains legacy material presets");
    require(fluid_25d_scenic_material_json(
                fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(terrain), v1)) ==
                fluid_25d_scenic_material_json(terrain),
            "terrain controls roundtrip exactly");
    require(fluid_25d_scenic_terrain_view("shaded") == 0U &&
                fluid_25d_scenic_terrain_view("face-normal") == 11U,
            "terrain component slots are stable");
    require_throws([] { static_cast<void>(fluid_25d_scenic_terrain_view("unknown")); },
                   "unknown terrain views fail closed");
    require(fluid_25d_scenic_terrain_view("no-specular") == 12U &&
                fluid_25d_scenic_terrain_view("no-shadows") == 13U &&
                fluid_25d_scenic_terrain_view("specular-only") == 14U,
            "macro diagnostic slots append without changing old component indices");
    require(v1.wet_roughness == 0.38F && v1.water_clarity == 0.0F &&
                v1.water_scatter_lighting == 0.0F && refined.wet_roughness > v1.wet_roughness,
            "V1 remains available and refined wet ground is less glossy");
    const auto roundtrip =
        fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(refined), v1);
    require(fluid_25d_scenic_material_json(roundtrip) == fluid_25d_scenic_material_json(refined),
            "effective material settings roundtrip exactly");
    const auto minimum = fluid_25d_parse_scenic_material(
        R"({"schema":"cubey.fluid25d.scenic-material.v2","wet_roughness":0.2,"water_roughness":0.08})",
        v1);
    require(minimum.wet_roughness == 0.2F && minimum.water_roughness == 0.08F &&
                minimum.wet_darkening == v1.wet_darkening,
            "decimal endpoints are accepted and omitted controls inherit the selected profile");
    const auto minimum_roundtrip =
        fluid_25d_parse_scenic_material(fluid_25d_scenic_material_json(minimum), v1);
    require(minimum_roundtrip.water_roughness == minimum.water_roughness &&
                minimum_roundtrip.wet_roughness == minimum.wet_roughness,
            "logged decimal endpoint settings roundtrip without float-boundary rejection");
    for (
        const auto* text :
        {"{}", "[]", R"({"schema":"unknown"})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_clarity":true})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","wet_roughness":0.19})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_clarity":1.1})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","water_clarity":1e400})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","rainfall":10})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_normal_strength":-0.5})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_material_blend":1.1})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_ambient_softening":2})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_diffuse_convolution":2})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_specular_scale":-1})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_shadow_scale":2})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","terrain_slope_color_scale":2})",
         R"({"schema":"cubey.fluid25d.scenic-material.v2","wet_roughness":0.4,"wet_roughness":0.7})"}) {
        require_throws(
            [&] { static_cast<void>(fluid_25d_parse_scenic_material(text, v1)); },
            "malformed/unknown/duplicate/non-finite or out-of-range tuning fails closed");
    }
    require_throws(
        [&] { static_cast<void>(fluid_25d_parse_scenic_material(std::string(16385U, ' '), v1)); },
        "tuning document memory is bounded");
    require_throws([] { static_cast<void>(fluid_25d_scenic_material("unknown")); },
                   "unknown material profiles fail closed");
    TerrainFixture fixture;
    const auto tuning_file = fixture.root / "scenic.json";
    {
        std::ofstream stream(tuning_file);
        stream << fluid_25d_scenic_material_json(refined);
    }
    require(fluid_25d_scenic_material_json(fluid_25d_load_scenic_material(tuning_file, v1)) ==
                fluid_25d_scenic_material_json(refined),
            "bounded tuning file loads the same effective settings as the parser");
    const auto link = fixture.root / "scenic-link.json";
    std::filesystem::create_symlink(tuning_file, link);
    require_throws([&] { static_cast<void>(fluid_25d_load_scenic_material(link, v1)); },
                   "tuning rejects symbolic-link files");
    require_throws([&] { static_cast<void>(fluid_25d_load_scenic_material(fixture.root, v1)); },
                   "tuning rejects directories");
    for (const char* source : {"--fluid25d-recording", "--fluid25d-external-session"}) {
        const auto parsed = parse_project(
            {"fluid_25d", source, "fixture", "--fluid25d-native-presentation", "scenic",
             "--fluid25d-scenic-material", "refined", "--fluid25d-scenic-tuning", "study.json"});
        require(parsed.native_scenic_material == "refined" &&
                    parsed.native_scenic_tuning_path == "study.json" &&
                    parsed.simulation.solver == Fluid25DSolver::VirtualPipes,
                "recording/live share material overrides without changing the solver");
    }
    for (const char* style : {"original", "motion", "readable"})
        require_throws(
            [&] {
                static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                                 "--fluid25d-native-presentation", style,
                                                 "--fluid25d-scenic-material", "refined"}));
            },
            "non-Scenic startup cannot silently ignore material overrides");
    require_throws(
        [] { static_cast<void>(parse_project({"fluid_25d", "--fluid25d-scenic-material", "v1"})); },
        "builtin cannot accidentally consume Scenic controls");
    const auto study = parse_project(
        {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-presentation", "scenic",
         "--fluid25d-scenic-material", "terrain", "--fluid25d-scenic-terrain-view", "terrain-only",
         "--fluid25d-native-camera-yaw", "0.2"});
    const auto surface_study = parse_project(
        {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-presentation", "scenic",
         "--fluid25d-scenic-surface-source", "heightfield.json", "--fluid25d-scenic-surface-mode",
         "climate"});
    require(surface_study.native_scenic_surface_mode == "climate" &&
                surface_study.native_scenic_surface_source == "heightfield.json" &&
                surface_study.simulation.solver == Fluid25DSolver::VirtualPipes,
            "surface study changed numerical defaults");
    for (const char* source : {"--fluid25d-recording", "--fluid25d-external-session"}) {
        const auto bound =
            parse_project({"fluid_25d", source, "fixture", "--fluid25d-native-presentation",
                           "scenic", "--fluid25d-scenic-surface-grid", "grid.json",
                           "--fluid25d-scenic-surface-mode", "correlated"});
        require(bound.native_scenic_surface_grid == "grid.json" &&
                    bound.native_scenic_surface_mode == "correlated" &&
                    bound.simulation.solver == Fluid25DSolver::VirtualPipes,
                "world-coordinate binding must support live and recorded views without numerical "
                "changes");
    }
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-stream", "fixture",
                                 "--fluid25d-native-presentation", "scenic",
                                 "--fluid25d-scenic-surface-grid", "grid.json"});
        },
        "legacy streams lack validated live geometry binding");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-native-presentation", "scenic",
                                 "--fluid25d-scenic-surface-grid", "grid.json",
                                 "--fluid25d-scenic-surface-source", "old.json"});
        },
        "mixed source bindings accepted");
    for (const char* source : {"--fluid25d-stream", "--fluid25d-external-session"})
        require_throws(
            [&] {
                (void)parse_project({"fluid_25d", source, "fixture",
                                     "--fluid25d-native-presentation", "scenic",
                                     "--fluid25d-scenic-surface-source", "heightfield.json"});
            },
            "surface study cannot assume missing live source-coordinate metadata");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-native-presentation", "scenic",
                                 "--fluid25d-scenic-surface-mode", "climate"});
        },
        "unbound climate mode was accepted");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-native-presentation", "readable",
                                 "--fluid25d-scenic-surface-source", "heightfield.json"});
        },
        "surface source cannot be silently ignored outside Scenic");
    require(study.native_scenic_terrain_view == "terrain-only" &&
                study.native_camera_yaw_radians == 0.2F &&
                study.native_camera_sweep_radians == 0.0F,
            "terrain diagnostic and heading remain render-only options");
    require(study.native_camera_motion == "legacy", "legacy capture motion default changed");
    const auto eased_capture =
        parse_project({"fluid_25d", "--fluid25d-recording", "fixture", "--headless", "--capture",
                       "video", "--frames", "480", "--fluid25d-native-camera-sweep", "0.26",
                       "--fluid25d-native-camera-motion", "eased"});
    require(eased_capture.native_camera_motion == "eased" &&
                eased_capture.simulation.solver == Fluid25DSolver::VirtualPipes,
            "capture easing changed numerical policy");
    require(eased_capture.recording_opening_frames == 0U &&
                eased_capture.recording_closing_frames == 0U &&
                eased_capture.native_camera_vertical_shift == 0.0F,
            "editorial capture controls must remain opt-in");
    const auto editorial =
        parse_project({"fluid_25d", "--fluid25d-recording", "fixture", "--headless", "--capture",
                       "video", "--frames", "480", "--fluid25d-recording-opening-frames", "24",
                       "--fluid25d-recording-closing-frames", "120",
                       "--fluid25d-native-camera-vertical-shift", "0.06"});
    require(editorial.recording_opening_frames == 24U &&
                editorial.recording_closing_frames == 120U &&
                editorial.native_camera_vertical_shift == 0.06F &&
                editorial.simulation.solver == Fluid25DSolver::VirtualPipes,
            "endpoint holds and film shift changed numerical defaults");
    using fluid_25d_project_config_detail::recording_source_frame;
    for (std::uint32_t i = 0U; i < 480U; ++i) {
        const auto mapped = recording_source_frame(i, 480U, 24U, 120U);
        require(mapped == std::min(i > 24U ? i - 24U : 0U, 335U),
                "editorial source frame mapping must be exact and monotone");
        require(recording_source_frame(i, 480U, 0U, 0U) == i,
                "zero holds must preserve the original capture timeline");
    }
    require_throws([] { (void)recording_source_frame(0U, 480U, 24U, 455U); },
                   "endpoint holds cannot remove the active sequence");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-recording-closing-frames", "120"});
        },
        "animated holds cannot change normal GUI playback");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-external-session", "fixture",
                                 "--headless", "--capture", "video", "--frames", "480",
                                 "--fluid25d-recording-closing-frames", "120"});
        },
        "animated holds must not attach to a live solver");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-native-camera-vertical-shift", "0.16"});
        },
        "film shift must be bounded");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-native-camera-motion", "eased"});
        },
        "eased camera accepted outside video capture");
    const auto camera_recipe = parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                              "--fluid25d-native-camera-target-x", "0.25",
                                              "--fluid25d-native-camera-target-z", "0.75",
                                              "--fluid25d-native-camera-span-m", "900"});
    require(camera_recipe.native_camera_target_x == 0.25F &&
                camera_recipe.native_camera_target_z == 0.75F &&
                camera_recipe.native_camera_span_m == 900.0F &&
                camera_recipe.simulation.solver == Fluid25DSolver::VirtualPipes,
            "observation recipe changed numerical defaults");
    const auto shadow_control = parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                               "--fluid25d-native-presentation", "scenic",
                                               "--fluid25d-native-shadow-projection", "scene",
                                               "--fluid25d-native-shadow-extent", "4096"});
    require(shadow_control.native_shadow_projection == "scene" &&
                shadow_control.native_shadow_extent == 4096U &&
                shadow_control.simulation.solver == Fluid25DSolver::VirtualPipes,
            "shadow control changed numerical defaults");
    require(shadow_control.native_terrain_context == "off", "Fluid must default to patch-only");
    require(shadow_control.environment_mode == "fixed", "environment default changed");
    const auto daynight = parse_project(
        {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-presentation", "scenic",
         "--fluid25d-environment", "shared", "--time-of-day-mode", "solar", "--time-hours", "12"});
    require(daynight.environment_mode == "shared" && daynight.atmosphere.time_hours == 12.0F,
            "clear day/night controls were removed with clouds");
    for (const auto* flag :
         {"--clouds", "--fluid25d-cloud-background", "--no-fluid25d-cloud-reflections"})
        require_throws(
            [&] {
                (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                     "--fluid25d-native-presentation", "scenic",
                                     "--fluid25d-environment", "shared", flag});
            },
            "retired elevated-view cloud flag accepted");
    require(parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                           "--fluid25d-native-presentation", "scenic",
                           "--fluid25d-native-terrain-context", "off"})
                    .native_terrain_context == "off",
            "legacy explicit context off must remain compatible");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                 "--fluid25d-native-presentation", "scenic",
                                 "--fluid25d-native-terrain-context", "on"});
        },
        "retired wider Fluid terrain must reject explicitly");
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"fluid_25d", "--fluid25d-native-shadow-projection", "scene"},
             {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-shadow-extent",
              "4096"},
             {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-presentation",
              "scenic", "--fluid25d-native-shadow-extent", "3072"}})
        require_throws([&] { (void)parse_project(args); }, "invalid shadow control accepted");
    for (const auto& args : std::vector<std::vector<std::string>>{
             {"fluid_25d", "--fluid25d-native-camera-target-x", "0.5"},
             {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-camera-target-x",
              "0.5"},
             {"fluid_25d", "--fluid25d-recording", "fixture", "--fluid25d-native-camera-target-x",
              "1.1", "--fluid25d-native-camera-target-z", "0.5", "--fluid25d-native-camera-span-m",
              "900"}})
        require_throws([&] { (void)parse_project(args); }, "invalid camera recipe accepted");
    for (const auto* flag : {"--fluid25d-scenic-terrain-view", "--fluid25d-native-camera-sweep"})
        require_throws(
            [&] {
                static_cast<void>(parse_project(
                    {"fluid_25d", flag,
                     flag == std::string_view("--fluid25d-scenic-terrain-view") ? "albedo"
                                                                                : "0.4"}));
            },
            "terrain diagnostics and sweeps cannot silently affect builtin simulations");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                             "--fluid25d-native-presentation", "readable",
                                             "--fluid25d-scenic-terrain-view", "albedo"}));
        },
        "terrain components require Scenic");
    require_throws(
        [] {
            static_cast<void>(parse_project({"fluid_25d", "--fluid25d-recording", "fixture",
                                             "--fluid25d-native-camera-sweep", "0.4"}));
        },
        "sweep requires a headless recorded video");
}

void test_bank_comparison_controls() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DCatchmentRenderOptions render;
    render.native_presentation = 2U;
    render.native_surface_highlights = true;
    render.terrain_height_scale = 3.0F;
    render.native_bilinear_water = true;
    for (unsigned repeat = 0; repeat < 5U; ++repeat) {
        for (auto mode : {Fluid25DBankView::Reference, Fluid25DBankView::Bspline2x,
                          Fluid25DBankView::MarchingSquares}) {
            apply_fluid_25d_bank_view(render, mode, true);
            require(!render.native_bilinear_water &&
                        render.native_bspline_surface == (mode == Fluid25DBankView::Bspline2x) &&
                        render.native_display_coverage ==
                            (mode == Fluid25DBankView::MarchingSquares),
                    "bank modes must be mutually exclusive");
            require(render.native_presentation == 2U && render.native_surface_highlights &&
                        render.terrain_height_scale == 3.0F,
                    "bank switching must not change independent presentation settings");
            require(fluid_25d_bank_view(fluid_25d_bank_view_name(mode)) == mode,
                    "bank view names round trip");
        }
    }
    apply_fluid_25d_bank_view(render, Fluid25DBankView::Bspline2x, false);
    require_throws(
        [&] { apply_fluid_25d_bank_view(render, Fluid25DBankView::MarchingSquares, false); },
        "missing coverage must reject before touching mode state");
    require(render.native_bspline_surface && !render.native_display_coverage &&
                render.native_surface_subdivision == 2U,
            "failed switch must preserve the prior mode");
    require_throws([] { (void)fluid_25d_bank_view("strong"); },
                   "strong smoothing is not a supported bank preset");
    const std::array<double, 6> source{0., 10., 20., 30., 40., 50.};
    const std::array<double, 3> masks{10., 20., 30.};
    Fluid25DBankComparisonWindow window(source, masks);
    require(window.first_s() == 10. && window.last_saved_s() == 30. && window.end_s() < 40. &&
                window.frame_count() == 3U && window.contains(39.9) && !window.contains(40.),
            "comparison holds final mask but never selects the next unmasked native field");
    require(window.map(0., false).time_s == 10. && window.map(100., false).at_end &&
                window.map(100., false).time_s == window.end_s(),
            "comparison seek/advance must clamp safely");
    require(window.map(40., true).looped && window.map(40., true).time_s == 10. &&
                window.map(75., true).time_s == 15.,
            "explicit replay must preserve overrun and bounded timing");
    require(window.map(window.end_s(), true).time_s == 10.,
            "looping the displayed end must restart cleanly");
    require_throws([&] { (void)window.map(std::numeric_limits<double>::quiet_NaN(), false); },
                   "nonfinite window time rejected");
    const std::array<double, 2> gaps{10., 30.};
    require_throws([&] { Fluid25DBankComparisonWindow invalid(source, gaps); },
                   "sparse masks cannot advertise continuous comparison playback");
    const std::array<double, 2> wrong{10., 21.};
    require_throws([&] { Fluid25DBankComparisonWindow invalid(source, wrong); },
                   "comparison requires exact saved mask times");
    const auto ordinary = parse_project({"fluid_25d", "--fluid25d-recording", "recording.json"});
    require(!ordinary.bank_comparison && !ordinary.native_bank_view &&
                !ordinary.bank_comparison_loop && ordinary.bank_comparison_cycle_frames == 0U,
            "comparison controls remain opt-in");
    const auto configured = parse_project(
        {"fluid_25d", "--fluid25d-recording", "recording.json",
         "--fluid25d-native-display-coverage", "coverage.json", "--fluid25d-bank-comparison",
         "--fluid25d-native-bank-view", "bspline-2x", "--fluid25d-bank-comparison-loop"});
    require(configured.bank_comparison && configured.bank_comparison_loop &&
                configured.simulation.solver == Fluid25DSolver::VirtualPipes,
            "comparison presets cannot change numerical defaults");
    require_throws([] { (void)parse_project({"fluid_25d", "--fluid25d-bank-comparison"}); },
                   "comparison requires a recording and sidecar");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "x",
                                 "--fluid25d-native-bank-view", "marching-squares"});
        },
        "marching squares requires a sidecar");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-recording", "x",
                                 "--fluid25d-native-bank-view", "reference",
                                 "--fluid25d-native-water-sampling", "bspline"});
        },
        "conflicting old/new mode flags rejected");
    require_throws(
        [] {
            (void)parse_project(
                {"fluid_25d", "--fluid25d-recording", "x", "--fluid25d-bank-comparison-loop"});
        },
        "implicit comparison loops rejected");
    require_throws(
        [] {
            (void)parse_project({"fluid_25d", "--fluid25d-stream", "x",
                                 "--fluid25d-native-display-coverage", "y",
                                 "--fluid25d-bank-comparison"});
        },
        "live comparison coverage cannot be implied");
}

void test_catchment_far_plane_geometry() {
    using cubey::math::Vec3;
    using namespace cubey::projects::fluid::fluid_25d;

    struct SceneBounds {
        Vec3 minimum;
        Vec3 maximum;
        Vec3 target;
        float minimum_orbit_m;
        float maximum_orbit_m;
        std::array<float, 3> observation_distances_m;
    };
    std::vector<SceneBounds> scenes{
        SceneBounds{
            .minimum = {-3825.0F, 20.0F, -3825.0F},
            .maximum = {3825.0F, 920.0F, 3825.0F},
            .target = {-1125.0F, 370.0F, 315.0F},
            .minimum_orbit_m = 960.0F,
            .maximum_orbit_m = 30600.0F,
            .observation_distances_m = {3000.0F, 1200.0F, 11475.0F},
        },
        SceneBounds{
            .minimum = {-45.0F, -250.0F, -30.0F},
            .maximum = {45.0F, 1750.0F, 30.0F},
            .target = {30.0F, 200.0F, -20.0F},
            .minimum_orbit_m = 8.0F,
            .maximum_orbit_m = 48.0F,
            .observation_distances_m = {12.0F, 24.0F, 40.0F},
        },
        SceneBounds{
            .minimum = {-20000.0F, -50.0F, -200.0F},
            .maximum = {20000.0F, 50.0F, 200.0F},
            .target = {15000.0F, 0.0F, 0.0F},
            .minimum_orbit_m = 900.0F,
            .maximum_orbit_m = 160000.0F,
            .observation_distances_m = {1200.0F, 3000.0F, 30000.0F},
        },
        SceneBounds{
            .minimum = {-100.0F, -10.0F, -50.0F},
            .maximum = {100.0F, 15.0F, 50.0F},
            .target = {80.0F, 5.0F, 40.0F},
            .minimum_orbit_m = 8.0F,
            .maximum_orbit_m = 400.0F,
            .observation_distances_m = {24.0F, 96.0F, 200.0F},
        },
    };

    constexpr std::uint32_t rain_grid_width = 512U;
    constexpr std::uint32_t rain_grid_height = 512U;
    constexpr float rain_cell_size_m = 30.0F;
    constexpr float rain_domain_extent_m =
        static_cast<float>(rain_grid_width - 1U) * rain_cell_size_m;
    constexpr float rain_minimum_orbit_m = 960.0F;
    constexpr float rain_maximum_orbit_m = 61320.0F;
    constexpr float rain_travel_home_distance_m = 3000.0F;
    constexpr float rain_collection_home_distance_m = 1200.0F;
    Fluid25DScenarioData rain;
    rain.width = rain_grid_width;
    rain.height = rain_grid_height;
    rain.cell_size_m = rain_cell_size_m;
    rain.terrain_height_m.assign(static_cast<std::size_t>(rain_grid_width) * rain_grid_height,
                                 0.0F);
    rain.terrain_height_m.front() = -10.0F;
    rain.terrain_height_m.back() = 300.0F;
    const std::size_t rain_travel_cell =
        fluid_25d_scenario_index(rain_grid_width, rain_grid_height, 138U, 234U);
    const std::size_t rain_collection_cell =
        fluid_25d_scenario_index(rain_grid_width, rain_grid_height, 58U, 251U);
    rain.terrain_height_m[rain_travel_cell] = 125.0F;
    rain.terrain_height_m[rain_collection_cell] = 42.0F;
    const auto [rain_minimum, rain_maximum] =
        std::minmax_element(rain.terrain_height_m.begin(), rain.terrain_height_m.end());
    const float rain_overview_distance_m =
        std::max(rain_domain_extent_m * 1.5F, (*rain_maximum - *rain_minimum) * 1.5F + 16.0F);
    const auto rain_overview_limits = fluid_25d_catchment_orbit_limits(
        Fluid25DScenario::HillsideRainStudy, rain_domain_extent_m, rain_domain_extent_m);
    require_close(rain_domain_extent_m, 15330.0, kDepthToleranceM,
                  "512-cell rain crop should retain its 15,330 m domain extent");
    require_close(rain_overview_limits.minimum_distance_m, rain_minimum_orbit_m, 0.001,
                  "512-cell rain presets should retain their 960 m minimum orbit");
    require_close(rain_overview_limits.maximum_distance_m, rain_maximum_orbit_m, 0.001,
                  "512-cell rain presets should retain their 61,320 m maximum orbit");
    require_close(rain_overview_distance_m, 22995.0, kDepthToleranceM,
                  "rain overview home distance should retain its 22,995 m preset");
    const Vec3 rain_overview_target{0.0F, 0.5F * (*rain_minimum + *rain_maximum), 0.0F};
    const Vec3 rain_travel_target{-3525.0F, rain.terrain_height_m[rain_travel_cell], -645.0F};
    const Vec3 rain_collection_target{-5925.0F, rain.terrain_height_m[rain_collection_cell],
                                      -135.0F};
    require(rain_travel_target.x == -3525.0F && rain_travel_target.z == -645.0F &&
                rain_collection_target.x == -5925.0F && rain_collection_target.z == -135.0F &&
                rain_overview_target.x == 0.0F && rain_overview_target.z == 0.0F,
            "rain overview, travel, and collection should retain their exact world targets");
    require(rain_travel_home_distance_m == 3000.0F && rain_collection_home_distance_m == 1200.0F &&
                rain_overview_distance_m >= rain_minimum_orbit_m &&
                rain_overview_distance_m <= rain_maximum_orbit_m,
            "rain overview, travel, and collection should retain their legal home distances");
    const float rain_terrain_minimum_y = std::min(*rain_minimum, *rain_maximum);
    const float rain_terrain_maximum_y = std::max(*rain_minimum, *rain_maximum);
    const auto add_rain_scene = [&](Vec3 target, float home_distance_m) {
        scenes.push_back({
            .minimum = {-0.5F * rain_domain_extent_m, rain_terrain_minimum_y,
                        -0.5F * rain_domain_extent_m},
            .maximum = {0.5F * rain_domain_extent_m, rain_terrain_maximum_y,
                        0.5F * rain_domain_extent_m},
            .target = target,
            .minimum_orbit_m = rain_minimum_orbit_m,
            .maximum_orbit_m = rain_maximum_orbit_m,
            .observation_distances_m = {rain_travel_home_distance_m,
                                        rain_collection_home_distance_m, home_distance_m},
        });
    };
    add_rain_scene(rain_overview_target, rain_overview_distance_m);
    add_rain_scene(rain_travel_target, rain_travel_home_distance_m);
    add_rain_scene(rain_collection_target, rain_collection_home_distance_m);

    for (const SceneBounds& scene : scenes) {
        const float far_plane = fluid_25d_catchment_far_plane(scene.maximum_orbit_m, scene.minimum,
                                                              scene.maximum, scene.target);
        require(std::isfinite(far_plane) && far_plane > scene.maximum_orbit_m,
                "catchment far plane should be finite and beyond every legal orbit distance");
        std::array<float, 5> distances{
            scene.minimum_orbit_m,
            scene.observation_distances_m[0],
            scene.observation_distances_m[1],
            scene.observation_distances_m[2],
            scene.maximum_orbit_m,
        };
        for (const float distance_m : distances) {
            require(distance_m >= scene.minimum_orbit_m && distance_m <= scene.maximum_orbit_m,
                    "camera observation preset should remain inside legal zoom limits");
            for (std::uint32_t step = 0U; step <= 16U; ++step) {
                const float orbit_distance_m =
                    scene.minimum_orbit_m + (scene.maximum_orbit_m - scene.minimum_orbit_m) *
                                                (static_cast<float>(step) / 16.0F);
                for (std::uint32_t corner = 0U; corner < 8U; ++corner) {
                    const Vec3 point{
                        (corner & 1U) == 0U ? scene.minimum.x : scene.maximum.x,
                        (corner & 2U) == 0U ? scene.minimum.y : scene.maximum.y,
                        (corner & 4U) == 0U ? scene.minimum.z : scene.maximum.z,
                    };
                    const float dx = point.x - scene.target.x;
                    const float dy = point.y - scene.target.y;
                    const float dz = point.z - scene.target.z;
                    const float corner_radius_m = std::hypot(std::hypot(dx, dy), dz);
                    // Every unit forward vector has a corner projection no
                    // larger than the Euclidean camera-to-corner distance,
                    // which is at most orbit distance plus target radius.
                    const float maximum_forward_depth_m = orbit_distance_m + corner_radius_m;
                    require(maximum_forward_depth_m < far_plane,
                            "terrain AABB corner must remain before far clip at every legal orbit");
                }
            }
        }
    }

    const Vec3 minimum{-1.0F, -2.0F, -3.0F};
    const Vec3 maximum{1.0F, 2.0F, 3.0F};
    require_throws(
        [&] { static_cast<void>(fluid_25d_catchment_far_plane(0.0F, minimum, maximum, Vec3{})); },
        "catchment far-plane helper should reject a nonpositive orbit limit");
    require_throws(
        [&] {
            static_cast<void>(fluid_25d_catchment_far_plane(std::numeric_limits<float>::infinity(),
                                                            minimum, maximum, Vec3{}));
        },
        "catchment far-plane helper should reject nonfinite orbit limits");
    require_throws(
        [&] { static_cast<void>(fluid_25d_catchment_far_plane(10.0F, maximum, minimum, Vec3{})); },
        "catchment far-plane helper should reject reversed scene bounds");
    require_throws(
        [&] {
            static_cast<void>(fluid_25d_catchment_far_plane(
                10.0F, minimum, maximum,
                Vec3{0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F}));
        },
        "catchment far-plane helper should reject nonfinite targets");
    require_throws(
        [&] {
            const float largest = std::numeric_limits<float>::max();
            static_cast<void>(fluid_25d_catchment_far_plane(largest, Vec3{-largest, 0.0F, 0.0F},
                                                            Vec3{largest, 0.0F, 0.0F}, Vec3{}));
        },
        "catchment far-plane helper should reject bounds whose conservative depth overflows");
}

void test_hillside_supply_and_depth_cues() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DHillsideSupply reference;
    Fluid25DHillsideSupply response(true);
    for (const double time : {0.0, 3598.0, 3600.0, 5398.0, 5400.0, 7198.0, 7200.0, 9000.0}) {
        require_close(reference.rate(time, 100.0F), 100.0, 0.0, "reference must stay constant");
        const double expected = time < 3600.0 || time >= 7200.0 ? 100.0
                                : time < 5400.0                 ? 150.0
                                                                : 50.0;
        require_close(response.rate(time, 100.0F), expected, 0.0,
                      "response uses half-open physical phases");
    }
    response.queue_preset(50.0F);
    require(response.pending() == 50.0F && !response.manual(),
            "queued supply waits for fixed step");
    require_close(response.rate(3600.0, 100.0F), 150.0, 0.0, "queue doesn't mutate paused supply");
    response.apply_pending();
    require(response.manual() && !response.pending(),
            "fixed step applies and consumes manual queue");
    require_close(response.rate(7200.0, 100.0F), 50.0, 0.0,
                  "manual override survives scripted boundary");
    response.queue_preset(150.0F);
    response.reset();
    require(!response.manual() && !response.pending(),
            "reset clears applied and pending manual forcing");
    require_close(response.rate(0.0, 100.0F), 100.0, 0.0, "reset restarts response reference");
    require_throws([&] { response.queue_preset(0.0F); }, "continuous presets never permit zero");
    require_throws([&] { static_cast<void>(response.rate(-1.0, 100.0F)); },
                   "supply rejects invalid clock");
    const auto paced_sequence = [](float refresh) {
        Fluid25DWindowedPacing pacing(2.0F, 8.0F);
        Fluid25DHillsideSupply supply(true);
        std::vector<float> rates;
        while (rates.size() < 3600U) {
            const auto frame = pacing.advance(1.0F / refresh, false);
            for (std::uint32_t step = 0; step < frame.fixed_step_count && rates.size() < 3600U;
                 ++step)
                rates.push_back(supply.rate(static_cast<double>(rates.size()) * 2.0, 100.0F));
        }
        require(pacing.dropped_backlog_frames() == 0U, "normal pacing must not drop supply steps");
        return rates;
    };
    const auto rates = paced_sequence(30.0F);
    require(rates == paced_sequence(60.0F), "source sequence must be independent of render FPS");
    double integral = 0.0;
    for (float rate : rates)
        integral += static_cast<double>(rate) * 2.0;
    require_close(integral, 720000.0, 0.0, "two-hour response equals constant reference input");
    Fluid25DWindowedPacing paused(2.0F, 8.0F);
    require(paused.advance(1.0F, true).fixed_step_count == 0U, "pause never consumes supply steps");
    for (std::size_t index = 0; index < kFluid25DHillsideDepthLegendM.size(); ++index)
        require_close(
            fluid_25d_hillside_depth_palette_position(kFluid25DHillsideDepthLegendM[index]),
            static_cast<double>(index) / 3.0, 0.000001, "fixed decade depth legend matches shader");
    require_close(fluid_25d_hillside_depth_palette_position(100.0F), 1.0, 0.0, "deep scale clamps");
    require_close(fluid_25d_hillside_depth_palette_position(0.0001F), 0.0, 0.0,
                  "thin scale clamps");
}

void test_forcing_cube_footprints_and_shared_faces() {
    using namespace cubey::projects::fluid::fluid_25d;
    Fluid25DConfig config;
    config.scenario = Fluid25DScenario::HillsideFlowStudy;
    config.grid_width = 4U;
    config.grid_height = 3U;
    config.cell_size_m = 30.0F;
    Fluid25DScenarioData scenario;
    scenario.width = 4U;
    scenario.height = 3U;
    scenario.cell_size_m = 30.0F;
    scenario.terrain_height_m.resize(12U);
    for (std::size_t i = 0U; i < 12U; ++i)
        scenario.terrain_height_m[i] = static_cast<float>(i) * 10.0F - 20.0F;
    scenario.source_depth_rate_m_per_s.assign(12U, 0.0F);
    scenario.sink_depth_rate_m_per_s.assign(12U, 0.0F);
    scenario.source_depth_rate_m_per_s[5] = 0.1F;
    scenario.source_depth_rate_m_per_s[6] = 0.2F;
    scenario.sink_depth_rate_m_per_s[0] = 0.3F;
    scenario.sink_depth_rate_m_per_s[11] = 0.4F;
    const auto before = scenario;
    const auto cubes = fluid_25d_forcing_cubes(config, scenario);
    require(cubes.size() == 4U && cubes[0].center_kind == std::array<float, 4>{-15, 30, 0, 0} &&
                cubes[1].center_kind == std::array<float, 4>{15, 40, 0, 0} &&
                cubes[2].center_kind == std::array<float, 4>{-45, -20, -30, 1},
            "cube counts and terrain-anchored centers must match actual source/sink cells");
    require(cubes[0].neighbor_present == std::array<float, 4>{0, 1, 0, 0} &&
                cubes[1].neighbor_present == std::array<float, 4>{1, 0, 0, 0} &&
                cubes[0].neighbor_bed[1] == 40 && cubes[1].neighbor_bed[0] == 30 &&
                cubes[2].neighbor_present == std::array<float, 4>{0, 0, 0, 0},
            "cube neighbors must use matching forcing masks without wrapping crop edges");
    require(scenario.terrain_height_m == before.terrain_height_m &&
                scenario.source_depth_rate_m_per_s == before.source_depth_rate_m_per_s &&
                scenario.sink_depth_rate_m_per_s == before.sink_depth_rate_m_per_s,
            "cube construction must leave terrain and hydraulic forcing untouched");
    config.scenario = Fluid25DScenario::RiverCatchment;
    require(fluid_25d_forcing_cubes(config, scenario).empty(), "default River V0 stays unchanged");
    config.scenario = Fluid25DScenario::NaturalFlowStudy;
    scenario.sink_depth_rate_m_per_s.assign(12U, 0.0F);
    scenario.outlet_cell = 11U;
    require(fluid_25d_forcing_cubes(config, scenario).size() == 2U,
            "an observed expected exit is not a drain forcing region");
    using Ranges = std::array<std::array<float, 2>, 2>;
    require(fluid_25d_forcing_cube_side_ranges(2, 0, true) == Ranges{{{-1, -1}, {1, 1}}} &&
                fluid_25d_forcing_cube_side_ranges(2, 1, true) == Ranges{{{-1, 0}, {1, 1}}} &&
                fluid_25d_forcing_cube_side_ranges(2, -1, true) == Ranges{{{-1, -1}, {0, 1}}} &&
                fluid_25d_forcing_cube_side_ranges(2, 5, true) == Ranges{{{-1, 1}, {1, 1}}} &&
                fluid_25d_forcing_cube_side_ranges(2, 0, false) == Ranges{{{-1, 1}, {1, 1}}},
            "cube sides must omit only shared overlap and retain exposed height steps");
    config.scenario = Fluid25DScenario::SourceOutletDemo;
    config.grid_width = 128U;
    config.grid_height = 64U;
    config.cell_size_m = 1.0F;
    const auto demo = make_fluid_25d_scenario(config.scenario, 128U, 64U, 1.0F);
    const auto positive_count = [](const auto& field) {
        return static_cast<std::size_t>(
            std::count_if(field.begin(), field.end(), [](float value) { return value > 0; }));
    };
    require(fluid_25d_forcing_cubes(config, demo).size() ==
                positive_count(demo.source_depth_rate_m_per_s) +
                    positive_count(demo.sink_depth_rate_m_per_s),
            "authored source/drain strips must highlight every actual forcing tile");
}

void test_windowed_pacing() {
    using namespace cubey::projects::fluid::fluid_25d;

    const auto count_steps = [](double refresh_hz, float presentation_time_scale,
                                std::uint32_t frame_count) {
        Fluid25DWindowedPacing pacing(1.0F / 60.0F, presentation_time_scale);
        std::uint64_t step_count = 0U;
        for (std::uint32_t frame = 0U; frame < frame_count; ++frame) {
            step_count += pacing.advance(1.0 / refresh_hz, false).fixed_step_count;
        }
        require(pacing.accumulator_seconds() >= 0.0 &&
                    pacing.accumulator_seconds() < 1.0 / 60.0 + 1.0e-12,
                "fluid 2.5D windowed pacing should retain only a substep remainder");
        return std::pair<std::uint64_t, std::uint64_t>{step_count, pacing.dropped_backlog_frames()};
    };

    const auto [steps_144, drops_144] = count_steps(144.0, 1.0F, 144U);
    require(steps_144 == 60U && drops_144 == 0U,
            "fluid 2.5D windowed pacing should match one second at 144 Hz");

    const auto [steps_60, drops_60] = count_steps(60.0, 1.0F, 60U);
    require(steps_60 == 60U && drops_60 == 0U,
            "fluid 2.5D windowed pacing should match one second at 60 Hz");

    const auto [steps_30, drops_30] = count_steps(30.0, 1.0F, 30U);
    require(steps_30 == 60U && drops_30 == 0U,
            "fluid 2.5D windowed pacing should catch up two steps at 30 Hz");

    const auto [steps_4x, drops_4x] = count_steps(60.0, 4.0F, 60U);
    require(steps_4x == 240U && drops_4x == 0U,
            "fluid 2.5D windowed pacing should support 4x at 60 Hz within its cap");

    const auto [steps_8x, drops_8x] = count_steps(144.0, 8.0F, 144U);
    require(steps_8x == 480U && drops_8x == 0U,
            "fluid 2.5D windowed pacing should support 8x at 144 Hz within its cap");

    Fluid25DWindowedPacing paused(1.0F / 60.0F, 1.0F);
    require(paused.advance(1.0 / 120.0, false).fixed_step_count == 0U,
            "fluid 2.5D pacing should retain a fractional step");
    const double fractional_remainder = paused.accumulator_seconds();
    require(paused.advance(30.0, true).fixed_step_count == 0U &&
                paused.accumulator_seconds() == fractional_remainder,
            "fluid 2.5D paused pacing should not accumulate wall-time backlog");
    require(paused.advance(1.0 / 120.0, false).fixed_step_count == 1U,
            "fluid 2.5D pacing should resume from the pre-pause remainder");

    Fluid25DWindowedPacing stalled(1.0F / 60.0F, 1.0F);
    const Fluid25DWindowedPacingFrame catch_up = stalled.advance(1.0, false);
    require(catch_up.fixed_step_count == kFluid25DWindowedMaxFixedStepsPerFrame &&
                catch_up.dropped_backlog && stalled.dropped_backlog_frames() == 1U,
            "fluid 2.5D pacing should cap and report a long-frame backlog drop");
    require(stalled.accumulator_seconds() >= 0.0 &&
                stalled.accumulator_seconds() < 1.0 / 60.0 + 1.0e-12 &&
                stalled.advance(0.0, false).fixed_step_count == 0U,
            "fluid 2.5D pacing should discard excess backlog while retaining no burst");

    static_cast<void>(stalled.advance(1.0 / 120.0, false));
    stalled.reset();
    require(stalled.accumulator_seconds() == 0.0 && stalled.dropped_backlog_frames() == 0U &&
                stalled.advance(1.0 / 120.0, false).fixed_step_count == 0U,
            "fluid 2.5D pacing reset should clear backlog and drop history");
    require_throws([] { static_cast<void>(Fluid25DWindowedPacing(0.0F, 1.0F)); },
                   "fluid 2.5D windowed pacing should reject a nonpositive fixed delta");
    require_throws([] { static_cast<void>(Fluid25DWindowedPacing(1.0F / 60.0F, 0.0F)); },
                   "fluid 2.5D windowed pacing should reject a nonpositive presentation scale");
    require_throws(
        [] {
            Fluid25DWindowedPacing pacing;
            static_cast<void>(pacing.advance(std::numeric_limits<double>::infinity(), false));
        },
        "fluid 2.5D windowed pacing should reject a nonfinite wall delta");
}

void test_motion_marker_display_clock() {
    using namespace cubey::projects::fluid::fluid_25d;
    const auto sample = [&](std::uint32_t fps) {
        Fluid25DWindowedPacing pacing(2.0F, 8.0F);
        Fluid25DMotionMarkerDisplayClock display;
        std::uint32_t steps = 0U;
        for (std::uint32_t frame = 0U; frame < fps * 10U; ++frame) {
            const auto state = pacing.advance(1.0 / static_cast<double>(fps), false);
            steps += state.fixed_step_count;
            display.update(pacing.accumulator_seconds(), 2.0F, state.fixed_step_count, false,
                           false);
        }
        const float before_pause = display.fraction();
        for (std::uint32_t frame = 0U; frame < fps; ++frame) {
            const auto state = pacing.advance(1.0 / static_cast<double>(fps), true);
            display.update(pacing.accumulator_seconds(), 2.0F, state.fixed_step_count, false, true);
        }
        require(display.fraction() == before_pause,
                "pause freezes the exact displayed marker position");
        return std::pair{steps, display.fraction()};
    };
    const auto at30 = sample(30U), at60 = sample(60U);
    require(at30.first == 40U && at60.first == 40U,
            "30 and 60 FPS execute the same 80 physical seconds of hillside motion");
    require_close(
        at30.second, at60.second, 0.000001,
        "render FPS does not change the interpolated position at the same physical clock");
    Fluid25DMotionMarkerDisplayClock display;
    display.update(0.0, 2.0F, 16U, true, true);
    display.update(0.5, 2.0F, 0U, false, false);
    require(display.fraction() == 1.0F,
            "continuous resume holds the completed inspection state until a new step");
    display.update(0.5, 2.0F, 1U, false, false);
    require_close(display.fraction(), 0.25, 0.0,
                  "regular fixed steps resume interpolation without backwards teleport");
    display.reset();
    require(display.fraction() == 1.0F, "reset restores the display clock");
    require_throws(
        [&] { display.update(0.0, 0.0F, 0U, false, false); },
        "invalid display delta cannot introduce animation independent of simulation time");
}

void test_hillside_dye_palette_mapping() {
    using namespace cubey::projects::fluid::fluid_25d;

    require_close(fluid_25d_hillside_dye_palette_position(0.0F), 0.0, 0.0,
                  "clear carrier should map to the bottom of the fixed dye scale");
    require_close(
        fluid_25d_hillside_dye_palette_position(kFluid25DHillsideDyePaletteMinimumConcentration),
        0.0, 0.0, "the lower palette endpoint should remain clear carrier");
    require_close(
        fluid_25d_hillside_dye_palette_position(kFluid25DHillsideDyePaletteMaximumConcentration),
        1.0, 0.0, "injected concentration should map to the upper palette endpoint");
    require_close(fluid_25d_hillside_dye_palette_position(2.0F), 1.0, 0.0,
                  "concentration above the injection reference should clamp to the upper endpoint");
    require_close(kFluid25DHillsideDyePaletteClearFloorPercent, 0.01, 0.000001,
                  "the fixed clear-carrier floor should be 0.01 percent of injection");

    constexpr std::array<float, 4> expected_positions{0.25F, 0.5F, 0.75F, 1.0F};
    for (std::size_t index = 0U; index < kFluid25DHillsideDyePaletteLegendConcentrations.size();
         ++index) {
        const float concentration = kFluid25DHillsideDyePaletteLegendConcentrations[index];
        require_close(fluid_25d_hillside_dye_palette_position(concentration),
                      expected_positions[index], 0.00001,
                      "each fixed concentration legend knot should have its logarithmic position");
        if (index > 0U) {
            const float previous = kFluid25DHillsideDyePaletteLegendConcentrations[index - 1U];
            require(fluid_25d_hillside_dye_palette_tint(concentration) >
                        fluid_25d_hillside_dye_palette_tint(previous),
                    "successive concentration decades should produce stronger dye tint");
        }
    }
    require(fluid_25d_hillside_dye_palette_tint(0.001F) > 0.10F &&
                fluid_25d_hillside_dye_palette_tint(0.003F) < 0.35F &&
                fluid_25d_hillside_dye_palette_tint(1.0F) ==
                    kFluid25DHillsideDyePaletteMaximumTint &&
                kFluid25DHillsideDyePaletteMaximumTint < 1.0F,
            "weak tails should remain visible, former saturation levels should retain a gradient, "
            "and injected concentration should preserve carrier color");

    constexpr std::array<float, 10> samples{-1.0F,  0.0F,  0.00001F, 0.0001F, 0.0002F,
                                            0.001F, 0.01F, 0.1F,     1.0F,    2.0F};
    float previous_position = -1.0F;
    float previous_tint = -1.0F;
    for (const float concentration : samples) {
        const float position = fluid_25d_hillside_dye_palette_position(concentration);
        const float tint = fluid_25d_hillside_dye_palette_tint(concentration);
        require(std::isfinite(position) && position >= 0.0F && position <= 1.0F &&
                    std::isfinite(tint) && tint >= 0.0F &&
                    tint <= kFluid25DHillsideDyePaletteMaximumTint &&
                    position >= previous_position && tint >= previous_tint,
                "the fixed dye palette position and tint should be finite, bounded, and monotonic");
        previous_position = position;
        previous_tint = tint;
    }

    const auto uses_hillside_palette = [](Fluid25DScenario scenario) {
        return fluid_25d_hillside_dye_palette_enabled_for_material_cue(
            fluid_25d_catchment_terrain_material_cue(scenario));
    };
    require(uses_hillside_palette(Fluid25DScenario::HillsideFlowStudy) &&
                !uses_hillside_palette(Fluid25DScenario::NaturalFlowStudy) &&
                !uses_hillside_palette(Fluid25DScenario::SustainedHeadwatersDemo) &&
                !uses_hillside_palette(Fluid25DScenario::SourceOutletDemo) &&
                !uses_hillside_palette(Fluid25DScenario::MountainSourceOutletDemo) &&
                !fluid_25d_hillside_dye_palette_enabled_for_material_cue(3.999F),
            "only the exact hillside material cue should select the new dye scale");
}

void test_presentation_cue_contract() {
    using namespace cubey::projects::fluid::fluid_25d;

    require(kFluid25DPresentationCueRelaxationPerSecond > 0.0F &&
                kFluid25DPresentationCueRelaxationPerSecond < 0.01F,
            "presentation cue relaxation should remain a mild render-only stabilization");
    const float repeat_a = fluid_25d_presentation_cue_seed(37U, 19U);
    const float repeat_b = fluid_25d_presentation_cue_seed(37U, 19U);
    require(repeat_a == repeat_b && repeat_a >= 0.0F && repeat_a <= 1.0F,
            "presentation cue seed should be deterministic and normalized");

    float minimum = 1.0F;
    float maximum = 0.0F;
    float maximum_neighbor_delta = 0.0F;
    for (std::uint32_t y = 0U; y < 64U; ++y) {
        for (std::uint32_t x = 0U; x < 128U; ++x) {
            const float value = fluid_25d_presentation_cue_seed(x, y);
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
            if (x != 0U) {
                maximum_neighbor_delta =
                    std::max(maximum_neighbor_delta,
                             std::abs(value - fluid_25d_presentation_cue_seed(x - 1U, y)));
            }
            if (y != 0U) {
                maximum_neighbor_delta =
                    std::max(maximum_neighbor_delta,
                             std::abs(value - fluid_25d_presentation_cue_seed(x, y - 1U)));
            }
        }
    }
    require(maximum - minimum > 0.20F && maximum_neighbor_delta < 0.12F,
            "presentation cue seed should vary broadly without cell-scale white noise");

    Fluid25DPresentationCueParity parity;
    require(parity.source_is_a(), "presentation cue parity should begin on A after startup/reset");
    parity.advance();
    require(!parity.source_is_a(),
            "presentation cue parity should advance exactly once per update");
    parity.advance();
    require(parity.source_is_a(), "presentation cue parity should ping-pong deterministically");
    parity.reset();
    require(parity.source_is_a(), "presentation cue reset should restore the deterministic source");

    const Fluid25DQuiverLattice product_lattice = fluid_25d_quiver_lattice(256U, 128U);
    const Fluid25DQuiverLattice demo_lattice = fluid_25d_quiver_lattice(128U, 64U);
    const Fluid25DQuiverLattice tiny_lattice = fluid_25d_quiver_lattice(3U, 2U);
    const Fluid25DQuiverLattice degenerate_lattice = fluid_25d_quiver_lattice(0U, 1U);
    require(product_lattice.columns == 96U && product_lattice.rows == 48U &&
                demo_lattice.columns == 96U && demo_lattice.rows == 48U &&
                fluid_25d_quiver_count(256U, 128U) == 4608U &&
                fluid_25d_quiver_count(128U, 64U) == 4608U && tiny_lattice.columns == 3U &&
                tiny_lattice.rows == 2U && degenerate_lattice.columns == 1U &&
                degenerate_lattice.rows == 1U &&
                fluid_25d_quiver_anchor(0U, 0U, 1U).cell_x == 0.0F &&
                fluid_25d_quiver_anchor(0U, 0U, 1U).cell_y == 0.0F,
            "Flow Inspection should use a regular 96 by 48 field on product and demo grids while "
            "reducing tiny fixtures");
    const Fluid25DQuiverAnchor demo_first = fluid_25d_quiver_anchor(0U, 128U, 64U);
    const Fluid25DQuiverAnchor demo_second = fluid_25d_quiver_anchor(1U, 128U, 64U);
    const Fluid25DQuiverAnchor demo_next_row = fluid_25d_quiver_anchor(96U, 128U, 64U);
    const Fluid25DQuiverAnchor demo_repeat = fluid_25d_quiver_anchor(96U, 128U, 64U);
    require(demo_first.cell_x == 0.5F && demo_first.cell_y == 0.5F &&
                demo_second.cell_x > demo_first.cell_x && demo_second.cell_y == demo_first.cell_y &&
                demo_next_row.cell_x == demo_first.cell_x &&
                demo_next_row.cell_y > demo_first.cell_y &&
                demo_next_row.cell_x == demo_repeat.cell_x &&
                demo_next_row.cell_y == demo_repeat.cell_y &&
                std::abs((demo_second.cell_x - demo_first.cell_x) - (126.0F / 95.0F)) < 0.0001F,
            "quiver anchors should be deterministic, fixed, and regularly spaced near 1.3 cells on "
            "the demo");
    require(kFluid25DQuiverVertexCount == 9U && kFluid25DQuiverMinimumSpeedMPerS > 0.019828F &&
                kFluid25DQuiverSpeedUpperMPerS > 0.79F && kFluid25DQuiverSpeedUpperMPerS < 0.81F &&
                kFluid25DQuiverMinimumSilhouettePitchFraction > 0.57F &&
                kFluid25DQuiverMaximumSilhouettePitchFraction > 0.71F &&
                kFluid25DQuiverMaximumSilhouettePitchFraction < 0.73F &&
                kFluid25DQuiverNeighborhoodRadiusCells == 1U &&
                fluid_25d_quiver_smoothing_blend(2.0F, kFluid25DQuiverDirectionSmoothingSeconds) >
                    0.0F &&
                fluid_25d_quiver_smoothing_blend(2.0F, kFluid25DQuiverDirectionSmoothingSeconds) <
                    1.0F &&
                !fluid_25d_quiver_sample_is_visible(0.0F, 1.0F, 0.001F) &&
                !fluid_25d_quiver_sample_is_visible(1.0F, 0.019828F, 0.001F) &&
                fluid_25d_quiver_sample_is_visible(1.0F, 0.05F, 0.001F),
            "quiver field should use simulation-time smoothing and keep dry, still, and initial "
            "terrain flow absent");

    const std::filesystem::path shader_directory =
        std::filesystem::path(__FILE__).parent_path() / "shaders";
    const auto read_shader = [](const std::filesystem::path& path) {
        const auto shared = std::filesystem::path(__FILE__)
                                .parent_path()
                                .parent_path()
                                .parent_path()
                                .parent_path()
                                .parent_path() /
                            "shaders/cubey/terrain/surface_patch" / path.filename();
        std::ifstream stream(
            path.parent_path().filename() == "shaders" && std::filesystem::exists(shared) ? shared
                                                                                          : path);
        if (!stream) {
            throw std::runtime_error("failed to read presentation cue shader");
        }
        return std::string(std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>());
    };
    const std::string reset =
        read_shader(shader_directory / "fluid_25d_presentation_cue_reset.comp");
    const std::string advect =
        read_shader(shader_directory / "fluid_25d_presentation_cue_advect.comp");
    const std::string water = read_shader(shader_directory / "fluid_25d_water.frag");
    const std::string water_vertex = read_shader(shader_directory / "fluid_25d_water.vert");
    const std::string marker_vertex =
        read_shader(shader_directory / "fluid_25d_motion_markers.vert");
    const std::string marker_bilinear_vertex =
        read_shader(shader_directory / "fluid_25d_motion_markers_bilinear.vert");
    const std::string sampling = read_shader(shader_directory / "fluid_25d_surface_sampling.glsl");
    const std::string bspline_surface =
        read_shader(shader_directory / "fluid_25d_bspline_surface.glsl");
    const std::string bspline_water =
        read_shader(shader_directory / "fluid_25d_water_bspline.vert") +
        read_shader(shader_directory / "fluid_25d_water_bspline_body.glsl");
    const std::string bspline_terrain =
        read_shader(shader_directory / "fluid_25d_terrain_bspline.vert") +
        read_shader(shader_directory / "fluid_25d_terrain_bspline_body.glsl");
    require(bspline_water.find("#include \"fluid_25d_water_bspline_body.glsl\"") !=
                    std::string::npos &&
                bspline_terrain.find("#include \"fluid_25d_terrain_bspline_body.glsl\"") !=
                    std::string::npos,
            "paired vertex wrappers must include the shared bodies being checked");
    require(bspline_water.find("fluid25d_bspline_vertex") != std::string::npos &&
                bspline_terrain.find("fluid25d_bspline_vertex") != std::string::npos &&
                bspline_water.find("water_depth = bh.y") != std::string::npos &&
                bspline_surface.find("f.x >= f.y") != std::string::npos &&
                marker_bilinear_vertex.find("fluid25d_bspline_mesh_bed_depth") !=
                    std::string::npos &&
                water.find("float water_depth = triangle_water_depth") != std::string::npos,
            "experimental material and markers must follow the matched fine triangle mesh, not a "
            "second cubic fragment surface");
    require(marker_vertex.find("f.x>=f.y") != std::string::npos &&
                marker_vertex.find("h[0]*(1.0-f.x)+h[1]*(f.x-f.y)+h[3]*f.y") != std::string::npos &&
                marker_vertex.find("h[0]*(1.0-f.y)+h[2]*(f.y-f.x)+h[3]*f.x") != std::string::npos &&
                sampling.find("f.x >= f.y") != std::string::npos &&
                sampling.find("v.x*(1.0-f.x) + v.y*(f.x-f.y) + v.w*f.y") != std::string::npos &&
                sampling.find("v.x*(1.0-f.y) + v.z*(f.y-f.x) + v.w*f.x") != std::string::npos &&
                water_vertex.find("uvec2(0u, 0u), uvec2(1u, 1u), uvec2(1u, 0u)") !=
                    std::string::npos,
            "markers must use the visible water mesh diagonal, not a bilinear surface below it");
    require(water.find("fluid25d_supported_depth") != std::string::npos &&
                marker_bilinear_vertex.find("fluid25d_supported_depth") != std::string::npos &&
                marker_bilinear_vertex.find("h[0]*(1.0-f.x)+h[1]*(f.x-f.y)+h[3]*f.y") !=
                    std::string::npos &&
                marker_bilinear_vertex.find("h[0]*(1.0-f.y)+h[2]*(f.y-f.x)+h[3]*f.x") !=
                    std::string::npos &&
                marker_vertex.find("fluid25d_supported_depth") == std::string::npos &&
                sampling.find("<= 0.5") != std::string::npos,
            "water and marker display wetness share conservative support, without dry diagonal "
            "bridges");
    // Independent curved-cell counterexample: four corner heights 0,0,0,4
    // give a visible diagonal height of 2 at the center. Bilinear draping
    // gives 1 and can hide the marker below the water. Both triangles have
    // nonnegative barycentric weights and agree along their common diagonal.
    const auto mesh_height = [](double x, double z) { return x >= z ? 4.0 * z : 4.0 * x; };
    require_close(mesh_height(0.5, 0.5), 2.0, 0.0, "curved-cell mesh center height");
    require_close(mesh_height(0.75, 0.25), 1.0, 0.0, "first water triangle height");
    require_close(mesh_height(0.25, 0.75), 1.0, 0.0, "second water triangle height");
    const std::string quiver_reset = read_shader(shader_directory / "fluid_25d_quiver_reset.comp");
    const std::string quiver_update =
        read_shader(shader_directory / "fluid_25d_quiver_update.comp");
    const std::string quiver_vertex = read_shader(shader_directory / "fluid_25d_quiver.vert");
    const std::string quiver_fragment = read_shader(shader_directory / "fluid_25d_quiver.frag");
    const std::string commands =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_commands.cpp");
    const std::string app =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_builtin_render.cpp") +
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_builtin_app_internal.h");
    const std::string ui =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_ui.cpp");
    const std::string recording_app =
        read_shader(std::filesystem::path(__FILE__).parent_path() / "fluid_25d_recording_presentation.cpp");
    const std::string project_config = read_shader(std::filesystem::path(__FILE__).parent_path() /
                                                   "../../fluid_25d/fluid_25d_project_config.h");
    require(reset.find("cue_lattice") != std::string::npos &&
                reset.find("cue_a.values[index] = seed;") != std::string::npos &&
                reset.find("cue_b.values[index] = seed;") != std::string::npos,
            "presentation cue reset shader should restore both ping-pong fields from one seed");
    require(advect.find("sample_cue_bilinear") != std::string::npos &&
                advect.find("status.values[0].x != 0u") != std::string::npos &&
                advect.find("destination_cue.values[index] = source_cue.values[index];") !=
                    std::string::npos,
            "presentation cue advection should backtrace bilinearly and freeze on solver failure");
    require(water.find("presentation_cue") != std::string::npos &&
                water.find("flow_inspection") != std::string::npos &&
                water.find("params.animation") == std::string::npos &&
                water.find("sin(") == std::string::npos && water.find("cos(") == std::string::npos,
            "water shading should consume the persistent cue without procedural time bands");
    const std::size_t highlight_off =
        water.find("if ((options & 256u) != 0u) sparse_highlight = 0.0;");
    const std::size_t highlight_contribution = water.find("* sparse_highlight", highlight_off);
    require(highlight_off != std::string::npos && highlight_contribution != std::string::npos &&
                water.find("if ((options & 256u) != 0u) water_depth") == std::string::npos,
            "native highlights-off bit may suppress only the sparse surface highlight term");
    require(water_vertex.find("binding = 6") != std::string::npos &&
                water_vertex.find("TracerQField") != std::string::npos &&
                water_vertex.find("dye_concentration = clamp") != std::string::npos &&
                water.find("params.presentation.y == 1.0") != std::string::npos &&
                water.find("params.presentation.y == 2.0") != std::string::npos &&
                water.find("params.presentation.y == 3.0") != std::string::npos &&
                water.find("params.presentation.y > 1.5") == std::string::npos &&
                water.find("transport_inspection") != std::string::npos &&
                water.find("dye_visibility") != std::string::npos,
            "Transport Inspection should derive bounded q/h concentration and keep every prior "
            "catchment mode's predicate exact");
    require(water.find("params.presentation.w == 4.0") != std::string::npos &&
                water.find("log(bounded_concentration / dye_scale_minimum)") != std::string::npos &&
                water.find("smoothstep(0.00025, 0.0030, dye_concentration)") != std::string::npos,
            "the fixed log-scale palette should be material-cue gated while other scenarios retain "
            "their established concentration palette");
    require(
        ui.find("kTransportCatchmentViews") != std::string::npos &&
            ui.find("transport_inspection_available") != std::string::npos &&
            ui.find("Transport Inspection needs a finite-volume dye pulse on an eligible demo.") !=
                std::string::npos &&
            ui.find("Magenta-to-violet") != std::string::npos &&
            project_config.find("transport-inspection") != std::string::npos &&
            project_config.find("positive dye pulse duration") != std::string::npos,
        "transport UI and CLI should expose the dye reading only when its scenario contract is "
        "valid");
    const std::size_t highlights_ui_start = recording_app.find("if (ImGui::Combo(\"Highlights\"");
    const std::size_t highlights_ui_end =
        highlights_ui_start == std::string::npos
            ? std::string::npos
            : recording_app.find("\n    }\n", highlights_ui_start);
    const std::string_view highlights_ui_block =
        highlights_ui_start == std::string::npos || highlights_ui_end == std::string::npos
            ? std::string_view{}
            : std::string_view(recording_app)
                  .substr(highlights_ui_start, highlights_ui_end - highlights_ui_start);
    require(!highlights_ui_block.empty() &&
                highlights_ui_block.find("Auto (off in Readable/Scenic)") !=
                    std::string_view::npos &&
                highlights_ui_block.find("config_.native_surface_highlights") !=
                    std::string_view::npos &&
                highlights_ui_block.find("render_.native_surface_highlights") !=
                    std::string_view::npos &&
                highlights_ui_block.find("reset_presentation_history") == std::string_view::npos &&
                highlights_ui_block.find("reset_visual_history") == std::string_view::npos &&
                highlights_ui_block.find("cue_reset_") == std::string_view::npos &&
                highlights_ui_block.find("marker_reset_") == std::string_view::npos &&
                highlights_ui_block.find("visual_delta_") == std::string_view::npos &&
                highlights_ui_block.find("clock_") == std::string_view::npos &&
                highlights_ui_block.find("configure_camera") == std::string_view::npos &&
                highlights_ui_block.find("backend_") == std::string_view::npos,
            "highlight combo must be explicit and must not reset playback, visual history, camera, "
            "or backend");
    const std::size_t substep_loop = commands.find("for (std::uint32_t substep");
    const std::size_t cue_update =
        commands.find("record_presentation_cue_advection(", substep_loop);
    const std::size_t pause_return = commands.find("if (paused) {\n        return;");
    const std::size_t direct_batch = commands.find("void record_fluid_25d_compute_batch(");
    const std::size_t direct_compute = commands.find("void record_fluid_25d_compute(");
    const std::size_t frame_graph = commands.find("build_fluid_25d_frame_graph(");
    const std::size_t diagnostics_pass = commands.find("fluid_25d diagnostics");
    const std::size_t catchment_pass = commands.find("fluid_25d catchment");
    const std::size_t catchment_cue_a =
        commands.find(".read_storage_buffer(presentation_cue_a)", catchment_pass);
    const std::size_t catchment_cue_b =
        commands.find(".read_storage_buffer(presentation_cue_b)", catchment_pass);
    const std::size_t catchment_tracer_q_a =
        commands.find(".read_storage_buffer(tracer_q_a)", catchment_pass);
    const std::size_t catchment_tracer_q_b =
        commands.find(".read_storage_buffer(tracer_q_b)", catchment_pass);
    const std::size_t quiver_update_call = commands.find("record_quiver_update(", substep_loop);
    const std::size_t headless_quiver =
        commands.find("record_fluid_25d_flow_inspection_quiver_step(");
    const std::size_t headless_quiver_reset =
        app.find("record_fluid_25d_flow_inspection_quiver_reset(");
    const std::size_t headless_compute = app.find("record_fluid_25d_compute(");
    require(substep_loop != std::string::npos && cue_update != std::string::npos &&
                substep_loop < cue_update && pause_return != std::string::npos &&
                pause_return < substep_loop,
            "presentation cue should advance after complete fixed steps and never while paused");
    require(direct_batch != std::string::npos && direct_compute != std::string::npos &&
                frame_graph != std::string::npos && direct_batch < direct_compute &&
                commands.find("Fluid25DComputeRecordingPolicy{}", direct_batch) < direct_compute &&
                commands.find("Fluid25DPresentationCuePolicy::WindowedPresentation", frame_graph) !=
                    std::string::npos &&
                app.find("bool reset_requested_ = false;") != std::string::npos &&
                app.find("bool presentation_cue_reset_requested_ = true;") != std::string::npos,
            "direct headless compute should keep the solver-only path while frame-graph "
            "presentation owns the cue");
    require(
        diagnostics_pass != std::string::npos && catchment_pass != std::string::npos &&
            diagnostics_pass < catchment_pass && catchment_cue_a != std::string::npos &&
            catchment_cue_b != std::string::npos && catchment_cue_a > catchment_pass &&
            catchment_cue_b > catchment_pass && catchment_tracer_q_a != std::string::npos &&
            catchment_tracer_q_b != std::string::npos && catchment_tracer_q_a > catchment_pass &&
            catchment_tracer_q_b > catchment_pass,
        "the catchment pass should declare the cue and tracer buffers read by the water shader");
    require(quiver_reset.find("anchor_axis") != std::string::npos &&
                quiver_reset.find("kMaxColumns = 96u") != std::string::npos &&
                quiver_reset.find("kMaxRows = 48u") != std::string::npos &&
                quiver_reset.find("kMinimumPitchCells = 1u") != std::string::npos &&
                quiver_reset.find("sample_average_velocity") != std::string::npos &&
                quiver_reset.find("status.values[0].x != 0u") != std::string::npos &&
                quiver_reset.find("velocity_sample.z < 0.5") != std::string::npos &&
                quiver_update.find("sample_average_velocity") != std::string::npos &&
                quiver_update.find("status.values[0].x != 0u") != std::string::npos &&
                quiver_update.find("state.anchor_xy_reserved =") == std::string::npos &&
                quiver_update.find("smoothed_direction") != std::string::npos &&
                quiver_update.find("mixed_length < kDirectionEpsilon") != std::string::npos &&
                quiver_update.find("state.direction_xy_strength_opacity = vec4(0.0)") !=
                    std::string::npos,
            "quiver compute should use fixed wet-aware local samples, smooth safely, and freeze on "
            "rejected status");
    require(quiver_vertex.find("gl_InstanceIndex") != std::string::npos &&
                quiver_vertex.find("shaft_vertex") != std::string::npos &&
                quiver_vertex.find("head_vertex") != std::string::npos &&
                quiver_vertex.find("lattice_pitch") != std::string::npos &&
                quiver_vertex.find("params.terrain_palette.w - kMinimumSpeedMPerS") !=
                    std::string::npos &&
                Fluid25DCatchmentRenderOptions{}.quiver_speed_upper_m_per_s == 0.80F &&
                quiver_vertex.find("kMaximumSilhouettePitchFraction") != std::string::npos &&
                quiver_vertex.find("current_velocity.z < 0.5") != std::string::npos &&
                quiver_vertex.find("current_velocity.xy") != std::string::npos &&
                quiver_vertex.find("length(current_velocity.xy)") == std::string::npos &&
                quiver_fragment.find("fwidth(normalized_edge)") != std::string::npos &&
                quiver_fragment.find("frag_part != 0u") != std::string::npos &&
                quiver_fragment.find("color * alpha") != std::string::npos,
            "Flow Inspection should render fixed conventional antialiased arrow glyphs without a "
            "draw-time speed cutoff");
    require(quiver_update_call != std::string::npos && quiver_update_call > substep_loop &&
                headless_quiver != std::string::npos &&
                headless_quiver_reset != std::string::npos &&
                headless_compute != std::string::npos && headless_quiver_reset < headless_compute &&
                app.find("record_fluid_25d_flow_inspection_quiver_step") != std::string::npos &&
                app.find("bool quiver_reset_requested_ = true;") != std::string::npos,
            "quiver should seed the initial headless field before solving, then follow completed "
            "outer steps separately");
    require(commands.find("catchment_view == Fluid25DCatchmentView::FlowInspection") !=
                    std::string::npos &&
                commands.find("catchment_view == Fluid25DCatchmentView::TransportInspection") ==
                    std::string::npos,
            "Transport Inspection should not schedule or render Flow Inspection's quiver field");
}

} // namespace cubey::projects::fluid::fluid_25d::test_detail
