#include <cubey/host/headless_png_host.h>
#include <cubey/host/windowed_host.h>
#include <cubey/vulkan/command_pool.h>
#include <cubey/vulkan/command_recorder.h>
#include <cubey/vulkan/image_transitions.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace cubey;
enum class Failure {
    None,
    Create,
    Update,
    Record,
    Destroy,
    Shutdown
};
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void admit(vulkan::GpuRuntime& gpu, std::atomic<bool>& retired) {
    retired = false;
    static_cast<void>(gpu.enqueue(
        {.label = "admitted lifecycle work", .work = [&](auto& owner) {
             // A command buffer with no resource accesses gives this work a
             // real retirement ticket without exposing a resource to a broken
             // teardown. Its pool remains alive until owner-side retirement.
             auto pool =
                 std::make_shared<vulkan::CommandPool>(owner.device(), vulkan::CommandPoolConfig{});
             const auto commands = pool->allocate_primary();
             vulkan::begin_command_buffer(commands, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
             vulkan::end_command_buffer(commands, "host lifecycle admission");
             const auto ticket = owner.submission().submit({.command_buffers = {commands}},
                                                           "host lifecycle submission");
             owner.defer_destruction_after(ticket, [pool = std::move(pool), &retired]() mutable {
                 pool.reset();
                 retired = true;
             });
         }}));
}
void fail() {
    throw std::runtime_error("intentional host lifecycle failure");
}
void clear(VkCommandBuffer commands, render::ColorTargetView target) {
    render::RenderClearValues values;
    values.color.color = {{0.2F, 0.3F, 0.4F, 1.0F}};
    const render::RenderTargetRenderingInfo info(render::render_target_view(target), values);
    vkCmdBeginRendering(commands, &info.info());
    vkCmdEndRendering(commands);
}

void headless(vulkan::GpuRuntimeExecutionMode mode, Failure failure) {
    std::atomic<bool> retired{true}, shutdown_retired{true};
    bool cleanup_safe = false, observed = false;
    unsigned shutdowns = 0;
    host::CommonRunConfig config;
    config.headless = config.headless_render_only = true;
    config.require_validation = true;
    config.width = 32;
    config.height = 24;
    config.frames = 2;
    host::HeadlessPngHostCallbacks callbacks;
    callbacks.create_resources = [&](auto& context) {
        if (failure == Failure::Create) {
            admit(context.gpu(), retired);
            fail();
        }
    };
    callbacks.before_frame = [&](auto& context, const auto&) {
        if (failure == Failure::Update) {
            admit(context.gpu(), retired);
            fail();
        }
    };
    callbacks.record_frame = [&](auto& context, const auto&, VkCommandBuffer commands,
                                 const auto& target) {
        if (failure == Failure::Record) {
            admit(context.gpu(), retired);
            fail();
        }
        clear(commands,
              render::color_target_view(target.extent, target.format, target.image, target.view));
    };
    callbacks.shutdown = [&](auto& context) {
        ++shutdowns;
        cleanup_safe = retired;
        if (failure == Failure::Shutdown) {
            admit(context.gpu(), shutdown_retired);
            fail();
        }
    };
    {
        host::HeadlessPngHost host({.run_config = config, .gpu_execution_mode = mode}, callbacks);
        try {
            static_cast<void>(host.run());
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()) == "intentional host lifecycle failure",
                    "headless cleanup changed the original failure");
            observed = true;
        }
        require(cleanup_safe && retired && shutdown_retired,
                "headless cleanup preceded admitted work retirement");
    }
    require(shutdowns == 1U, "headless shutdown must run exactly once");
    require(observed == (failure != Failure::None), "headless failure was lost");
}

void windowed(vulkan::GpuRuntimeExecutionMode mode, Failure failure, bool resize) {
    std::atomic<bool> retired{true}, shutdown_retired{true};
    bool cleanup_safe = true, observed = false;
    unsigned creates = 0, destroys = 0, shutdowns = 0, updates = 0;
    host::WindowedHostConfig config;
    config.gpu_execution_mode = mode;
    config.run_config.require_validation = true;
    config.run_config.width = 64;
    config.run_config.height = 48;
    config.run_config.frames = resize ? 100U : 2U;
    host::WindowedHostCallbacks callbacks;
    callbacks.create_swapchain_resources = [&](auto& context) {
        ++creates;
        if (failure == Failure::Create) {
            admit(context.gpu(), retired);
            fail();
        }
    };
    callbacks.update = [&](auto& context, const auto&) {
        ++updates;
        if (failure == Failure::Update) {
            admit(context.gpu(), retired);
            fail();
        }
        if (resize) {
            admit(context.gpu(), retired);
            if (creates > 1U)
                context.window().request_close();
            else if (updates == 1U)
                glfwSetWindowSize(context.window().native_handle(), 96, 72);
        }
    };
    callbacks.record_frame = [&](auto& context, const host::WindowedRenderFrame& frame) {
        if (failure == Failure::Record) {
            admit(context.gpu(), retired);
            fail();
        }
        const vulkan::CommandRecorder recorder(frame.command_buffer);
        recorder.begin(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);
        recorder.transition_image_layout(
            vulkan::begin_color_attachment_transition(frame.color_target.image));
        clear(frame.command_buffer, frame.color_target);
        recorder.transition_image_layout(
            vulkan::finish_color_attachment_for_present_transition(frame.color_target.image));
        recorder.end("host lifecycle test");
    };
    callbacks.destroy_swapchain_resources = [&](auto& context) {
        ++destroys;
        cleanup_safe = cleanup_safe && retired;
        if (failure == Failure::Destroy) {
            admit(context.gpu(), retired);
            fail();
        }
    };
    callbacks.shutdown = [&](auto& context) {
        ++shutdowns;
        cleanup_safe = cleanup_safe && retired;
        if (failure == Failure::Shutdown) {
            admit(context.gpu(), shutdown_retired);
            fail();
        }
    };
    {
        host::WindowedHost host(config, callbacks);
        try {
            static_cast<void>(host.run());
        } catch (const std::runtime_error& error) {
            require(std::string(error.what()) == "intentional host lifecycle failure",
                    "windowed cleanup changed the original failure");
            observed = true;
        }
    }
    require(cleanup_safe && retired && shutdown_retired,
            "windowed cleanup preceded admitted work retirement");
    require(shutdowns == 1U && destroys == creates,
            "windowed resource cleanup must run exactly once per creation");
    require(observed == (failure != Failure::None), "windowed failure was lost");
    require(!resize || creates >= 2U, "windowed fixture did not actually recreate its target");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const bool windows = argc == 2 && std::string(argv[1]) == "--windowed";
        if (windows && (std::getenv("CUBEY_ALLOW_WINDOWED_TESTS") == nullptr ||
                        std::string(std::getenv("CUBEY_ALLOW_WINDOWED_TESTS")) != "1"))
            return 77;
        for (const auto mode :
             {vulkan::GpuRuntimeExecutionMode::Inline, vulkan::GpuRuntimeExecutionMode::Threaded}) {
            for (const auto failure : {Failure::None, Failure::Create, Failure::Update,
                                       Failure::Record, Failure::Shutdown}) {
                if (windows)
                    windowed(mode, failure, false);
                else
                    headless(mode, failure);
            }
            if (windows) {
                windowed(mode, Failure::Destroy, false);
                windowed(mode, Failure::None, true);
            }
        }
        std::cout << "host lifecycle PASS: inline/threaded admission, failure, retirement"
                  << (windows ? ", resize/close\n" : "\n");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
