#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <webgpu/webgpu.h>

#include <cassert>
#include <cstdio>

static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

static WGPUAdapter request_adapter(WGPUInstance instance, WGPUSurface surface) {
    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;

    WGPUAdapter out = nullptr;
    WGPURequestAdapterCallbackInfo cb{};
    cb.mode     = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView msg, void* ud1, void*) {
        if (status == WGPURequestAdapterStatus_Success)
            *static_cast<WGPUAdapter*>(ud1) = a;
        else
            fprintf(stderr, "requestAdapter: %.*s\n", (int)msg.length, msg.data);
    };
    cb.userdata1 = &out;
    wgpuInstanceRequestAdapter(instance, &opts, cb);
    return out;
}

static WGPUDevice request_device(WGPUAdapter adapter) {
    WGPUDevice out = nullptr;

    WGPUUncapturedErrorCallbackInfo err{};
    err.callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
        fprintf(stderr, "WebGPU error [%d]: %.*s\n", type, (int)msg.length, msg.data);
    };

    WGPUDeviceDescriptor desc{};
    desc.label                      = sv("cubey");
    desc.defaultQueue.label         = sv("queue");
    desc.uncapturedErrorCallbackInfo = err;

    WGPURequestDeviceCallbackInfo cb{};
    cb.mode     = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView msg, void* ud1, void*) {
        if (status == WGPURequestDeviceStatus_Success)
            *static_cast<WGPUDevice*>(ud1) = d;
        else
            fprintf(stderr, "requestDevice: %.*s\n", (int)msg.length, msg.data);
    };
    cb.userdata1 = &out;
    wgpuAdapterRequestDevice(adapter, &desc, cb);
    return out;
}

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(1280, 720, "cubey", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }

    WGPUInstanceDescriptor idesc{};
    WGPUInstance instance = wgpuCreateInstance(&idesc);
    assert(instance);

    WGPUSurface surface = glfwCreateWindowWGPUSurface(instance, window);
    assert(surface);

    WGPUAdapter adapter = request_adapter(instance, surface);
    assert(adapter);

    WGPUDevice device = request_device(adapter);
    assert(device);

    WGPUQueue queue = wgpuDeviceGetQueue(device);

    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface, adapter, &caps);
    WGPUTextureFormat fmt = caps.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    WGPUSurfaceConfiguration cfg{};
    cfg.device      = device;
    cfg.format      = fmt;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = 1280;
    cfg.height      = 720;
    cfg.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface, &cfg);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        WGPUSurfaceTexture st{};
        wgpuSurfaceGetCurrentTexture(surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) continue;

        WGPUTextureView view = wgpuTextureCreateView(st.texture, nullptr);

        WGPURenderPassColorAttachment color{};
        color.view       = view;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp     = WGPULoadOp_Clear;
        color.storeOp    = WGPUStoreOp_Store;
        color.clearValue = {0.08, 0.06, 0.12, 1.0};

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments     = &color;

        WGPUCommandEncoderDescriptor enc_desc{};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &enc_desc);
        WGPURenderPassEncoder pass  = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        WGPUCommandBufferDescriptor cmd_desc{};
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
        wgpuCommandEncoderRelease(encoder);

        wgpuQueueSubmit(queue, 1, &cmd);
        wgpuCommandBufferRelease(cmd);

        wgpuSurfacePresent(surface);
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);
    }

    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuSurfaceRelease(surface);
    wgpuInstanceRelease(instance);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
