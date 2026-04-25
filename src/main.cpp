#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <GLFW/glfw3.h>
#ifndef __EMSCRIPTEN__
#include <glfw3webgpu.h>
#endif
#include <webgpu/webgpu.h>

#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>

// ── Geometry ──────────────────────────────────────────────────────────────────

static const float k_verts[] = {
    -0.5f, -0.5f, -0.5f,
     0.5f, -0.5f, -0.5f,
     0.5f,  0.5f, -0.5f,
    -0.5f,  0.5f, -0.5f,
    -0.5f, -0.5f,  0.5f,
     0.5f, -0.5f,  0.5f,
     0.5f,  0.5f,  0.5f,
    -0.5f,  0.5f,  0.5f,
};

static const uint16_t k_indices[] = {
    4, 5, 6,  4, 6, 7,  // front
    1, 0, 3,  1, 3, 2,  // back
    0, 4, 7,  0, 7, 3,  // left
    5, 1, 2,  5, 2, 6,  // right
    0, 1, 5,  0, 5, 4,  // bottom
    7, 6, 2,  7, 2, 3,  // top
};

// ── Uniforms ──────────────────────────────────────────────────────────────────
// Must match WGSL struct layout: mat4x4f (64) + f32 (4) + pad to 80 (align 16)

struct Uniforms {
    glm::mat4 mvp;
    float     time;
    float     _pad[3];
};

// ── WGSL shaders ─────────────────────────────────────────────────────────────

static const char* k_compute_wgsl = R"(
struct Uniforms { mvp: mat4x4f, time: f32 }
@group(0) @binding(0) var<uniform>            u:   Uniforms;
@group(0) @binding(1) var<storage, read>      src: array<f32>;
@group(0) @binding(2) var<storage, read_write> dst: array<f32>;

@compute @workgroup_size(8)
fn cs_main(@builtin(global_invocation_id) id: vec3u) {
    let i = id.x;
    if (i >= 8u) { return; }
    let b   = i * 3u;
    let pos = vec3f(src[b], src[b + 1u], src[b + 2u]);
    let wave = 0.15 * sin(u.time * 2.0 + f32(i) * 0.8);
    let p = pos + normalize(pos) * wave;
    dst[b]      = p.x;
    dst[b + 1u] = p.y;
    dst[b + 2u] = p.z;
}
)";

static const char* k_vert_wgsl = R"(
struct Uniforms { mvp: mat4x4f, time: f32 }
@group(0) @binding(0) var<uniform> u: Uniforms;

struct Out {
    @builtin(position) pos: vec4f,
    @location(0)       col: vec3f,
}

@vertex
fn vs_main(@location(0) in_pos: vec3f) -> Out {
    var o: Out;
    o.pos = u.mvp * vec4f(in_pos, 1.0);
    o.col = in_pos + 0.5;
    return o;
}
)";

static const char* k_frag_wgsl = R"(
@fragment
fn fs_main(@location(0) col: vec3f) -> @location(0) vec4f {
    return vec4f(col, 1.0);
}
)";

// ── State ────────────────────────────────────────────────────────────────────

struct State {
    GLFWwindow*         window       = nullptr;
    WGPUInstance        instance     = nullptr;
    WGPUSurface         surface      = nullptr;
    WGPUAdapter         adapter      = nullptr;
    WGPUDevice          device       = nullptr;
    WGPUQueue           queue        = nullptr;
    WGPUTextureFormat   fmt          = WGPUTextureFormat_Undefined;
    // geometry
    WGPUBuffer          vbuf_src     = nullptr;  // rest-pose vertices (storage, read)
    WGPUBuffer          vbuf         = nullptr;  // displaced vertices (storage | vertex)
    WGPUBuffer          ibuf         = nullptr;
    WGPUBuffer          ubuf         = nullptr;
    // compute
    WGPUComputePipeline compute_pipe = nullptr;
    WGPUBindGroup       compute_bg   = nullptr;
    // render
    WGPURenderPipeline  render_pipe  = nullptr;
    WGPUBindGroup       render_bg    = nullptr;
    WGPUTexture         depth_tex    = nullptr;
    WGPUTextureView     depth_view   = nullptr;
    float               t            = 0.0f;
};

static State g;

// ── Helpers ───────────────────────────────────────────────────────────────────

static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

static WGPUBuffer upload_buffer(WGPUBufferUsage usage, const void* data, size_t size) {
    WGPUBufferDescriptor d{};
    d.size             = size;
    d.usage            = usage;
    d.mappedAtCreation = true;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(g.device, &d);
    memcpy(wgpuBufferGetMappedRange(buf, 0, size), data, size);
    wgpuBufferUnmap(buf);
    return buf;
}

