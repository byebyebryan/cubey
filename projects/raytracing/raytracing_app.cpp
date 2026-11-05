#include "raytracing_config.h"
#include "raytracing_renderer.h"
#include <chrono>
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
        orbit_.set_home_distance(config_.distance);
        orbit_.set_distance(config_.distance);
    }
    Config current() const {
        auto c = config_;
        c.yaw += orbit_.yaw();
        c.pitch += orbit_.pitch();
        c.distance = orbit_.distance();
        return c;
    }
    void create(vulkan::Device& device, vulkan::GpuRuntime& gpu, std::uint32_t slots,
                VkExtent2D extent, VkFormat format) {
        if (!renderer_) {
            if (config_.exercise_retry && config_.fault != "target") {
                bool failed = false;
                try {
                    Renderer rejected(device, gpu, scene_, slots, config_.fault);
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
                !config_.query_output.empty(), !config_.metrics.empty());
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
            if (!config_.query_output.empty())
                renderer_->save_queries(config_.query_output);
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
    void ui() {
        ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({320, 0}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Ray tracing");
        ImGui::TextUnformatted("Hardware triangle tracing");
        ImGui::TextUnformatted("Drag to orbit, scroll to zoom, R to reset");
        const char* views[] = {"final", "geometric-normal", "shading-normal",
                               "depth", "instance-id",      "barycentric"};
        int view = 0;
        for (int i = 0; i < 6; ++i)
            if (config_.view == views[i])
                view = i;
        if (ImGui::Combo("View", &view, views, 6))
            config_.view = views[view];
        ImGui::Checkbox("Shadows", &config_.shadows);
        ImGui::SliderFloat("Sun azimuth", &config_.sun_azimuth, -3.14159F, 3.14159F);
        ImGui::SliderFloat("Sun elevation", &config_.sun_elevation, .02F, 1.55F);
        ImGui::SliderFloat("Light intensity", &config_.intensity, 0, 10);
        float color[] = {config_.red, config_.green, config_.blue};
        if (ImGui::ColorEdit3("Base color", color)) {
            config_.red = color[0];
            config_.green = color[1];
            config_.blue = color[2];
        }
        ImGui::SliderFloat("Roughness", &config_.roughness, .04F, 1);
        ImGui::SliderFloat("Ambient fill", &config_.ambient, 0, .5F);
        ImGui::SliderFloat("Exposure", &config_.exposure, -4, 4);
        if (ImGui::Button("Reset camera"))
            orbit_.reset();
        if (!config_.save_config.empty() && ImGui::Button("Save view"))
            save();
        ImGui::End();
    }
    void save() {
        auto c = current();
        c.save_config.clear();
        c.metrics.clear();
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
            orbit_.update_from_input(c.filtered_input(), t.delta_seconds);
        };
        cb.draw_ui = [&](host::WindowedAppContext&) { ui(); };
        cb.record_frame = [&](host::WindowedAppContext& c, const host::WindowedRenderFrame& f) {
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
    bool target_fault_used_ = false;
};
} // namespace
int run(const Config& config) {
    App app(config);
    return app.run();
}
} // namespace cubey::projects::raytracing
