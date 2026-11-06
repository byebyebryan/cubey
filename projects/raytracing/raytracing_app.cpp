#include "raytracing_config.h"
#include "raytracing_environment.h"
#include "raytracing_renderer.h"
#include <chrono>
#include <cstdio>
#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_app.h>
#include <cubey/input/orbit_controller.h>
#include <fstream>
#include <imgui.h>
#include <iostream>

namespace cubey::projects::raytracing {
namespace {
class App {
  public:
    explicit App(Config c) : config_(std::move(c)), scene_(make_scene(config_)) {
        cpu_scene_ms_ =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
                .count();
        if (!config_.scene_output.empty()) {
            nlohmann::json data;
            data["meshes"] = nlohmann::json::array();
            data["instances"] = nlohmann::json::array();
            for (const auto& mesh : scene_.meshes) {
                nlohmann::json m;
                m["positions"] = nlohmann::json::array();
                m["normals"] = nlohmann::json::array();
                for (const auto& v : mesh.vertices) {
                    m["positions"].push_back({v.position.x, v.position.y, v.position.z});
                    m["normals"].push_back({v.normal.x, v.normal.y, v.normal.z});
                }
                m["indices"] = mesh.indices;
                data["meshes"].push_back(m);
            }
            for (const auto& i : scene_.instances) {
                nlohmann::json matrix = nlohmann::json::array();
                for (int r = 0; r < 4; ++r)
                    matrix.push_back({i.transform[0][r], i.transform[1][r], i.transform[2][r],
                                      i.transform[3][r]});
                data["instances"].push_back({{"mesh", i.mesh},
                                             {"world", matrix},
                                             {"material", i.material},
                                             {"color", {i.color.x, i.color.y, i.color.z}}});
            }
            data["materials"] = nlohmann::json::array();
            for (const auto& m : scene_.materials)
                data["materials"].push_back(
                    {{"kind", m.optics.x},
                     {"base", {m.base_roughness.x, m.base_roughness.y, m.base_roughness.z}},
                     {"roughness", m.base_roughness.w},
                     {"global", m.optics.w},
                     {"reflectance", {m.reflectance.x, m.reflectance.y, m.reflectance.z}}});
            data["config"] = schema(config_).template_json();
            if (scene_.workshop) {
                data["workshop_recipes"] = scene_.workshop->identity();
                data["geometry_conversion"] =
                    "shared forward specimen positions/normals; uint16 indices widened to uint32; "
                    "opaque triangles oriented against supplied outward normals";
            }
            if (config_.scene_output.has_parent_path())
                std::filesystem::create_directories(config_.scene_output.parent_path());
            std::ofstream file(config_.scene_output);
            if (!file)
                throw std::runtime_error("cannot write RT scene identity");
            file << data.dump(2);
        }
        orbit_.set_home_distance(config_.distance);
        orbit_.set_distance(config_.distance);
        const auto recipe_path =
            config_.recipe_output.empty()
                ? std::filesystem::path("outputs/raytracing/material-workshop/edited-bronze.json")
                : config_.recipe_output;
        std::snprintf(recipe_path_.data(), recipe_path_.size(), "%s", recipe_path.string().c_str());
    }
    Config current() const {
        auto c = config_;
        c.yaw += orbit_.yaw() + config_.orbit_speed * float(presentation_seconds_);
        c.sun_azimuth += config_.sun_speed * float(presentation_seconds_);
        c.pitch += orbit_.pitch();
        c.distance = orbit_.distance();
        if (c.scene == "materials") {
            const float maximum_pitch = std::nextafter(1.5F, 0.F);
            c.pitch = std::clamp(c.pitch, -maximum_pitch, maximum_pitch);
        }
        if (c.environment == "atmosphere" && c.scene != "materials")
            c.pitch = std::clamp(
                c.pitch, std::asin(std::clamp((.05F - c.target_y) / c.distance, -.99F, .99F)),
                1.4F);
        return c;
    }
    void stress_edit(std::uint64_t frame) {
        if (!scene_.workshop || frame >= config_.stress_edits)
            return;
        auto& state = *scene_.workshop;
        auto recipe = state.recipes.at(state.selected);
        const float fraction = float(frame % 97) / 96.F;
        if (recipe.optics.settings.y > 0 && recipe.library.optical_options.x < .5F)
            recipe.optics.settings.z = 1.1F + fraction;
        else
            recipe.surface.base_roughness.w = .05F + .9F * fraction;
        state.replace(state.selected, std::move(recipe));
    }
    void create(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
                VkExtent2D extent, VkFormat format) {
        if (!renderer_) {
            if (config_.exercise_retry && config_.fault != "target") {
                bool failed = false;
                try {
                    Renderer rejected(device, gpu, scene_, slots, config_.fault, false, false,
                                      &config_);
                } catch (const std::runtime_error& e) {
                    if (std::string_view(e.what()).find("owned RT creation fault:") ==
                        std::string_view::npos)
                        throw;
                    std::cout << "raytracing: recovered " << e.what() << '\n';
                    failed = true;
                }
                if (!failed)
                    throw std::runtime_error("RT owned fault was not observed");
            }
            renderer_ = std::make_unique<Renderer>(
                device, gpu, scene_, slots, config_.exercise_retry ? "none" : config_.fault,
                dump_mode(config_) != 0, !config_.metrics.empty(), &config_);
        }
        if (config_.exercise_retry && config_.fault == "target" && !target_fault_used_) {
            bool failed = false;
            try {
                renderer_->resize(extent, format, true);
            } catch (const std::runtime_error& e) {
                if (std::string_view(e.what()).find("owned RT creation fault:") ==
                    std::string_view::npos)
                    throw;
                std::cout << "raytracing: recovered " << e.what() << '\n';
                failed = true;
            }
            if (!failed)
                throw std::runtime_error("RT target fault was not observed");
            target_fault_used_ = true;
        }
        renderer_->resize(extent, format, !config_.exercise_retry && config_.fault == "target");
    }
    void shutdown() {
        if (renderer_) {
            renderer_->finish();
            if (dump_mode(config_))
                renderer_->save_queries(!config_.query_output.empty()
                                            ? config_.query_output
                                            : (!config_.optics_output.empty()
                                                   ? config_.optics_output
                                                   : config_.hdr_output));
            auto data = renderer_->metrics();
            data["cpu_scene_prepare_ms"] = cpu_scene_ms_;
            data["total_app_wall_ms"] =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
                    .count();
            auto completed_config = current();
            data["config"] = schema(completed_config).template_json();
            if (!config_.metrics.empty()) {
                if (config_.metrics.has_parent_path())
                    std::filesystem::create_directories(config_.metrics.parent_path());
                std::ofstream out(config_.metrics);
                if (!out)
                    throw std::runtime_error("cannot write RT metrics");
                out << data.dump(2) << '\n';
            }
            std::cout << "raytracing: triangles=" << data["triangles"] << " AS=" << data["as_bytes"]
                      << " bytes, scene=" << data["scene_upload_and_build_ms"]
                      << " ms, pipeline=" << data["pipeline_setup_ms"] << " ms\n";
            renderer_.reset();
        }
    }
    void workshop_ui() {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        auto& state = *scene_.workshop;
        auto recipe = state.recipes.at(state.selected);
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({390, std::min(720.F, ImGui::GetIO().DisplaySize.y - 24)},
                                 ImGuiCond_FirstUseEver);
        ImGui::Begin("RT material workshop");
        ImGui::PushItemWidth(155);
        ImGui::TextUnformatted(state.catalog.at(state.selected).label.c_str());
        if (renderer_)
            ImGui::Text("Samples %u / %u  |  History %llu", renderer_->sample_count(),
                        config_.samples,
                        static_cast<unsigned long long>(renderer_->history_epoch()));
        bool changed = false;
        changed |= ImGui::ColorEdit3("Base color", &recipe.surface.base_roughness.x);
        changed |=
            ImGui::SliderFloat("Perceptual roughness", &recipe.surface.base_roughness.w, .05F, 1);
        changed |= ImGui::SliderFloat("Metallic", &recipe.optics.settings.x, 0, 1);
        changed |= ImGui::SliderFloat("Dielectric IOR", &recipe.optics.settings.z, 1, 2.5F);
        changed |= ImGui::ColorEdit3("Emission", &recipe.surface.emission.x,
                                     ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
        if (changed) {
            try {
                state.replace(state.selected, recipe);
                workshop_message_.clear();
            } catch (const std::exception& e) {
                workshop_message_ = e.what();
            }
        }
        ImGui::Checkbox("Patterns", &config_.pattern);
        ImGui::SameLine();
        ImGui::Checkbox("Relief normals", &config_.bump);
        const char* sources[] = {"environment", "scene"};
        int source = config_.reflection_source == "scene";
        if (ImGui::Combo("Optical source", &source, sources, 2)) {
            config_.reflection_source = sources[source];
            config_.transmission_source = sources[source];
        }
        ImGui::Checkbox("Shadows", &config_.shadows);
        const char* lighting_modes[] = {"combined", "sky", "sun", "furnace"};
        int lighting = 0;
        for (int i = 0; i < 4; ++i)
            if (config_.workshop_lighting == lighting_modes[i])
                lighting = i;
        if (ImGui::Combo("Lighting components", &lighting, lighting_modes, 4))
            config_.workshop_lighting = lighting_modes[lighting];
        float elevation = glm::degrees(config_.sun_elevation);
        float azimuth = render::atmosphere_environment_wrap_signed_degrees(
            shared_sun_azimuth_degrees(config_.sun_azimuth));
        if (ImGui::SliderFloat("Sun elevation", &elevation, -6, 60))
            config_.sun_elevation = glm::radians(elevation);
        if (ImGui::SliderFloat("Sun azimuth", &azimuth, -180, 180))
            config_.sun_azimuth = glm::pi<float>() - glm::radians(azimuth);
        ImGui::SliderFloat("Exposure EV", &config_.exposure, -4, 4);
        int samples = static_cast<int>(config_.samples);
        if (ImGui::SliderInt("Sample target", &samples, 1, 8192, "%d",
                             ImGuiSliderFlags_Logarithmic))
            config_.samples = static_cast<std::uint32_t>(samples);
        if (ImGui::Button("Reset material"))
            state.replace(state.selected, state.catalog.at(state.selected).recipe);
        ImGui::SameLine();
        if (ImGui::Button("Reset camera"))
            orbit_.reset();
        if (!config_.save_config.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Save view"))
                save();
        }
        ImGui::InputText("Recipe file", recipe_path_.data(), recipe_path_.size());
        auto operate = [&](auto operation) {
            try {
                operation();
                workshop_message_ = "Recipe operation complete";
            } catch (const std::exception& e) {
                workshop_message_ = e.what();
            }
        };
        if (ImGui::Button("Save recipe"))
            operate([&] {
                asset::save_material_recipe(recipe_path_.data(), state.recipes.at(state.selected));
            });
        ImGui::SameLine();
        if (ImGui::Button("Load recipe"))
            operate([&] {
                auto candidate = asset::load_material_recipe(recipe_path_.data());
                state.replace(state.selected, std::move(candidate));
            });
        if (ImGui::Button("Reload catalog"))
            operate([&] { state.reload(); });
        ImGui::Separator();
        if (ImGui::BeginCombo("Family", workshop_family_.c_str())) {
            if (ImGui::Selectable("all", workshop_family_ == "all"))
                workshop_family_ = "all";
            std::vector<std::string> families;
            for (const auto& item : state.catalog) {
                if (item.recipe.optics.settings.y > 0 ||
                    std::find(families.begin(), families.end(), item.family) != families.end())
                    continue;
                families.push_back(item.family);
                if (ImGui::Selectable(item.family.c_str(), workshop_family_ == item.family))
                    workshop_family_ = item.family;
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginCombo("Material", state.catalog.at(state.selected).label.c_str())) {
            for (std::size_t i = 0; i < state.catalog.size(); ++i) {
                const auto& item = state.catalog[i];
                if (item.recipe.optics.settings.y > 0 ||
                    (workshop_family_ != "all" && item.family != workshop_family_))
                    continue;
                if (ImGui::Selectable(item.label.c_str(), i == state.selected)) {
                    state.selected = static_cast<std::uint32_t>(i);
                    config_.material = item.id;
                }
            }
            ImGui::EndCombo();
        }
        const char* shapes[]{"sphere", "block", "swatch", "cut-block"};
        int shape = 0;
        for (int i = 0; i < 4; ++i)
            if (config_.specimen == shapes[i])
                shape = i;
        if (ImGui::Combo("Specimen", &shape, shapes, 4))
            config_.specimen = shapes[shape];
        recipe = state.recipes.at(state.selected);
        if (ImGui::CollapsingHeader("Recipe detail")) {
            bool detail = false;
            detail |= ImGui::SliderFloat("Diffuse roughness", &recipe.optics.attenuation.w, 0, 1);
            if (recipe.pattern.settings.x > 0) {
                detail |= ImGui::SliderFloat("Pattern scale", &recipe.pattern.settings.z, .25F, 64);
                detail |= ImGui::SliderFloat("Pattern strength", &recipe.pattern.settings.w, 0, 1);
                detail |= ImGui::ColorEdit3("Pattern secondary", &recipe.pattern.secondary.x);
                detail |= ImGui::SliderFloat("Pattern relief", &recipe.pattern.detail.x, 0, .005F,
                                             "%.5f");
            }
            auto& library = recipe.library;
            if (library.settings.x > 0) {
                detail |= ImGui::SliderFloat("Library scale", &library.settings.z, .1F, 16);
                detail |=
                    ImGui::SliderFloat("Relief height", &library.settings.w, 0, .005F, "%.5f");
                detail |= ImGui::ColorEdit3("Library secondary", &library.secondary.x);
                if (library.settings.x == 1) {
                    detail |=
                        ImGui::SliderFloat("Ring frequency", &library.wood.ring_frequency, 1, 100);
                    detail |=
                        ImGui::SliderFloat("Growth distortion", &library.wood.distortion, 0, 2);
                    detail |= ImGui::SliderFloat("Fiber frequency", &library.wood.grain_frequency,
                                                 20, 600);
                    detail |=
                        ImGui::SliderFloat("Pore strength", &library.wood.pore_strength, 0, 1);
                } else if (library.settings.x == 2) {
                    detail |= ImGui::SliderFloat("Rust coverage", &library.rust.coverage, 0, 1);
                    detail |=
                        ImGui::SliderFloat("Patch frequency", &library.rust.patch_frequency, 1, 64);
                    detail |= ImGui::SliderFloat("Edge breakup", &library.rust.edge_breakup, 0, 1);
                    detail |= ImGui::SliderFloat("Pitting", &library.rust.pit_strength, 0, 1);
                    detail |=
                        ImGui::SliderFloat("Steel roughness", &library.rust_roughness.x, .05F, 1);
                    detail |=
                        ImGui::SliderFloat("Oxide roughness", &library.rust_roughness.y, .05F, 1);
                } else if (library.settings.x == 3) {
                    detail |= ImGui::SliderFloat("Yarn spacing", &library.weave.yarn_spacing, .003F,
                                                 .06F, "%.4f");
                    detail |=
                        ImGui::SliderFloat("Yarn width", &library.weave.yarn_width, .3F, .95F);
                    detail |= ImGui::SliderFloat("Yarn irregularity", &library.weave.irregularity,
                                                 0, .5F);
                    detail |=
                        ImGui::SliderFloat("Fine fibers", &library.weave.fiber_strength, 0, 1);
                    detail |= ImGui::ColorEdit3("Sheen color", &library.sheen.x);
                    detail |= ImGui::SliderFloat("Sheen roughness", &library.sheen.w, .05F, 1);
                } else {
                    detail |= ImGui::SliderFloat("Fiber frequency", &library.paper.fiber_frequency,
                                                 40, 1000);
                    detail |= ImGui::SliderFloat("Fiber directionality",
                                                 &library.paper.directionality, 0, 1);
                    detail |=
                        ImGui::SliderFloat("Pulp flecks", &library.paper.fleck_strength, 0, 1);
                    detail |=
                        ImGui::SliderFloat("Fiber relief", &library.paper.fiber_strength, 0, 1);
                }
            }
            if (detail)
                operate([&] { state.replace(state.selected, recipe); });
        }
        if (!workshop_message_.empty())
            ImGui::TextWrapped("%s", workshop_message_.c_str());
        ImGui::PopItemWidth();
        ImGui::End();
    }
    void ui() {
        if (scene_.workshop) {
            workshop_ui();
            return;
        }
        const bool solo = config_.scene == "reflection-dragon-solo";
        const bool optical = config_.scene.find("reflection-") == 0 ||
                             config_.scene.find("refraction-") == 0 || config_.scene == "slab";
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({optical ? 400.F : 320.F, 0}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Ray tracing");
        if (optical)
            ImGui::PushItemWidth(210);
        ImGui::TextUnformatted("Hardware triangle tracing");
        ImGui::TextUnformatted("Drag to orbit, scroll to zoom, R to reset");
        const char* views[] = {"final",       "geometric-normal", "shading-normal", "depth",
                               "instance-id", "barycentric",      "secondary-id",   "path-class",
                               "path-events", "transport-limit",  "environment"};
        int view = 0;
        for (int i = 0; i < 11; ++i)
            if (config_.view == views[i])
                view = i;
        if (ImGui::Combo("View", &view, views, 11))
            config_.view = views[view];
        ImGui::Checkbox("Shadows", &config_.shadows);
        ImGui::SliderFloat("Sun azimuth", &config_.sun_azimuth, -3.14159F, 3.14159F);
        ImGui::SliderFloat("Sun elevation", &config_.sun_elevation,
                           config_.environment == "atmosphere" ? -.15F : .02F, 1.55F);
        ImGui::SliderFloat("Light intensity", &config_.intensity, 0, 10);
        float color[] = {config_.red, config_.green, config_.blue};
        if (ImGui::ColorEdit3(solo && config_.subject_material == "mirror" ? "Reflectance"
                                                                           : "Base color",
                              color)) {
            config_.red = color[0];
            config_.green = color[1];
            config_.blue = color[2];
        }
        ImGui::BeginDisabled(solo && config_.subject_material == "mirror");
        ImGui::SliderFloat("Roughness", &config_.roughness, .04F, 1);
        ImGui::EndDisabled();
        ImGui::BeginDisabled(config_.environment == "atmosphere");
        ImGui::SliderFloat("Ambient fill", &config_.ambient, 0, .5F);
        ImGui::EndDisabled();
        ImGui::SliderFloat("Exposure", &config_.exposure, -4, 4);
        if (ImGui::Button("Reset camera"))
            orbit_.reset();
        if (!config_.save_config.empty() && ImGui::Button("Save view"))
            save();
        if (solo) {
            const char* surfaces[] = {"clay", "mirror"};
            int surface = config_.subject_material == "mirror";
            if (ImGui::Combo("Dragon surface", &surface, surfaces, 2))
                config_.subject_material = surfaces[surface];
        }
        if (config_.scene.find("reflection-") == 0 || config_.scene.find("refraction-") == 0 ||
            config_.scene == "slab") {
            const char* sources[] = {"environment", "scene"};
            int reflection = config_.reflection_source == "scene";
            if (ImGui::Combo("Reflection source", &reflection, sources, 2))
                config_.reflection_source = sources[reflection];
            const bool glass = config_.scene.find("refraction-") == 0 || config_.scene == "slab";
            if (glass) {
                int transmission = config_.transmission_source == "scene";
                if (ImGui::Combo("Transmission source", &transmission, sources, 2))
                    config_.transmission_source = sources[transmission];
                ImGui::SliderFloat("Glass IOR", &config_.ior, 1, 2.5F);
                float attenuation[] = {config_.attenuation_red, config_.attenuation_green,
                                       config_.attenuation_blue};
                if (ImGui::ColorEdit3("Glass transmittance", attenuation)) {
                    config_.attenuation_red = std::max(attenuation[0], .001F);
                    config_.attenuation_green = std::max(attenuation[1], .001F);
                    config_.attenuation_blue = std::max(attenuation[2], .001F);
                }
                ImGui::TextWrapped("Sharp optics; straight glass shadows; no caustics");
            } else if (solo)
                ImGui::TextWrapped("Sharp reflective dragon; roughness applies to clay only");
            else
                ImGui::TextWrapped(
                    "Sharp scene reflections; rough materials use environment lighting");
        }
        if (optical)
            ImGui::PopItemWidth();
        ImGui::End();
    }
    void save() {
        auto c = current();
        c.save_config.clear();
        c.metrics.clear();
        c.query_output.clear();
        c.optics_output.clear();
        c.hdr_output.clear();
        c.scene_output.clear();
        c.orbit_speed = 0;
        c.sun_speed = 0;
        c.fault = "none";
        c.exercise_retry = false;
        schema(c).write_template(config_.save_config);
    }
    int run() {
        if (config_.common.headless) {
            host::HeadlessPngHostConfig h;
            h.run_config = config_.common;
            h.output_format = VK_FORMAT_R8G8B8A8_UNORM;
            h.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            h.require_ray_tracing = true;
            host::HeadlessPngHostCallbacks cb;
            cb.create_resources = [&](host::HeadlessPngContext& c) {
                create(c.device(), c.gpu(), host::headless_capture_frame_slot_count(config_.common),
                       c.render_target().extent, c.render_target().format);
            };
            cb.record_frame = [&](host::HeadlessPngContext& c, const host::HeadlessCaptureFrame& f,
                                  VkCommandBuffer command,
                                  const host::HeadlessRenderTarget& target) {
                presentation_seconds_ = double(f.index) / double(std::max(config_.common.fps, 1U));
                stress_edit(f.index);
                renderer_->render(command, target, f.frame_slot, current(), false,
                                  c.profile_recorder(), f.index);
            };
            cb.shutdown = [&](host::HeadlessPngContext&) {
                if (!config_.save_config.empty())
                    save();
                shutdown();
            };
            host::HeadlessPngHost host(std::move(h), std::move(cb));
            return host.run();
        }
        host::WindowedAppConfig h;
        h.run_config = config_.common;
        h.app_name = "raytracing";
        h.ready_status = "rendering hardware ray-traced triangles";
        h.required_queue_flags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        h.require_ray_tracing = true;
        h.close_on_escape = true;
        host::WindowedAppCallbacks cb;
        cb.create_swapchain_resources = [&](host::WindowedAppContext& c) {
            create(c.device(), c.gpu(), c.frame_slot_count(), c.swapchain().extent(),
                   c.swapchain().format());
        };
        cb.update = [&](host::WindowedAppContext& c, const FrameTiming& t) {
            presentation_seconds_ += t.delta_seconds;
            orbit_.update_from_input(c.filtered_input(), t.delta_seconds);
        };
        cb.draw_ui = [&](host::WindowedAppContext&) { ui(); };
        cb.record_frame = [&](host::WindowedAppContext& c, const host::WindowedRenderFrame& f) {
            stress_edit(frame_);
            renderer_->render(f.command_buffer, f.color_target, f.frame_slot, current(), true,
                              c.profile_recorder(), frame_++);
        };
        cb.shutdown = [&](host::WindowedAppContext&) { shutdown(); };
        return host::run_windowed_app(std::move(h), std::move(cb));
    }

  private:
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
    double cpu_scene_ms_ = 0;
    Config config_;
    Scene scene_;
    OrbitController orbit_;
    std::unique_ptr<Renderer> renderer_;
    std::uint64_t frame_ = 0;
    double presentation_seconds_ = 0;
    bool target_fault_used_ = false;
    std::array<char, 512> recipe_path_{};
    std::string workshop_family_ = "all";
    std::string workshop_message_;
};
} // namespace
int run(const Config& config) {
    App app(config);
    return app.run();
}
} // namespace cubey::projects::raytracing