static WGPUAdapter request_adapter(WGPUInstance instance, WGPUSurface surface) {
#ifdef __EMSCRIPTEN__
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

    static double prev = glfwGetTime();
    double now = glfwGetTime();
    g.t += (float)(now - prev);
    prev = now;

    // Upload uniforms (MVP + time)
    Uniforms u{};
    glm::mat4 model = glm::rotate(glm::mat4(1.0f), g.t, glm::vec3(0.5f, 1.0f, 0.0f));
    glm::mat4 view  = glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f),
                                   glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 proj  = glm::perspective(glm::radians(45.0f), 1280.0f / 720.0f, 0.1f, 100.0f);
    proj[1][1] *= -1.0f;
    u.mvp  = proj * view * model;
    u.time = g.t;
    wgpuQueueWriteBuffer(g.queue, g.ubuf, 0, &u, sizeof(u));

    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(g.surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) return;

    WGPUTextureView view_tex = wgpuTextureCreateView(st.texture, nullptr);

    WGPUCommandEncoderDescriptor enc_desc{};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g.device, &enc_desc);

    // ── Compute pass: deform vertices ─────────────────────────────────────────
    {
        WGPUComputePassDescriptor cp_desc{};
        WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, &cp_desc);
        wgpuComputePassEncoderSetPipeline(cp, g.compute_pipe);
        wgpuComputePassEncoderSetBindGroup(cp, 0, g.compute_bg, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
        wgpuComputePassEncoderEnd(cp);
        wgpuComputePassEncoderRelease(cp);
    }

    // ── Render pass: draw cube using displaced vbuf ───────────────────────────
    {
        WGPURenderPassColorAttachment color{};
        color.view       = view_tex;
        color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        color.loadOp     = WGPULoadOp_Clear;
        color.storeOp    = WGPUStoreOp_Store;
        color.clearValue = {0.08, 0.06, 0.12, 1.0};

        WGPURenderPassDepthStencilAttachment depth_att{};
        depth_att.view            = g.depth_view;
        depth_att.depthLoadOp     = WGPULoadOp_Clear;
        depth_att.depthStoreOp    = WGPUStoreOp_Store;
        depth_att.depthClearValue = 1.0f;

        WGPURenderPassDescriptor pass_desc{};
        pass_desc.colorAttachmentCount   = 1;
        pass_desc.colorAttachments       = &color;
        pass_desc.depthStencilAttachment = &depth_att;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderSetPipeline(pass, g.render_pipe);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, g.render_bg, 0, nullptr);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, g.vbuf, 0, sizeof(k_verts));
        wgpuRenderPassEncoderSetIndexBuffer(pass, g.ibuf, WGPUIndexFormat_Uint16, 0, sizeof(k_indices));
        wgpuRenderPassEncoderDrawIndexed(pass, 36, 1, 0, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    WGPUCommandBufferDescriptor cmd_desc{};
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);
    wgpuCommandEncoderRelease(encoder);

    wgpuQueueSubmit(g.queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);

