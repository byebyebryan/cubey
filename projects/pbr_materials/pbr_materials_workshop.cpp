#include "pbr_materials_app_internal.h"
#include <algorithm>
#include <cubey/scene/transform_3d.h>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <iostream>

namespace cubey::projects::pbr_materials::detail {
render::PbrMaterialDefinition PbrMaterialsApp::material_definition(std::size_t i) const {
    std::string_view shape = config_.specimen;
    if (!config_.presentation.empty())
        for (const auto& item : presentation_layout(config_.presentation))
            if (item.material == catalog_.at(i).id)
                shape = item.shape;
    auto d = pbr_material_from_recipe(
        recipes_.at(i), config_.presentation == "library" ? .15F : specimen_thickness(shape),
        static_cast<std::uint32_t>(i));
    d.factors.pattern_enabled = config_.pattern;
    d.factors.bump_enabled = config_.bump;
    return d;
}
void PbrMaterialsApp::publish_materials(render::FrameSlot slot) {
    auto& table = materials_.at(slot.index);
    // The host has waited this frame slot's fence before record_frame. Each
    // table is used only by its corresponding slot, including CPU routing.
    if (config_.stress_edits && !stress_done_.at(slot.index)) {
        const auto before = table.metrics();
        const std::size_t index = selected_ < 0 ? 0 : static_cast<std::size_t>(selected_);
        const auto route = material_definition(index).factors.transmission_factor;
        auto wrong_route = material_definition(index);
        wrong_route.factors.transmission_factor = route > 0 ? 0.0F : 1.0F;
        bool rejected = false;
        try {
            table.update_after_retire(material_handles_[index], wrong_route);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        if (!rejected ||
            table.definition(material_handles_[index]).factors.transmission_factor != route)
            throw std::runtime_error("routing-changing edit was published");
        for (std::uint32_t i = 0; i < config_.stress_edits; ++i) {
            auto d = material_definition(index);
            d.factors.roughness_factor = .05F + .95F * static_cast<float>(i % 97) / 96;
            if (d.factors.surface_library.settings.x > 0)
                d.factors.surface_library.settings.y = static_cast<float>(i % 65536);
            if (d.factors.surface_library.settings.x == 2)
                d.factors.surface_library.rust.coverage = static_cast<float>(i % 101) / 100;
            table.update_after_retire(material_handles_[index], std::move(d));
        }
        const auto after = table.metrics();
        if (after.allocated_uniform_byte_size != before.allocated_uniform_byte_size ||
            after.allocated_descriptor_set_count != before.allocated_descriptor_set_count ||
            after.block_count != before.block_count)
            throw std::runtime_error("live material edits grew residency");
        std::cout << "pbr_materials: stress slot=" << slot.index
                  << " edits=" << config_.stress_edits
                  << " descriptors=" << after.allocated_descriptor_set_count
                  << " uniform_bytes=" << after.allocated_uniform_byte_size << " growth=0\n";
        published_revision_[slot.index] = 0;
        stress_done_[slot.index] = true;
    }
    if (published_revision_.at(slot.index) != revision_) {
        for (std::size_t i = 0; i < recipes_.size(); ++i)
            table.update_after_retire(material_handles_[i], material_definition(i));
        published_revision_[slot.index] = revision_;
    }
}
void PbrMaterialsApp::refresh_specimens() {
    auto edits = scene().create_edit_queue();
    if (!config_.presentation.empty()) {
        const auto layout = presentation_layout(config_.presentation);
        for (std::size_t i = 0; i < specimens_.size(); ++i) {
            const auto found = std::find_if(layout.begin(), layout.end(), [&](const auto& item) {
                return item.material == catalog_[i].id;
            });
            const bool visible = found != layout.end();
            if (visible)
                edits.transforms3d().set_local_transform(
                    specimens_[i].entity,
                    {.translation = found->position, .rotation = glm::quat(found->rotation)});
            edits.renderables3d().set_renderable(
                specimens_[i].entity,
                {.primitives = {{.mesh = config_.presentation == "library"
                                             ? *library_sphere_mesh_handle_
                                         : !visible || found->shape == "sphere"
                                             ? sphere_mesh_handle_
                                         : found->shape == "swatch" ? swatch_mesh_handle_
                                                                    : cut_block_mesh_handle_,
                                 .material = material_handles_[i]}},
                 .local_bounds = {.half_extent = {.25F, .25F, .25F}},
                 .visible = visible,
                 .cast_shadows = false,
                 .receive_shadows = false});
        }
        scene().commit(edits);
        return;
    }
    const bool gallery = selected_ < 0;
    const auto matches = [&](std::size_t i) {
        return family_filter_ == "all" || catalog_[i].family == family_filter_;
    };
    std::size_t visible = 0;
    for (std::size_t i = 0; i < catalog_.size(); ++i)
        if (matches(i))
            ++visible;
    const std::size_t columns =
        std::max<std::size_t>(1, std::min<std::size_t>(kMaterialGridColumns, visible));
    const std::size_t rows = (visible + columns - 1) / columns;
    std::size_t cell = 0;
    for (std::size_t i = 0; i < specimens_.size(); ++i) {
        const auto column = cell % columns, row = cell / columns;
        const math::Vec3 position{
            (static_cast<float>(column) - static_cast<float>(columns - 1) * .5F) *
                kMaterialGridSpacingX,
            (static_cast<float>(rows - 1) * .5F - static_cast<float>(row)) * kMaterialGridSpacingY,
            0};
        if (matches(i))
            ++cell;
        edits.transforms3d().set_local_transform(
            specimens_[i].entity, {.translation = gallery ? position : math::Vec3{0}});
        edits.renderables3d().set_renderable(
            specimens_[i].entity,
            {.primitives = {{.mesh = config_.specimen == "swatch"      ? swatch_mesh_handle_
                                     : config_.specimen == "cut-block" ? cut_block_mesh_handle_
                                     : config_.specimen == "block"     ? block_mesh_handle_
                                                                       : sphere_mesh_handle_,
                             .material = material_handles_[i]}},
             .local_bounds = {.half_extent = {.25F, .25F, .25F}},
             .visible = gallery ? matches(i) : static_cast<int>(i) == selected_,
             .cast_shadows = false,
             .receive_shadows = false});
    }
    scene().commit(edits);
}
void PbrMaterialsApp::leave_presentation() {
    if (config_.presentation.empty())
        return;
    config_.presentation.clear();
    config_.lighting_cycle = "frozen";
    ++revision_;
    orbit_controller_.set_home_distance(selected_ < 0 ? kCameraDistance : config_.distance);
    orbit_controller_.reset();
    orbit_controller_.set_pitch(config_.pitch);
}
void PbrMaterialsApp::draw_ui() {
    ImGui::SetNextWindowSize({420, 640}, ImGuiCond_FirstUseEver);
    if (!config_.presentation.empty())
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    if (ImGui::Begin("PBR material workshop")) {
        if (!config_.presentation.empty()) {
            ImGui::Text("Surface presentation / %s", config_.presentation.c_str());
            if (ImGui::Button(orbit_controller_.paused() ? "Play presentation"
                                                         : "Pause presentation"))
                orbit_controller_.toggle_pause();
            ImGui::SameLine();
            if (ImGui::Button("Replay presentation")) {
                presentation_seconds_ = 0;
                orbit_controller_.reset();
                orbit_controller_.set_pitch(config_.pitch);
            }
            float t = static_cast<float>(presentation_seconds_);
            if (ImGui::SliderFloat("Presentation time", &t, 0, 8, "%.2f s")) {
                presentation_seconds_ = t;
                if (!orbit_controller_.paused())
                    orbit_controller_.toggle_pause();
            }
            if (ImGui::Button("Open material workshop")) {
                leave_presentation();
                refresh_specimens();
            }
            ImGui::Separator();
        }
        if (ImGui::BeginCombo("Family", family_filter_.c_str())) {
            std::vector<std::string> families{"all"};
            for (const auto& e : catalog_)
                if (std::find(families.begin(), families.end(), e.family) == families.end())
                    families.push_back(e.family);
            for (const auto& family : families)
                if (ImGui::Selectable(family.c_str(), family_filter_ == family)) {
                    family_filter_ = family;
                    selected_ = -1;
                    leave_presentation();
                    refresh_specimens();
                    orbit_controller_.set_home_distance(kCameraDistance);
                    orbit_controller_.reset();
                    orbit_controller_.set_pitch(config_.pitch);
                }
            ImGui::EndCombo();
        }
        const char* selected_label =
            selected_ < 0 ? "Gallery" : recipes_[static_cast<std::size_t>(selected_)].label.c_str();
        if (ImGui::BeginCombo("Material", selected_label)) {
            if (ImGui::Selectable("Gallery", selected_ < 0)) {
                selected_ = -1;
                leave_presentation();
                orbit_controller_.set_home_distance(kCameraDistance);
                orbit_controller_.reset();
                orbit_controller_.set_pitch(config_.pitch);
                refresh_specimens();
            }
            for (std::size_t i = 0; i < catalog_.size(); ++i) {
                if (family_filter_ != "all" && catalog_[i].family != family_filter_)
                    continue;
                if (ImGui::Selectable(catalog_[i].label.c_str(),
                                      selected_ == static_cast<int>(i))) {
                    selected_ = static_cast<int>(i);
                    leave_presentation();
                    orbit_controller_.set_home_distance(config_.distance);
                    orbit_controller_.reset();
                    orbit_controller_.set_pitch(config_.pitch);
                    refresh_specimens();
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::Button("Reload library recipes")) {
            try {
                auto candidate = asset::load_material_catalog(
                    std::filesystem::path(CUBEY_MATERIAL_RECIPE_DIR) / "catalog.json");
                if (candidate.size() != catalog_.size())
                    throw std::runtime_error("Restart the workshop to change the catalog roster.");
                std::vector<asset::MaterialRecipe> next;
                for (std::size_t i = 0; i < candidate.size(); ++i) {
                    if (candidate[i].id != catalog_[i].id ||
                        (candidate[i].recipe.optics.settings.y > 0) !=
                            (originals_[i].optics.settings.y > 0))
                        throw std::runtime_error(
                            "Restart the workshop to change catalog IDs or optical routing.");
                    next.push_back(candidate[i].recipe);
                }
                catalog_ = std::move(candidate);
                recipes_ = std::move(next);
                originals_ = recipes_;
                family_filter_ = "all";
                ++revision_;
                refresh_specimens();
                ui_error_.clear();
            } catch (const std::exception& e) {
                ui_error_ = e.what();
            }
        }
        const char* environments[]{"studio", "clear", "clouds"};
        int environment =
            config_.environment == "studio" ? 0 : (config_.environment == "clear" ? 1 : 2);
        if (ImGui::Combo("Lighting", &environment, environments, 3)) {
            requested_environment_ = environments[environment];
            if (environment == 0)
                config_.lighting_cycle = "frozen";
        }
        const char* shapes[]{"sphere", "block", "swatch", "cut-block"};
        int shape = config_.specimen == "block"       ? 1
                    : config_.specimen == "swatch"    ? 2
                    : config_.specimen == "cut-block" ? 3
                                                      : 0;
        if (ImGui::Combo("Specimen", &shape, shapes, 4)) {
            leave_presentation();
            config_.specimen = shapes[shape];
            ++revision_;
            refresh_specimens();
        }
        bool changed = ImGui::Checkbox("Pattern", &config_.pattern);
        changed |= ImGui::Checkbox("Bump", &config_.bump);
        if (selected_ >= 0) {
            auto& r = recipes_.at(static_cast<std::size_t>(selected_));
            const bool glass = r.optics.settings.y > 0;
            ImGui::TextUnformatted(r.label.c_str());
            ImGui::BeginDisabled(glass);
            changed |= ImGui::ColorEdit3("Linear base color", &r.surface.base_roughness.x);
            ImGui::BeginDisabled(r.library.settings.x == 2);
            changed |= ImGui::SliderFloat("Roughness", &r.surface.base_roughness.w, .05F, 1);
            changed |= ImGui::SliderFloat("Metallic", &r.optics.settings.x, 0, 1);
            ImGui::EndDisabled();
            changed |= ImGui::SliderFloat("Diffuse roughness", &r.optics.attenuation.w, 0, 1);
            ImGui::EndDisabled();
            auto& library = r.library;

            if (library.settings.x > 0) {
                int seed = static_cast<int>(library.settings.y);
                if (ImGui::SliderInt("Library seed", &seed, 0, 65535)) {
                    library.settings.y = static_cast<float>(seed);
                    changed = true;
                }
                changed |= ImGui::SliderFloat("Library scale", &library.settings.z, .1F, 16);
                changed |=
                    ImGui::SliderFloat("Relief height", &library.settings.w, 0, .005F, "%.5f");
                changed |= ImGui::ColorEdit3("Library secondary color", &library.secondary.x);
                if (library.settings.x == 1) {
                    changed |=
                        ImGui::SliderFloat("Ring frequency", &library.wood.ring_frequency, 1, 100);
                    changed |=
                        ImGui::SliderFloat("Growth distortion", &library.wood.distortion, 0, 2);
                    changed |= ImGui::SliderFloat("Fiber frequency", &library.wood.grain_frequency,
                                                  20, 600);
                    changed |=
                        ImGui::SliderFloat("Pore strength", &library.wood.pore_strength, 0, 1);
                } else if (library.settings.x == 2) {
                    changed |= ImGui::SliderFloat("Rust coverage", &library.rust.coverage, 0, 1);
                    changed |=
                        ImGui::SliderFloat("Patch frequency", &library.rust.patch_frequency, 1, 64);
                    changed |= ImGui::SliderFloat("Edge breakup", &library.rust.edge_breakup, 0, 1);
                    changed |= ImGui::SliderFloat("Pitting", &library.rust.pit_strength, 0, 1);
                    changed |=
                        ImGui::SliderFloat("Steel roughness", &library.rust_roughness.x, .05F, 1);
                    changed |=
                        ImGui::SliderFloat("Oxide roughness", &library.rust_roughness.y, .05F, 1);
                } else if (library.settings.x == 3) {
                    changed |= ImGui::SliderFloat("Yarn spacing", &library.weave.yarn_spacing,
                                                  .003F, .06F, "%.4f");
                    changed |=
                        ImGui::SliderFloat("Yarn width", &library.weave.yarn_width, .3F, .95F);
                    changed |= ImGui::SliderFloat("Yarn irregularity", &library.weave.irregularity,
                                                  0, .5F);
                    changed |=
                        ImGui::SliderFloat("Fine fibers", &library.weave.fiber_strength, 0, 1);
                    changed |= ImGui::ColorEdit3("Sheen color", &library.sheen.x);
                    changed |= ImGui::SliderFloat("Sheen roughness", &library.sheen.w, .05F, 1);
                    ImGui::TextWrapped("Weave follows the material X/Y axes. Use the swatch to "
                                       "inspect its structure.");
                } else {
                    changed |= ImGui::SliderFloat("Fiber frequency", &library.paper.fiber_frequency,
                                                  40, 1000);
                    changed |= ImGui::SliderFloat("Fiber directionality",
                                                  &library.paper.directionality, 0, 1);
                    changed |=
                        ImGui::SliderFloat("Pulp flecks", &library.paper.fleck_strength, 0, 1);
                    changed |=
                        ImGui::SliderFloat("Fiber relief", &library.paper.fiber_strength, 0, 1);
                }
            }
            if (r.pattern.settings.x > 0) {
                int seed = static_cast<int>(r.pattern.settings.y);
                if (ImGui::SliderInt("Seed", &seed, 0, 65535)) {
                    r.pattern.settings.y = static_cast<float>(seed);
                    changed = true;
                }
                changed |= ImGui::SliderFloat("Pattern scale", &r.pattern.settings.z, .25F, 64);
                changed |= ImGui::SliderFloat("Color strength", &r.pattern.settings.w, 0, 1);
                changed |= ImGui::ColorEdit3("Linear secondary", &r.pattern.secondary.x);
                changed |= ImGui::SliderFloat("Bump height", &r.pattern.detail.x, 0, .005F, "%.5f");
                if (r.pattern.settings.x == 2) {
                    changed |= ImGui::SliderFloat("Vein frequency", &r.pattern.veins.w, .25F, 32);
                    changed |= ImGui::SliderFloat("Cream variation", &r.pattern.offset.w, 0, .25F);
                }
            }
            if (glass) {
                if (r.library.optical_options.x > .5F)
                    changed |= ImGui::SliderFloat("Frost roughness", &r.surface.base_roughness.w,
                                                  .05F, .8F);
                ImGui::TextWrapped("Forward refraction with a %.3f-unit thickness estimate. SDF "
                                   "measures its interface segments instead.",
                                   specimen_thickness(config_.specimen));
                changed |= ImGui::SliderFloat("IOR", &r.optics.settings.z, 1, 2.5F);
                changed |= ImGui::ColorEdit3("Attenuation", &r.optics.attenuation.x);
                for (int i = 0; i < 3; ++i)
                    r.optics.attenuation[i] = std::max(.001F, r.optics.attenuation[i]);
                changed |=
                    ImGui::SliderFloat("Attenuation distance", &r.optics.settings.w, .01F, 4);
            }
            if (ImGui::Button("Reset material")) {
                r = originals_.at(static_cast<std::size_t>(selected_));
                changed = true;
            }
            ImGui::InputText("Recipe path", recipe_path_, sizeof(recipe_path_));
            if (ImGui::Button("Save recipe")) {
                try {
                    asset::save_material_recipe(recipe_path_, r);
                    ui_error_.clear();
                } catch (const std::exception& e) {
                    ui_error_ = e.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload recipe")) {
                try {
                    auto next = asset::load_material_recipe(recipe_path_);
                    if ((next.optics.settings.y > 0) != glass)
                        throw std::runtime_error(
                            "Select the glass or opaque slot before reloading that recipe.");
                    r = std::move(next);
                    changed = true;
                    ui_error_.clear();
                } catch (const std::exception& e) {
                    ui_error_ = e.what();
                }
            }
        } else
            ImGui::TextWrapped("Select a material for close inspection and editing. Left-drag or "
                               "scroll to orbit and zoom.");
        if (changed)
            ++revision_;
        if (ImGui::Button("Next component view"))
            debug_view_ = render::next_pbr_debug_view(debug_view_);
        ImGui::SameLine();
        ImGui::TextUnformatted(render::pbr_debug_view_name(debug_view_).data());
        if (ImGui::Button("Reset camera")) {
            orbit_controller_.reset();
            orbit_controller_.set_pitch(config_.pitch);
        }
        if (!ui_error_.empty())
            ImGui::TextWrapped("%s", ui_error_.c_str());
    }
    ImGui::End();
}
} // namespace cubey::projects::pbr_materials::detail
