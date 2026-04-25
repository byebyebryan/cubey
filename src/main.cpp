#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <GLFW/glfw3.h>
#ifndef __EMSCRIPTEN__
#include <glfw3webgpu.h>
#endif
#include <webgpu/webgpu.h>

#include <cassert>
#include <cstdio>

// ── WGSL shaders ─────────────────────────────────────────────────────────────

static const char* k_vert_wgsl = R"(
@vertex
fn vs_main(@builtin(vertex_index) idx: u32) -> @builtin(position) vec4f {
    var pos = array<vec2f, 3>(
        vec2f( 0.0,  0.5),
        vec2f(-0.5, -0.5),
        vec2f( 0.5, -0.5),
    );
    return vec4f(pos[idx], 0.0, 1.0);
}
)";

static const char* k_frag_wgsl = R"(
@fragment
fn fs_main() -> @location(0) vec4f {
    return vec4f(0.4, 0.8, 1.0, 1.0);
}
)";

// ── State ────────────────────────────────────────────────────────────────────

struct State {
    GLFWwindow*        window   = nullptr;
    WGPUInstance       instance = nullptr;
    WGPUSurface        surface  = nullptr;
    WGPUAdapter        adapter  = nullptr;
    WGPUDevice         device   = nullptr;
    WGPUQueue          queue    = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    WGPUTextureFormat  fmt      = WGPUTextureFormat_Undefined;
};

static State g;

// ── Helpers ───────────────────────────────────────────────────────────────────

static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

static WGPUAdapter request_adapter(WGPUInstance instance, WGPUSurface surface) {
#ifdef __EMSCRIPTEN__
    // Pass nullptr so the browser uses its own defaults; Dawn-specific fields
    // in WGPURequestAdapterOptions (e.g. featureLevel) confuse Chrome.
    WGPURequestAdapterOptions const* opts_ptr = nullptr;
#else
    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterOptions const* opts_ptr = &opts;
#endif

    WGPUAdapter out = nullptr;
    WGPURequestAdapterCallbackInfo cb{};
    cb.mode     = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter a, WGPUStringView msg, void* ud1, void*) {
        if (status == WGPURequestAdapterStatus_Success)
            *static_cast<WGPUAdapter*>(ud1) = a;
        else
            fprintf(stderr, "requestAdapter: %.*s\n", (int)msg.length, msg.data);
    };
    cb.userdata1 = &out;
    WGPUFuture f = wgpuInstanceRequestAdapter(instance, opts_ptr, cb);
    WGPUFutureWaitInfo wi{f, false};
    wgpuInstanceWaitAny(instance, 1, &wi, UINT64_MAX);
    return out;
}

static WGPUDevice request_device(WGPUInstance instance, WGPUAdapter adapter) {
    WGPUDevice out = nullptr;

    WGPUUncapturedErrorCallbackInfo err{};
    err.callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void*, void*) {
        fprintf(stderr, "WebGPU error [%d]: %.*s\n", type, (int)msg.length, msg.data);
    };

    WGPUDeviceDescriptor desc{};
    desc.label                       = sv("cubey");
    desc.defaultQueue.label          = sv("queue");
    desc.uncapturedErrorCallbackInfo = err;

    WGPURequestDeviceCallbackInfo cb{};
    cb.mode     = WGPUCallbackMode_WaitAnyOnly;
    cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice d, WGPUStringView msg, void* ud1, void*) {
        if (status == WGPURequestDeviceStatus_Success)
            *static_cast<WGPUDevice*>(ud1) = d;
        else
            fprintf(stderr, "requestDevice: %.*s\n", (int)msg.length, msg.data);
    };
    cb.userdata1 = &out;
    WGPUFuture f = wgpuAdapterRequestDevice(adapter, &desc, cb);
    WGPUFutureWaitInfo wi{f, false};
    wgpuInstanceWaitAny(instance, 1, &wi, UINT64_MAX);
    return out;
}