#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g.surface);
#endif
    wgpuTextureViewRelease(view_tex);
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

    // ── Buffers ───────────────────────────────────────────────────────────────
    // vbuf_src: rest-pose vertices, read-only in compute
    g.vbuf_src = upload_buffer(WGPUBufferUsage_Storage,
                               k_verts, sizeof(k_verts));
    // vbuf: displaced vertices, written by compute, read as vertex buffer in render
    g.vbuf     = upload_buffer(WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex,
                               k_verts, sizeof(k_verts));
    g.ibuf     = upload_buffer(WGPUBufferUsage_Index, k_indices, sizeof(k_indices));

    {
        WGPUBufferDescriptor d{};
        d.size  = sizeof(Uniforms);
        d.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        g.ubuf  = wgpuDeviceCreateBuffer(g.device, &d);
    }

    // ── Depth texture ─────────────────────────────────────────────────────────
    {
        WGPUTextureDescriptor d{};
        d.format        = WGPUTextureFormat_Depth24Plus;
        d.usage         = WGPUTextureUsage_RenderAttachment;
        d.dimension     = WGPUTextureDimension_2D;
        d.size          = {(uint32_t)w, (uint32_t)h, 1};
        d.mipLevelCount = 1;
        d.sampleCount   = 1;
        g.depth_tex  = wgpuDeviceCreateTexture(g.device, &d);
        g.depth_view = wgpuTextureCreateView(g.depth_tex, nullptr);
    }

    // ── Compute pipeline ──────────────────────────────────────────────────────
    {
        WGPUBindGroupLayoutEntry entries[3]{};
        // binding 0: uniform (mvp + time)
        entries[0].binding            = 0;
        entries[0].visibility         = WGPUShaderStage_Compute;
        entries[0].buffer.type        = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(Uniforms);
        // binding 1: src vertices (read-only storage)
        entries[1].binding            = 1;
        entries[1].visibility         = WGPUShaderStage_Compute;
        entries[1].buffer.type        = WGPUBufferBindingType_ReadOnlyStorage;
        // binding 2: dst vertices (read-write storage)
        entries[2].binding            = 2;
        entries[2].visibility         = WGPUShaderStage_Compute;
        entries[2].buffer.type        = WGPUBufferBindingType_Storage;

        WGPUBindGroupLayoutDescriptor bgld{};
        bgld.entryCount = 3;
        bgld.entries    = entries;
        WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(g.device, &bgld);

        WGPUBindGroupEntry bge[3]{};
        bge[0].binding = 0; bge[0].buffer = g.ubuf;     bge[0].size = sizeof(Uniforms);
        bge[1].binding = 1; bge[1].buffer = g.vbuf_src; bge[1].size = sizeof(k_verts);
        bge[2].binding = 2; bge[2].buffer = g.vbuf;     bge[2].size = sizeof(k_verts);

        WGPUBindGroupDescriptor bgd{};
        bgd.layout     = bgl;
        bgd.entryCount = 3;
        bgd.entries    = bge;
        g.compute_bg = wgpuDeviceCreateBindGroup(g.device, &bgd);

        WGPUPipelineLayoutDescriptor pld{};
        pld.bindGroupLayoutCount = 1;
        pld.bindGroupLayouts     = &bgl;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(g.device, &pld);

        WGPUShaderModule cs = create_shader(g.device, k_compute_wgsl);

        WGPUComputePipelineDescriptor cpd{};
        cpd.layout          = pl;
        cpd.compute.module  = cs;
        cpd.compute.entryPoint = sv("cs_main");
        g.compute_pipe = wgpuDeviceCreateComputePipeline(g.device, &cpd);
        assert(g.compute_pipe);

        wgpuShaderModuleRelease(cs);
        wgpuPipelineLayoutRelease(pl);
        wgpuBindGroupLayoutRelease(bgl);
    }

    // ── Render pipeline ───────────────────────────────────────────────────────
    {
        WGPUBindGroupLayoutEntry entry{};
        entry.binding               = 0;
        entry.visibility            = WGPUShaderStage_Vertex;
        entry.buffer.type           = WGPUBufferBindingType_Uniform;
        entry.buffer.minBindingSize = sizeof(Uniforms);

        WGPUBindGroupLayoutDescriptor bgld{};
        bgld.entryCount = 1;
        bgld.entries    = &entry;
        WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(g.device, &bgld);

        WGPUBindGroupEntry bge{};
        bge.binding = 0; bge.buffer = g.ubuf; bge.size = sizeof(Uniforms);

        WGPUBindGroupDescriptor bgd{};
        bgd.layout = bgl; bgd.entryCount = 1; bgd.entries = &bge;
        g.render_bg = wgpuDeviceCreateBindGroup(g.device, &bgd);

        WGPUPipelineLayoutDescriptor pld{};
        pld.bindGroupLayoutCount = 1;
        pld.bindGroupLayouts     = &bgl;
        WGPUPipelineLayout pl = wgpuDeviceCreatePipelineLayout(g.device, &pld);

        WGPUShaderModule vert = create_shader(g.device, k_vert_wgsl);
        WGPUShaderModule frag = create_shader(g.device, k_frag_wgsl);

        WGPUVertexAttribute va{};
        va.format = WGPUVertexFormat_Float32x3; va.offset = 0; va.shaderLocation = 0;

        WGPUVertexBufferLayout vbl{};
        vbl.arrayStride = 3 * sizeof(float);
        vbl.stepMode    = WGPUVertexStepMode_Vertex;
        vbl.attributeCount = 1; vbl.attributes = &va;

        WGPUColorTargetState ct{};
        ct.format = g.fmt; ct.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fs{};
        fs.module = frag; fs.entryPoint = sv("fs_main");
        fs.targetCount = 1; fs.targets = &ct;

        WGPUDepthStencilState dss{};
        dss.format            = WGPUTextureFormat_Depth24Plus;
        dss.depthWriteEnabled = WGPUOptionalBool_True;
        dss.depthCompare      = WGPUCompareFunction_Less;

        WGPURenderPipelineDescriptor rpd{};
        rpd.layout                  = pl;
        rpd.vertex.module           = vert;
        rpd.vertex.entryPoint       = sv("vs_main");
        rpd.vertex.bufferCount      = 1;
        rpd.vertex.buffers          = &vbl;
        rpd.primitive.topology      = WGPUPrimitiveTopology_TriangleList;
        rpd.primitive.cullMode      = WGPUCullMode_None;
        rpd.depthStencil            = &dss;
        rpd.multisample.count       = 1;
        rpd.multisample.mask        = 0xFFFFFFFF;
        rpd.fragment                = &fs;

        g.render_pipe = wgpuDeviceCreateRenderPipeline(g.device, &rpd);
        assert(g.render_pipe);

        wgpuShaderModuleRelease(vert);
        wgpuShaderModuleRelease(frag);
        wgpuPipelineLayoutRelease(pl);
        wgpuBindGroupLayoutRelease(bgl);
    }

#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(frame, 0, false);
#else
    while (!glfwWindowShouldClose(g.window)) frame();

    wgpuTextureViewRelease(g.depth_view);
    wgpuTextureRelease(g.depth_tex);
    wgpuBindGroupRelease(g.render_bg);
    wgpuBindGroupRelease(g.compute_bg);
    wgpuRenderPipelineRelease(g.render_pipe);
    wgpuComputePipelineRelease(g.compute_pipe);
    wgpuBufferRelease(g.ubuf);
    wgpuBufferRelease(g.ibuf);
    wgpuBufferRelease(g.vbuf);
    wgpuBufferRelease(g.vbuf_src);
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
