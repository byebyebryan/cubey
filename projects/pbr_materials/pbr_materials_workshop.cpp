#include "pbr_materials_app_internal.h"
#include <cubey/scene/transform_3d.h>
#include <imgui.h>
#include <iostream>
#include <algorithm>

namespace cubey::projects::pbr_materials::detail {
render::PbrMaterialDefinition PbrMaterialsApp::material_definition(std::size_t i) const {
    auto d = pbr_material_from_recipe(recipes_.at(i), .5F, static_cast<std::uint32_t>(i));
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
        auto wrong_route = material_definition(0);
        wrong_route.factors.transmission_factor = 1;
        bool rejected = false;
        try { table.update_after_retire(material_handles_[0], wrong_route); }
        catch (const std::runtime_error&) { rejected = true; }
        if (!rejected || table.definition(material_handles_[0]).factors.transmission_factor != 0)
            throw std::runtime_error("routing-changing edit was published");
        for (std::uint32_t i = 0; i < config_.stress_edits; ++i) {
            auto d = material_definition(0);
            d.factors.roughness_factor = .05F + .95F * static_cast<float>(i % 97) / 96;
            table.update_after_retire(material_handles_[0], std::move(d));
        }
        const auto after = table.metrics();
        if (after.allocated_uniform_byte_size != before.allocated_uniform_byte_size ||
            after.allocated_descriptor_set_count != before.allocated_descriptor_set_count ||
            after.block_count != before.block_count)
            throw std::runtime_error("live material edits grew residency");
        std::cout << "pbr_materials: stress slot=" << slot.index << " edits=" << config_.stress_edits
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
    for (std::size_t i = 0; i < cubes_.size(); ++i) {
        const bool gallery = selected_ < 0;
        edits.transforms3d().set_local_transform(cubes_[i].entity,
            {.translation = gallery ? math::Vec3{(static_cast<float>(i)-3)*kMaterialGridSpacingX,0,0} : math::Vec3{0}});
        edits.renderables3d().set_renderable(cubes_[i].entity,
            {.primitives = {{.mesh = config_.specimen == "block" ? block_mesh_handle_ : cube_mesh_handle_,
                              .material = material_handles_[i]}},
             .local_bounds = {.half_extent = {.25F,.25F,.25F}},
             .visible = gallery || static_cast<int>(i) == selected_,
             .cast_shadows = false, .receive_shadows = false});
    }
    scene().commit(edits);
}
void PbrMaterialsApp::draw_ui() {
    ImGui::SetNextWindowSize({350,0},ImGuiCond_FirstUseEver);
    if (ImGui::Begin("PBR material workshop")) {
        const char* names[]{"Gallery", "Neutral gray", "Limestone", "Balanced marble", "Dry terracotta", "Ivory ceramic", "Satin bronze", "Tinted glass"};
        int selected = selected_ + 1;
        if (ImGui::Combo("Material", &selected, names, 8)) {
            selected_ = selected-1;
            orbit_controller_.set_home_distance(selected_ < 0 ? kCameraDistance : config_.distance);
            orbit_controller_.reset(); orbit_controller_.set_pitch(config_.pitch);
            refresh_specimens();
        }
        const char* shapes[]{"Sphere", "Beveled block"};
        int shape = config_.specimen == "block" ? 1 : 0;
        if (ImGui::Combo("Specimen", &shape, shapes, 2)) {
            config_.specimen = shape ? "block" : "sphere"; refresh_specimens();
        }
        bool changed = ImGui::Checkbox("Pattern", &config_.pattern);
        changed |= ImGui::Checkbox("Bump", &config_.bump);
        if (selected_ >= 0) {
            auto& r = recipes_.at(static_cast<std::size_t>(selected_));
            const bool glass = r.optics.settings.y > 0;
            ImGui::TextUnformatted(r.label.c_str());
            ImGui::BeginDisabled(glass);
            changed |= ImGui::ColorEdit3("Linear base color", &r.surface.base_roughness.x);
            changed |= ImGui::SliderFloat("Roughness", &r.surface.base_roughness.w, .05F, 1);
            changed |= ImGui::SliderFloat("Diffuse roughness", &r.optics.attenuation.w, 0, 1);
            ImGui::EndDisabled();
            if (r.pattern.settings.x > 0) {
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
                ImGui::TextWrapped("Forward refraction with a 0.5-unit thickness estimate. SDF measures its interface segments instead.");
                changed |= ImGui::SliderFloat("IOR", &r.optics.settings.z, 1, 2.5F);
                changed |= ImGui::ColorEdit3("Attenuation", &r.optics.attenuation.x);
                for (int i=0;i<3;++i) r.optics.attenuation[i]=std::max(.001F,r.optics.attenuation[i]);
                changed |= ImGui::SliderFloat("Attenuation distance", &r.optics.settings.w, .01F, 4);
            }
            if (ImGui::Button("Reset material")) { r = originals_.at(static_cast<std::size_t>(selected_)); changed = true; }
            ImGui::InputText("Recipe path", recipe_path_, sizeof(recipe_path_));
            if (ImGui::Button("Save recipe")) {
                try { asset::save_material_recipe(recipe_path_, r); ui_error_.clear(); }
                catch (const std::exception& e) { ui_error_=e.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload recipe")) {
                try {
                    auto next = asset::load_material_recipe(recipe_path_);
                    if ((next.optics.settings.y>0) != glass) throw std::runtime_error("Select the glass or opaque slot before reloading that recipe.");
                    r = std::move(next); changed=true; ui_error_.clear();
                } catch (const std::exception& e) { ui_error_=e.what(); }
            }
        } else ImGui::TextWrapped("Select a material for close inspection and editing. Left-drag or scroll to orbit and zoom.");
        if (changed) ++revision_;
        if (ImGui::Button("Next component view")) debug_view_=render::next_pbr_debug_view(debug_view_);
        ImGui::SameLine(); ImGui::TextUnformatted(render::pbr_debug_view_name(debug_view_).data());
        if (ImGui::Button("Reset camera")) { orbit_controller_.reset(); orbit_controller_.set_pitch(config_.pitch); }
        if (!ui_error_.empty()) ImGui::TextWrapped("%s",ui_error_.c_str());
    }
    ImGui::End();
}
}