static WGPUShaderModule create_shader(WGPUDevice device, const char* wgsl) {
    WGPUShaderSourceWGSL src{};
    src.chain.sType = WGPUSType_ShaderSourceWGSL;
    src.code        = sv(wgsl);

    WGPUShaderModuleDescriptor desc{};
    desc.nextInChain = &src.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// ── Frame ─────────────────────────────────────────────────────────────────────

static void frame() {
    glfwPollEvents();

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(g.surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

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
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g.device, &enc_desc);
    WGPURenderPassEncoder pass  = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);

    wgpuRenderPassEncoderSetPipeline(pass, g.pipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmd_desc{};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g.surface);
#endif
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(st.texture);
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main() {
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE,  GLFW_FALSE);
    g.window = glfwCreateWindow(1280, 720, "cubey", nullptr, nullptr);
    if (!g.window) { glfwTerminate(); return 1; }

    WGPUInstanceFeatureName instance_features[] = {WGPUInstanceFeatureName_TimedWaitAny};
    WGPUInstanceDescriptor idesc{};
    idesc.requiredFeatureCount = 1;
    idesc.requiredFeatures     = instance_features;
    g.instance = wgpuCreateInstance(&idesc);
    assert(g.instance);

#ifdef __EMSCRIPTEN__
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
    canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvas.selector    = sv("#canvas");
    WGPUSurfaceDescriptor sdesc{};
    sdesc.nextInChain  = &canvas.chain;
    g.surface = wgpuInstanceCreateSurface(g.instance, &sdesc);
#else
    g.surface = glfwCreateWindowWGPUSurface(g.instance, g.window);
#endif
    assert(g.surface);

    g.adapter = request_adapter(g.instance, g.surface);
    assert(g.adapter);

    g.device = request_device(g.instance, g.adapter);
    assert(g.device);

    g.queue = wgpuDeviceGetQueue(g.device);

    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(g.surface, g.adapter, &caps);
    g.fmt = caps.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    int w, h;
    glfwGetFramebufferSize(g.window, &w, &h);

    WGPUSurfaceConfiguration cfg{};
    cfg.device      = g.device;
    cfg.format      = g.fmt;
    cfg.usage       = WGPUTextureUsage_RenderAttachment;
    cfg.width       = (uint32_t)w;
    cfg.height      = (uint32_t)h;
    cfg.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(g.surface, &cfg);

    WGPUShaderModule vert = create_shader(g.device, k_vert_wgsl);
    WGPUShaderModule frag = create_shader(g.device, k_frag_wgsl);

    WGPUColorTargetState color_target{};
    color_target.format    = g.fmt;
    color_target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState frag_state{};
    frag_state.module      = frag;
    frag_state.entryPoint  = sv("fs_main");
    frag_state.targetCount = 1;
    frag_state.targets     = &color_target;

    WGPURenderPipelineDescriptor pipeline_desc{};
    pipeline_desc.vertex.module      = vert;
    pipeline_desc.vertex.entryPoint  = sv("vs_main");
    pipeline_desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeline_desc.multisample.count  = 1;
    pipeline_desc.multisample.mask   = 0xFFFFFFFF;
    pipeline_desc.fragment           = &frag_state;

    g.pipeline = wgpuDeviceCreateRenderPipeline(g.device, &pipeline_desc);
    assert(g.pipeline);

    wgpuShaderModuleRelease(vert);
    wgpuShaderModuleRelease(frag);

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, false);
#else
    while (!glfwWindowShouldClose(g.window)) frame();

    wgpuRenderPipelineRelease(g.pipeline);
    wgpuSurfaceUnconfigure(g.surface);
    wgpuQueueRelease(g.queue);
    wgpuDeviceRelease(g.device);
    wgpuAdapterRelease(g.adapter);
    wgpuSurfaceRelease(g.surface);
    wgpuInstanceRelease(g.instance);
    glfwDestroyWindow(g.window);
    glfwTerminate();
#endif
    return 0;
}
