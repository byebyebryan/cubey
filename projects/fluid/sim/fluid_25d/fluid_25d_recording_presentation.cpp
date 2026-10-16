#include "fluid_25d_recording_app_internal.h"

namespace cubey::projects::fluid::fluid_25d::recording_detail {

void RecordingApp::draw_native_color_legend() const {
    if (scenic_material_active())
        ImGui::TextWrapped(
            "Scenic color = materials/reflection/optical depth, not a depth scale. "
            "Dots/trails = approximate velocity, not water parcels.");
    else
        ImGui::TextWrapped(
            "Blue = depth. Dots/trails = approximate velocity, not water parcels.");
}

void RecordingApp::select_native_style(std::uint32_t style) {
    const bool compare_materials = render_.native_presentation >= 2U && style >= 2U;
    render_.native_presentation = style;
    config_.native_presentation = style == 3U   ? "scenic"
                                  : style == 2U ? "readable"
                                  : style == 1U ? "motion"
                                                : "original";
    render_.native_surface_highlights = fluid_25d_native_surface_highlights_enabled(
        fluid_25d_native_surface_highlights_policy(config_.native_surface_highlights), style);
    // Readable/Scenic comparisons preserve the orbit and dot history.
    // Original/Motion retain their historical framing/reset behavior.
    if (!compare_materials) {
        reset_presentation_history();
        configure_camera();
    }
    std::printf("fluid_25d_native_style: mode=%s\n", config_.native_presentation.c_str());
    std::fflush(stdout);
}

void RecordingApp::report_scenic_material() const {
    const auto json = fluid_25d_scenic_material_json(scenic_material_);
    const bool dots_draw = config_.motion_markers &&
                           config_.native_scenic_terrain_view == "shaded" &&
                           config_.native_scenic_water_view == "shaded";
    std::printf("fluid_25d_scenic_terrain: view=%s water_draw=%u dots_draw=%u\n",
                config_.native_scenic_terrain_view.c_str(),
                config_.native_scenic_terrain_view == "shaded" ? 1U : 0U, dots_draw ? 1U : 0U);
    std::printf("fluid_25d_scenic_material: profile=%s%s%s settings=%s\n",
                config_.native_scenic_material.c_str(),
                config_.native_scenic_tuning_path ? "+tuning" : "",
                scenic_material_edited_ ? "+edited" : "", json.c_str());
    std::printf("fluid_25d_scenic_surface: mode=%s source_bound=%u\n",
                config_.native_scenic_surface_mode.c_str(), terrain_surface_ ? 1U : 0U);
    std::printf(
        "fluid_25d_scenic_water: shading=wet-ground view=%s pipeline=normal dots_draw=%u "
        "coverage=retained\n",
        config_.native_scenic_water_view.c_str(), dots_draw ? 1U : 0U);
    std::fflush(stdout);
}

void RecordingApp::draw_native_presentation_ui() {
    if (ImGui::TreeNode("Environment (render only)")) {
        bool shared = config_.environment_mode == "shared";
        if (ImGui::Checkbox("Shared clear sky and day/night", &shared))
            config_.environment_mode = shared ? "shared" : "fixed";
        if (shared && weather_)
            host::draw_terrain_surface_environment_controls(*weather_);
        if (!shared)
            ImGui::TextDisabled("Accepted fixed daylight reference. No environment clock.");
        ImGui::TreePop();
    }
    int style = static_cast<int>(render_.native_presentation);
    ImGui::SetNextItemWidth(-100.0F);
    if (ImGui::Combo("Style", &style,
                     "Original reference\0Continuous motion\0Readable terrain/water\0Scenic "
                     "(opt-in HDR)\0")) {
        select_native_style(static_cast<std::uint32_t>(style));
    }
    int highlight_policy = static_cast<int>(
        fluid_25d_native_surface_highlights_policy(config_.native_surface_highlights));
    ImGui::SetNextItemWidth(-100.0F);
    ImGui::BeginDisabled(render_.native_presentation == 3U);
    if (ImGui::Combo("Highlights", &highlight_policy,
                     "Auto (off in Readable/Scenic)\0On\0Off\0")) {
        config_.native_surface_highlights = highlight_policy == 1   ? "on"
                                            : highlight_policy == 2 ? "off"
                                                                    : "auto";
        render_.native_surface_highlights = fluid_25d_native_surface_highlights_enabled(
            fluid_25d_native_surface_highlights_policy(config_.native_surface_highlights),
            render_.native_presentation);
    }
    ImGui::EndDisabled();
    if (render_.native_presentation == 3U)
        ImGui::TextWrapped(
            "Scenic: HDR materials and decorative flow normals; V compares Readable. "
            "Raw maps and inspection views retain diagnostic shading.");
    if (render_.native_presentation == 3U &&
        ImGui::CollapsingHeader("Visible rain (render only)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show falling rain", &config_.native_rain_visuals);
        ImGui::SliderFloat("Rain visual strength", &config_.native_rain_strength, 0.0F, 2.0F);
        ImGui::SliderFloat("Rain fall speed", &config_.native_rain_speed, 0.25F, 8.0F, "%.2fx");
        const auto rain = applied_rain_mm_per_hour();
        if (rain)
            ImGui::Text("Applied weather %.2f mm/h | %u streaks", *rain,
                        rain_visual_frame().streak_count);
        else
            ImGui::TextUnformatted("Applied weather unavailable; no rain drawn");
        ImGui::TextWrapped("Artistic macro-scale streaks, not water parcels. Strength only "
                           "changes their density, never the solver supply. Fall speed is "
                           "independent of simulation speed; 1x is the original slow look. "
                           "Pause freezes motion. Component views hide rain.");
    }
    if (render_.native_presentation == 3U &&
        ImGui::CollapsingHeader("Scenic materials (render only)")) {
        int profile = config_.native_scenic_material == "macro"     ? 3
                      : config_.native_scenic_material == "terrain" ? 2
                      : config_.native_scenic_material == "refined" ? 1
                                                                    : 0;
        ImGui::SetNextItemWidth(-100.0F);
        if (ImGui::Combo(
                "Material", &profile,
                "V1 reference\0Refined\0Terrain study (opt-in)\0Mountain demo daylight\0")) {
            config_.native_scenic_material = profile == 3   ? "macro"
                                             : profile == 2 ? "terrain"
                                             : profile == 1 ? "refined"
                                                            : "v1";
            config_.native_scenic_tuning_path.reset();
            scenic_material_edited_ = false;
            scenic_material_ = fluid_25d_scenic_material(config_.native_scenic_material);
            report_scenic_material();
        }
        // Edits only affect the next frame's material uniforms, not playback history.
        int surface_mode =
            int(fluid_25d_terrain_surface_mode(config_.native_scenic_surface_mode));
        ImGui::BeginDisabled(!terrain_surface_);
        if (ImGui::Combo(
                "Terrain organization", &surface_mode,
                "Legacy\0Landform masks\0Climate + landform\0Source-aligned masks (study)\0")) {
            constexpr std::array names{"legacy", "landform", "climate", "correlated"};
            config_.native_scenic_surface_mode = names[std::size_t(surface_mode)];
            report_scenic_material();
        }
        ImGui::EndDisabled();
        if (terrain_surface_ && terrain_surface_->site) {
            ImGui::TextWrapped("World site: %s. Patch-only view: rain, water and terrain "
                               "cover the %.3f km simulation square. Wider backdrop terrain "
                               "belongs to the Terrain application.",
                               terrain_surface_->site->id.c_str(),
                               float(simulation_.grid_width) * simulation_.cell_size_m /
                                   1000.0F);
        }
        ImGui::TextWrapped(terrain_surface_
                               ? "Climate is a long-term material prior, not current rain or "
                                 "wetness. Masks are "
                                 "baked once; elevations and hydraulic fields are unchanged."
                               : "Terrain organization study requires a matching "
                                 "--fluid25d-scenic-surface-source.");
        constexpr std::array water_views{
            "shaded",           "environment-only", "direct-only",   "transmission-only",
            "no-environment",   "no-direct",        "no-clarity",    "no-detail",
            "depth-bands",      "coverage",         "film-weight",   "roughness",
            "rapid-activity",   "rapid-foam",       "no-rapid-foam", "cascade-weight",
            "landing-activity", "no-landing-foam",  "bank-edge",     "no-bank-edge"};
        int water_view =
            static_cast<int>(fluid_25d_scenic_water_view(config_.native_scenic_water_view));
        if (ImGui::Combo("Water component", &water_view, water_views.data(),
                         static_cast<int>(water_views.size()))) {
            config_.native_scenic_water_view =
                water_views[static_cast<std::size_t>(water_view)];
            if (water_view != 0)
                config_.native_scenic_terrain_view = "shaded";
            report_scenic_material();
        }
        if (water_view != 0)
            ImGui::TextWrapped(
                "Water-only contribution over terrain; dots hidden. Depth bands: yellow below "
                "1cm, orange 1-5cm, cyan above 5cm. Not a simulation mode.");
        bool edited =
            ImGui::SliderFloat("Wet roughness", &scenic_material_.wet_roughness, 0.2F, 1.0F);
        edited |=
            ImGui::SliderFloat("Shallow bed tint", &scenic_material_.water_clarity, 0.0F, 1.0F);
        if (ImGui::TreeNode("Water appearance and daylight")) {
            ImGui::TextDisabled("Optional bank-edge treatment (off by default)");
            edited |=
                ImGui::SliderFloat("Bank irregularity (m)",
                                   &scenic_material_.water_bank_irregularity_m, 0.0F, 24.0F);
            edited |= ImGui::SliderFloat("Bank water motion (m)",
                                         &scenic_material_.water_bank_motion_m, 0.0F, 12.0F);
            if (scenic_material_.water_bank_irregularity_m > 0.0F ||
                scenic_material_.water_bank_motion_m > 0.0F) {
                edited |= ImGui::SliderFloat(
                    "Bank noise scale (m)", &scenic_material_.water_bank_scale_m, 8.0F, 128.0F);
                edited |= ImGui::SliderFloat("Bank edge band (m)",
                                             &scenic_material_.water_bank_band_m, 1.0F, 64.0F);
                if (ImGui::SmallButton("Disable bank-edge treatment")) {
                    scenic_material_.water_bank_irregularity_m = 0.0F;
                    scenic_material_.water_bank_motion_m = 0.0F;
                    edited = true;
                }
                ImGui::TextWrapped(
                    "Experimental water-edge trimming only; terrain stays fixed. "
                    "B-spline mode follows reconstructed depth and slope, not cell edges. "
                    "Motion follows filtered flow and pauses with playback. "
                    "Insets are capped relative to the native cell and local depth. "
                    "Above 8/3 m, caps relax for an exaggerated comparison; narrow "
                    "streams can visibly retreat or break. "
                    "No new wet area; raw/component views bypass it.");
            }
            edited |= ImGui::SliderFloat("Flow surface agitation",
                                         &scenic_material_.water_flow_agitation, 0.0F, 1.0F);
            edited |= ImGui::SliderFloat("Rain surface agitation",
                                         &scenic_material_.water_rain_agitation, 0.0F, 1.0F);
            edited |= ImGui::SliderFloat("Steep-flow rapid material",
                                         &scenic_material_.water_rapid_strength, 0.0F, 1.0F);
            edited |= ImGui::SliderFloat("Cascade streaks",
                                         &scenic_material_.water_cascade_strength, 0.0F, 1.0F);
            edited |= ImGui::SliderFloat("Landing foam cue",
                                         &scenic_material_.water_landing_strength, 0.0F, 1.0F);
            const float old_whitewater = scenic_material_.water_whitewater_strength;
            edited |= ImGui::SliderFloat(
                "Whitewater flecks", &scenic_material_.water_whitewater_strength, 0.0F, 1.0F);
            if (old_whitewater == 0.0F && scenic_material_.water_whitewater_strength > 0.0F)
                whitewater_dirty_ = true;
            edited |=
                ImGui::SliderFloat("Gentler-stream foam",
                                   &scenic_material_.water_stream_foam_strength, 0.0F, 1.0F);
            if (scenic_material_.water_stream_foam_strength > 0.0F) {
                edited |= ImGui::SliderFloat("Gentle foam patchiness",
                                             &scenic_material_.water_stream_foam_patchiness,
                                             0.0F, 1.0F);
                edited |= ImGui::SliderFloat("Gentle foam brightness",
                                             &scenic_material_.water_stream_foam_brightness,
                                             0.0F, 1.0F);
                ImGui::TextWrapped(
                    "Patchiness leaves irregular clear-water gaps; brightness changes "
                    "the lit foam tint. Neither changes current or fleck speed. "
                    "Patchiness 0 / brightness 1 restores retained dense foam.");
            }
            if (scenic_material_.water_whitewater_strength > 0.0F)
                ImGui::TextWrapped("3D flecks: %zu/%zu sources; %.1fx cosmetic travel. "
                                   "Speed/size are startup tuning settings.",
                                   whitewater_selection_.seeds.size(),
                                   whitewater_selection_.requested,
                                   double(scenic_material_.water_whitewater_speed));
            if (scenic_material_.water_rapid_strength > 0.0F ||
                scenic_material_.water_cascade_strength > 0.0F ||
                scenic_material_.water_landing_strength > 0.0F)
                edited |=
                    ImGui::SliderFloat("Rapid texture period (m)",
                                       &scenic_material_.water_rapid_scale_m, 64.0F, 384.0F);
            ImGui::TextWrapped("Downhill-water appearance. Cascade motion is artist "
                               "exaggerated; landing foam is a local steep-to-flat heuristic. "
                               "Texture period is an "
                               "artist scale, not wave height or transported foam. "
                               "Water footprint and numerical fields stay unchanged.");
            ImGui::TextWrapped("Render-only activity proxies. Rain response uses applied "
                               "weather even when streaks are hidden. Zero restores retained "
                               "shading; no water mass, banks or solver changes.");
            edited |= ImGui::SliderFloat("Wet-only water normals",
                                         &scenic_material_.water_wet_normal, 0.0F, 1.0F);
            edited |= ImGui::SliderFloat("Wind ripple slope",
                                         &scenic_material_.water_ripple_strength, 0.0F, 0.3F);
            if (scenic_material_.water_ripple_strength > 0.0F)
                edited |=
                    ImGui::SliderFloat("Ripple wavelength (m)",
                                       &scenic_material_.water_ripple_scale_m, 8.0F, 128.0F);
            ImGui::TextWrapped(
                "Wind ripples are decorative normal detail, not simulated water motion.");
            bool shared_daylight = scenic_material_.daylight_environment > 0.5F;
            ImGui::BeginDisabled(config_.environment_mode == "shared");
            if (ImGui::Checkbox("Shared fixed daylight", &shared_daylight)) {
                scenic_material_.daylight_environment = shared_daylight ? 1.0F : 0.0F;
                edited = true;
            }
            if (shared_daylight) {
                edited |= ImGui::SliderFloat("Daylight exposure (EV)",
                                             &scenic_material_.daylight_exposure, -6.0F, 4.0F);
            }
            ImGui::EndDisabled();
            if (shared_daylight || config_.environment_mode == "shared") {
                edited |= ImGui::SliderFloat(config_.environment_mode == "shared"
                                                 ? "Direct-light artistic gain"
                                                 : "Daylight sun strength",
                                             &scenic_material_.daylight_sun_scale, 0.0F, 2.0F);
                ImGui::TextWrapped("Sky diffuse and reflections share one captured atmosphere. "
                                   "Sun strength is an artistic balance, not rainfall.");
            }
            ImGui::TreePop();
        }
        edited |=
            ImGui::SliderFloat("Film roughness", &scenic_material_.film_roughness, 0.2F, 0.8F);
        edited |= ImGui::SliderFloat("Film ground emphasis", &scenic_material_.film_ground_mix,
                                     0.0F, 1.0F);
        edited |=
            ImGui::SliderFloat("Film transition end (m)", &scenic_material_.film_end_m,
                               std::max(0.002F, scenic_material_.film_begin_m + 0.001F), 1.0F);
        edited |=
            ImGui::SliderFloat("Shallow coverage fade",
                               &scenic_material_.water_shallow_coverage_strength, 0.0F, 1.0F);
        if (scenic_material_.water_shallow_coverage_strength > 0.0F)
            edited |=
                ImGui::SliderFloat("Coverage full depth (m)",
                                   &scenic_material_.water_shallow_coverage_end_m, 0.05F, 1.0F);
        ImGui::TextWrapped("Shallow coverage fade exposes existing wet ground; it does not "
                           "create smaller streams or change simulated water. Zero retains "
                           "coverage. Raw depth/component views ignore this artist fade.");
        edited |= ImGui::SliderFloat("Neutral terrain",
                                     &scenic_material_.terrain_material_blend, 0.0F, 1.0F);
        bool corrected = scenic_material_.terrain_normal_strength >= 0.0F;
        if (ImGui::Checkbox("Surface-gradient terrain normals", &corrected)) {
            scenic_material_.terrain_normal_strength = corrected ? 0.12F : -1.0F;
            edited = true;
        }
        if (corrected)
            edited |= ImGui::SliderFloat("Terrain normal strength",
                                         &scenic_material_.terrain_normal_strength, 0.0F, 0.5F);
        edited |= ImGui::SliderFloat("Terrain ambient softening",
                                     &scenic_material_.terrain_ambient_softening, 0.0F, 1.0F);
        bool integrated = scenic_material_.terrain_diffuse_convolution > 0.0F;
        if (ImGui::Checkbox("Integrated terrain diffuse", &integrated)) {
            scenic_material_.terrain_diffuse_convolution = integrated ? 1.0F : 0.0F;
            edited = true;
        }
        edited |= ImGui::SliderFloat("Terrain specular",
                                     &scenic_material_.terrain_specular_scale, 0.0F, 1.0F);
        edited |= ImGui::SliderFloat("Terrain shadows", &scenic_material_.terrain_shadow_scale,
                                     0.0F, 1.0F);
        if (ImGui::Checkbox("Whole-scene shadow fit", &render_.native_shadow_scene_fit))
            config_.native_shadow_projection =
                render_.native_shadow_scene_fit ? "scene" : "legacy";
        edited |= ImGui::SliderFloat("Slope color contrast",
                                     &scenic_material_.terrain_slope_color_scale, 0.0F, 1.0F);
        int readability = int(scenic_material_.terrain_readability);
        if (ImGui::Combo(
                "Terrain detail", &readability,
                "Previous reference\0Periodic detail off\0Readable (Macro default)\0")) {
            scenic_material_.terrain_readability = float(readability);
            edited = true;
        }
        constexpr std::array terrain_views{
            "shaded",        "terrain-only", "albedo",      "weights",    "base-normal",
            "detail-normal", "roughness",    "direct",      "ambient",    "constant-albedo",
            "no-detail",     "face-normal",  "no-specular", "no-shadows", "specular-only"};
        int terrain_view =
            static_cast<int>(fluid_25d_scenic_terrain_view(config_.native_scenic_terrain_view));
        if (ImGui::Combo("Terrain component", &terrain_view, terrain_views.data(),
                         static_cast<int>(terrain_views.size()))) {
            config_.native_scenic_terrain_view =
                terrain_views[static_cast<std::size_t>(terrain_view)];
            if (terrain_view != 0)
                config_.native_scenic_water_view = "shaded";
            report_scenic_material();
        }
        if (terrain_view != 0)
            ImGui::TextWrapped("Terrain-only diagnostic: water and dots hidden, native fields "
                               "unchanged. Colors retain Scenic tonemapping.");
        if (edited) {
            scenic_material_edited_ = true;
            report_scenic_material();
        }
        if (config_.native_scenic_tuning_path || scenic_material_edited_)
            ImGui::TextWrapped("Custom settings; selecting a preset discards these overrides.");
        ImGui::TextWrapped(
            "Thin water uses wet-ground shading. Shallow bed tint is an artistic "
            "color cue, not transparency or a depth reading. Material edits "
            "do not move terrain or modify water fields. Bank controls trim only "
            "the displayed water edge. JSON overrides load "
            "once; effective settings are logged.");
    }
    const auto selected = render_.native_display_coverage  ? Fluid25DBankView::MarchingSquares
                          : render_.native_bspline_surface ? Fluid25DBankView::Bspline2x
                                                           : Fluid25DBankView::Reference;
    constexpr std::array<const char*, 3> names{"Triangular reference",
                                               "B-spline 2x (experimental)",
                                               "Marching-squares coverage (recorded)"};
    const bool advanced =
        render_.native_bilinear_water ||
        (render_.native_bspline_surface && render_.native_surface_subdivision != 2U);
    ImGui::BeginDisabled(comparison_ && !coverage_cache_);
    ImGui::SetNextItemWidth(-100.0F);
    if (ImGui::BeginCombo("Banks [S]", advanced ? "Advanced reconstruction"
                                                : names[static_cast<std::size_t>(selected)])) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            const auto mode = static_cast<Fluid25DBankView>(i);
            const bool available =
                mode != Fluid25DBankView::MarchingSquares || matching_mask_ready();
            ImGui::BeginDisabled(!available);
            if (ImGui::Selectable(names[i], !advanced && mode == selected))
                set_bank_view(mode);
            ImGui::EndDisabled();
            if (!available && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Requires a validated completed-recording mask at the "
                                  "current saved time. No live reconstruction.");
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (selected == Fluid25DBankView::Reference)
        ImGui::TextWrapped("Original height geometry and native field sampling; cell-scale "
                           "angular banks remain.");
    else if (selected == Fluid25DBankView::Bspline2x)
        ImGui::TextWrapped("Rounds displayed terrain and water. Can widen/bead streams or "
                           "falsely connect thin gaps; no extra simulated water.");
    else
        ImGui::TextWrapped("Marching squares + bounded point smoothing changes coverage only. "
                           "Height geometry stays triangular. Baked %s; not live.",
                           coverage_->label().c_str());
    if (!matching_mask_ready())
        ImGui::TextWrapped("Marching squares: recorded masks only; unavailable here.");
    if (view_ == Fluid25DPresentationView::Diagnostics)
        ImGui::TextWrapped("RAW 2D FIELDS: bank reconstruction is bypassed; selected bank view "
                           "resumes in 3D.");
    else
        ImGui::TextWrapped("Bank views are display-only; native inputs stay unchanged.");
    ImGui::BeginDisabled(!markers_available_);
    ImGui::Checkbox("Dots/trails [W] - approximate velocity", &config_.motion_markers);
    ImGui::EndDisabled();
    if (!markers_available_)
        ImGui::TextWrapped(
            "Enable dots at launch; comparison mode prepares them automatically.");
    if (ImGui::CollapsingHeader("Advanced reconstruction / display diagnostics")) {
        int sampling = render_.native_bspline_surface  ? 2
                       : render_.native_bilinear_water ? 1
                                                       : 0;
        ImGui::BeginDisabled(comparison_.has_value());
        ImGui::SetNextItemWidth(-100.0F);
        if (ImGui::Combo("Surface", &sampling,
                         "Triangular reference\0Bilinear depth only\0Matched B-spline "
                         "terrain/water\0")) {
            render_.native_bilinear_water = sampling == 1;
            render_.native_bspline_surface = sampling == 2;
            render_.native_display_coverage = false;
            coverage_upload_pending_ = false;
            config_.native_water_sampling = sampling == 2   ? "bspline"
                                            : sampling == 1 ? "bilinear"
                                                            : "triangular";
        }
        if (render_.native_bspline_surface) {
            int level = render_.native_surface_subdivision == 4U   ? 2
                        : render_.native_surface_subdivision == 2U ? 1
                                                                   : 0;
            ImGui::TextWrapped("Display subdivision only; simulation resolution is unchanged.");
            ImGui::SetNextItemWidth(-100.0F);
            if (ImGui::Combo("Subdivisions", &level, "1x\0 2x\0 4x\0"))
                config_.native_surface_subdivision = render_.native_surface_subdivision =
                    1U << level;
        }
        ImGui::EndDisabled();
        int diagnostic = static_cast<int>(render_.native_water_debug);
        ImGui::SetNextItemWidth(-100.0F);
        if (ImGui::Combo(
                "Water diagnostic", &diagnostic,
                "Shaded\0Solid coverage\0Unlit depth\0Normals\0Wireframe\0Native/display "
                "wet mask\0No terrain occlusion (diagnostic)\0Fixed depth contours\0"))
            render_.native_water_debug = static_cast<std::uint32_t>(diagnostic);
    }
}

void RecordingApp::draw_ui(ManagedSceneReload* demo) {
    if (external_session_) {
        draw_external_ui(demo);
        return;
    }
    ImGui::SetNextWindowSize(
        ImVec2(510.0F, std::min(850.0F, ImGui::GetIO().DisplaySize.y - 32.0F)),
        ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(16.0F, 16.0F), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(comparison_           ? "Terrain Water - BANKS recorded comparison"
                     : config_.stream_path ? "Terrain Water - external field stream"
                                           : "Terrain Water - REPLAY recorded fields")) {
        const Fluid25DRecordedRainStatus rain = recording_->rain_status_at(shown_->time_s);
        ImGui::SeparatorText("Rainfall - recorded forcing (read only)");
        ImGui::Text("Saved rain: %s | %.2f mm/h",
                    fluid_25d_recorded_rain_phase_name(rain.phase), rain.rate_mm_per_hour);
        ImGui::Text("Weather at playhead %.2f mm/h (water held at %.0f s)",
                    applied_rain_mm_per_hour().value_or(0.0), shown_->time_s);
        ImGui::Text("Scheduled total %.3f mm (not retained water)",
                    rain.cumulative_scheduled_rain_depth_mm);
        ImGui::TextWrapped(
            "Fixed recorded history. Use LIVE solver mode to change intensity. "
            "Rain off stops supply, not runoff; viewing speed does not change rain.");
        ImGui::Separator();
        if (comparison_) {
            ImGui::TextColored(ImVec4(.25F, .85F, 1.F, 1.F),
                               "RECORDED BANK COMPARISON - prebaked, not live");
            ImGui::Text("Window %.0f..%.3f s | %zu saved masks", comparison_->first_s(),
                        comparison_->end_s(), comparison_->frame_count());
            if (coverage_cache_)
                ImGui::Text("Prepared %zu masks | %.1f MiB | %.1f ms once",
                            coverage_cache_->frame_count(),
                            static_cast<double>(coverage_cache_->bytes()) / (1024. * 1024.),
                            coverage_cache_->preparation_ms());
            else if (comparison_error_.empty())
                ImGui::TextWrapped("Preparing coverage %zu/%zu; reference shown, playback held",
                                   cache_progress_->load(), comparison_->frame_count());
            else
                ImGui::TextWrapped("PREPARATION FAILED: %s", comparison_error_.c_str());
            ImGui::Checkbox("Loop recorded window (not simulation)", &comparison_loop_);
            if (comparison_at_end_)
                ImGui::TextWrapped(
                    "END OF COMPARISON WINDOW: last saved state held. Replay/Restart is "
                    "explicit; the solver has not been stopped.");
            draw_native_presentation_ui();
            draw_native_color_legend();
            ImGui::Separator();
        }
        if (config_.stream_path) {
            ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                               "LIVE EXTERNAL SYNXFLOW - Vulkan viewer only");
            ImGui::Text("Producer: %s | native %.0f s | available %.0f s",
                        recording_->producer_state().c_str(),
                        recording_->latest_native_time_s(), recording_->times_s().back());
            ImGui::Text("Native solver: %s", recording_->native_running()
                                                 ? "COMPUTING"
                                                 : "NOT COMPUTING (import/audit may continue)");
            ImGui::Text("Displayed %.0f s | import lag %.0f physical s", shown_->time_s,
                        recording_->latest_native_time_s() - recording_->times_s().back());
            ImGui::TextWrapped("%s", recording_->producer_message().c_str());
            if (!stream_error_.empty())
                ImGui::TextWrapped("VIEWER ERROR: %s", stream_error_.c_str());
            if (recording_->producer_state() == "running" && clock_.ended())
                ImGui::TextUnformatted("WAITING FOR NEXT SNAPSHOT");
            if (ImGui::Checkbox("Follow latest (may skip saved states)", &follow_latest_))
                reset_visual_history();
            if (ImGui::Button(live_playing_ ? "Pause viewing [Space]"
                                            : "Resume viewing [Space]"))
                live_playing_ = !live_playing_;
            ImGui::TextWrapped(
                "Viewing controls never pause computation or change rainfall. Closing this "
                "window detaches; the launcher owns the finite run.");
        } else if (!comparison_) {
            ImGui::TextColored(ImVec4(0.25F, 0.85F, 1.0F, 1.0F),
                               "REPLAY - recorded SynxFlow; no solver running");
            draw_native_color_legend();
            ImGui::TextWrapped("Space pauses viewing, not computation.");
        }
        if (ImGui::CollapsingHeader("Backend details")) {
            ImGui::TextWrapped("Adapter: %s", backend_metadata_.id.c_str());
            ImGui::Text("Input: %.12s | solver bed: %.12s",
                        backend_metadata_.grid.input_sha256.c_str(),
                        backend_metadata_.grid.solver_bed_sha256.c_str());
            ImGui::TextUnformatted("Fields: saved depth, momentum; derived velocity");
            ImGui::TextDisabled("No face discharge, native ledger or tracer fields.");
            ImGui::TextDisabled("Playback controls only; no numerical solver commands.");
            ImGui::Text(
                "Viewer generation %llu | sequence %llu | playhead %.6f s",
                static_cast<unsigned long long>(backend_metadata_.session.reset_generation),
                static_cast<unsigned long long>(backend_metadata_.session.frame_sequence),
                backend_metadata_.session.physical_time_s);
            ImGui::TextDisabled("Logical playback boundary, not the native producer clock; "
                                "saved field time below.");
        }
        ImGui::Text("Saved state: %.0f min %.0f s / %.0f min",
                    std::floor(shown_->time_s / 60.0), std::fmod(shown_->time_s, 60.0),
                    recording_->protocol().value("duration_s", recording_->times_s().back()) /
                        60.0);
        if (shown_index_ != clock_.frame_index())
            ImGui::Text("Loading requested state %.0f s...", clock_.time_s());
        else
            ImGui::TextWrapped("%s | playhead %.1f s | %.1fx viewing speed",
                               comparison_at_end_ ? "END OF COMPARISON WINDOW"
                               : clock_.ended()
                                   ? (config_.stream_path
                                          ? (recording_->producer_state() == "running"
                                                 ? "WAITING FOR NEXT SNAPSHOT"
                                                 : "END OF FINITE SOURCE")
                                          : "END OF RECORDING")
                               : clock_.paused() ? "PAUSED"
                                                 : "PLAYING",
                               clock_.time_s(), clock_.rate());
        ImGui::BeginDisabled((!comparison_ && clock_.ended()) ||
                             config_.stream_path.has_value() ||
                             (comparison_ && !coverage_cache_));
        if (ImGui::Button(comparison_at_end_ ? "Replay window [Space]"
                          : clock_.paused()  ? "Play viewing [Space]"
                                             : "Pause viewing [Space]"))
            toggle_playback();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Restart [R]")) {
            restart_playback();
        }
        ImGui::SameLine();
        if (ImGui::Button("<")) {
            clock_.previous();
            constrain_comparison_clock(false);
            live_playing_ = false;
            follow_latest_ = false;
            reset_visual_history();
        }
        ImGui::SameLine();
        if (ImGui::Button(">")) {
            clock_.next();
            constrain_comparison_clock(false);
            live_playing_ = false;
            follow_latest_ = false;
            reset_visual_history();
            if (playback_session_->metadata().session.lifecycle != Fluid25DLifecycle::Completed)
                playback_navigation_kind_ = Fluid25DCommandKind::Step;
        }
        float time = static_cast<float>(clock_.time_s() / 60.0);
        if (ImGui::SliderFloat(
                "Seek (minutes)", &time,
                static_cast<float>(
                    (comparison_ ? comparison_->first_s() : recording_->times_s().front()) /
                    60.0),
                static_cast<float>(
                    (comparison_ ? comparison_->end_s() : recording_->times_s().back()) / 60.0),
                "%.1f")) {
            seek_playback(static_cast<double>(time) * 60.0);
        }
        double taper_start_s = -1.0;
        double rain_zero_s = -1.0;
        const auto rain_history = recording_->rainfall_history();
        for (std::size_t index = 0U; index + 1U < rain_history.size(); ++index) {
            const auto& start = rain_history[index];
            const auto& end = rain_history[index + 1U];
            if (taper_start_s < 0.0 && start.rate_m_per_s > end.rate_m_per_s &&
                start.rate_m_per_s > 0.0) {
                taper_start_s = start.time_s;
            }
            if (rain_zero_s < 0.0 && start.rate_m_per_s > 0.0 && end.rate_m_per_s == 0.0) {
                rain_zero_s = end.time_s;
            }
        }
        const auto seek_to_recorded_time = [this](const char* label, double seek_time_s) {
            if (ImGui::Button(label)) {
                seek_playback(seek_time_s);
            }
        };
        if (comparison_) {
            seek_to_recorded_time("Window start", comparison_->first_s());
            ImGui::SameLine();
            seek_to_recorded_time("Window end", comparison_->end_s());
        } else {
            if (taper_start_s >= 0.0) {
                seek_to_recorded_time("Rain taper start", taper_start_s);
                ImGui::SameLine();
            }
            if (rain_zero_s >= 0.0) {
                seek_to_recorded_time("Rain zero", rain_zero_s);
                ImGui::SameLine();
                seek_to_recorded_time(
                    "Zero +30m", std::min(rain_zero_s + 1800.0, recording_->times_s().back()));
                ImGui::SameLine();
                seek_to_recorded_time(
                    "Zero +1h", std::min(rain_zero_s + 3600.0, recording_->times_s().back()));
                ImGui::SameLine();
            }
            seek_to_recorded_time("End", recording_->times_s().back());
        }
        float rate = static_cast<float>(clock_.rate());
        if (ImGui::SliderFloat("Playback x", &rate, 0.125F, 300.0F, "%.1fx",
                               ImGuiSliderFlags_Logarithmic))
            clock_.set_rate(rate);
        ImGui::TextWrapped(
            "Fields are held between saved states (%.0f s here). Seeking pauses and "
            "clears visual history. End holds the final state; restart is explicit.",
            recording_->protocol().value("output_interval_s", 0.0));
        ImGui::Separator();
        for (const char* name : {"overview", "runoff", "collection"}) {
            if (ImGui::RadioButton(name == std::string_view("overview") ? "Full patch" : name,
                                   config_.recording_camera == name)) {
                select_camera(name);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();
        int mode = static_cast<int>(catchment_view_);
        if (ImGui::Combo("3D reading", &mode,
                         "Composite\0Water isolation\0Flow inspection\0")) {
            catchment_view_ = static_cast<Fluid25DCatchmentView>(mode);
            view_ = Fluid25DPresentationView::Catchment;
            quiver_reset_ = true;
        }
        bool diagnostics = view_ == Fluid25DPresentationView::Diagnostics;
        if (ImGui::Checkbox("2D diagnostics [A]", &diagnostics))
            view_ = diagnostics ? Fluid25DPresentationView::Diagnostics
                                : Fluid25DPresentationView::Catchment;
        if (diagnostics) {
            int debug = static_cast<int>(debug_view_);
            if (ImGui::Combo("Map [D]", &debug,
                             "Terrain\0Depth\0Surface\0Speed\0Direction\0Wet/dry\0"))
                debug_view_ = static_cast<Fluid25DDebugView>(debug);
        }
        if (scenic_material_active())
            ImGui::TextWrapped("Scenic color is not a depth palette; use raw maps to measure.");
        else
            ImGui::Text("Depth color: log scale 0.01 / 0.1 / 1 / 10 m");
        if (!comparison_)
            draw_native_presentation_ui();
        if (diagnostics)
            ImGui::Text("2D speed: 0..15 m/s (saturates above 15)");
        ImGui::TextWrapped(
            "Composite attenuates thin rain film. Water isolation shows all wet cells. Arrows: "
            "fixed grid, smoothed neighborhood velocity; 0.025..4 m/s (saturates above 4).");
        ImGui::Text("Storage %.3f million m3 | deepest %.3f m",
                    shown_->stats.water_volume_m3 / 1.0e6, shown_->stats.max_depth_m);
        ImGui::Text("Maximum saved speed %.3f m/s | grid %.0f m",
                    shown_->stats.max_speed_m_per_s, simulation_.cell_size_m);
        ImGui::Text("Peak scheduled rain %.2f mm/h | vertical scale %.2fx",
                    recording_->rain_rate_mm_per_hour(), *render_.terrain_height_scale);
        ImGui::TextWrapped("Source is rainfall over the whole map. Native 'fall' perimeter "
                           "lets runoff leave; there is no point source/drain. Terrain is "
                           "unchanged. No native face ledger or Cubey CFL/oracle claim.");
        if (config_.motion_markers)
            ImGui::TextWrapped("Dots/trails are local visual cues advected in held saved "
                               "velocities, not native simulated parcels or conserved dye.");
        ImGui::Text("Drag to orbit; scroll to zoom; Esc closes.");
    }
    ImGui::End();
}

} // namespace cubey::projects::fluid::fluid_25d::recording_detail
