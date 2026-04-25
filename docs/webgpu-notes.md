# WebGPU / Dawn — Implementation Notes

Gotchas and learnings from bootstrapping the Dawn + Emscripten hello triangle on the `webgpu` branch.

---

## Build setup

### Dawn requires Clang on Linux

GCC 15 rejects Dawn due to stricter `operator==` requirements in its standard library headers. Build with:

```bash
cmake ... -DCMAKE_CXX_COMPILER=clang++
```

### FetchContent + Dawn submodules

Dawn normally pulls dependencies via `depot_tools`/`gclient`. With CMake FetchContent, set `DAWN_FETCH_DEPENDENCIES=ON` and suppress git submodule checkout — they conflict with `GIT_SHALLOW`:

```cmake
FetchContent_Declare(dawn
    GIT_REPOSITORY https://github.com/google/dawn
    GIT_TAG        main
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""   # DAWN_FETCH_DEPENDENCIES handles deps via CMake
)
```

### glfw3webgpu needs explicit platform defines

glfw3webgpu doesn't auto-detect Wayland/X11 at compile time. Drive it from what GLFW actually built with:

```cmake
if (GLFW_BUILD_WAYLAND)
    target_compile_definitions(glfw3webgpu PRIVATE GLFW_EXPOSE_NATIVE_WAYLAND)
endif()
if (GLFW_BUILD_X11)
    target_compile_definitions(glfw3webgpu PRIVATE GLFW_EXPOSE_NATIVE_X11)
endif()
```

Without this, surface creation silently returns null on Wayland.

### Dawn Wayland support is off by default

Must explicitly enable:

```cmake
set(DAWN_USE_WAYLAND ON CACHE BOOL "" FORCE)
set(DAWN_USE_X11     ON CACHE BOOL "" FORCE)
```

---

## Emscripten (web build)

### emdawnwebgpu port flag goes on both compile and link

The port provides both C++ headers and a JS glue library. Without the compile flag, the headers aren't found:

```cmake
target_compile_options(cubey PRIVATE --use-port=emdawnwebgpu)
target_link_options(cubey PRIVATE --use-port=emdawnwebgpu ...)
```

### Canvas surface struct name

The Emscripten-specific surface source type is:

```cpp
WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas{};
canvas.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
canvas.selector    = sv("#canvas");  // CSS selector, '#' prefix required
```

The generated HTML gives the canvas `id="canvas"`, so `#canvas` is correct.

### wgpuSurfacePresent is not supported on web

The browser owns presentation. Skip it on Emscripten:

```cpp
#ifndef __EMSCRIPTEN__
    wgpuSurfacePresent(g.surface);
#endif
```

Calling it aborts with: `wgpuSurfacePresent is unsupported (use requestAnimationFrame via html5.h instead)`.

---

## Async adapter/device request

### WaitAnyOnly + wgpuInstanceWaitAny is the correct pattern

`WGPUCallbackMode_AllowSpontaneous` fires synchronously on native but does not suspend in the browser. The right approach for both platforms:

```cpp
WGPURequestAdapterCallbackInfo cb{};
cb.mode     = WGPUCallbackMode_WaitAnyOnly;
cb.callback = /* ... */;
cb.userdata1 = &out;
WGPUFuture f = wgpuInstanceRequestAdapter(instance, opts_ptr, cb);
WGPUFutureWaitInfo wi{f, false};
wgpuInstanceWaitAny(instance, 1, &wi, UINT64_MAX);
```

On native this polls synchronously. On Emscripten, `wgpuInstanceWaitAny` maps to `emwgpuWaitAny` which calls `Asyncify.handleAsync` to suspend the WASM stack until the JS promise resolves. Requires `-sASYNCIFY=1` at link time.

### TimedWaitAny must be opted into

The WaitAny API requires an instance feature:

```cpp
WGPUInstanceFeatureName feats[] = {WGPUInstanceFeatureName_TimedWaitAny};
WGPUInstanceDescriptor idesc{};
idesc.requiredFeatureCount = 1;
idesc.requiredFeatures     = feats;
g.instance = wgpuCreateInstance(&idesc);
```

### Pass nullptr for adapter options on Emscripten

`WGPURequestAdapterOptions` contains Dawn-specific fields (e.g. `featureLevel`) that Chrome's WebGPU implementation doesn't recognize and rejects, returning null. Let the browser use its own defaults:

```cpp
#ifdef __EMSCRIPTEN__
    WGPURequestAdapterOptions const* opts_ptr = nullptr;
#else
    WGPURequestAdapterOptions opts{};
    opts.compatibleSurface = surface;
    opts.powerPreference   = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterOptions const* opts_ptr = &opts;
#endif
```

---

## Chrome WebGPU on Linux

### New GPUs are blocklisted

Chrome maintains a GPU allowlist for WebGPU. Unknown/new GPUs (e.g. RTX 5070 Ti / Blackwell at time of writing) have `navigator.gpu.requestAdapter()` return null even though `navigator.gpu` exists. Bypass:

```bash
google-chrome --ignore-gpu-blocklist --enable-unsafe-webgpu http://localhost:8002/cubey.html
```

### Firefox works out of the box

Firefox uses wgpu (Rust WebGPU implementation) over Vulkan on Linux, with no GPU allowlist. Prefer Firefox for development on new hardware.

### Checking Chrome's WebGPU status

`chrome://gpu` → look for "Dawn info" — if it shows `<Unknown GPU>` or `Compatibility Mode (ANGLE/OpenGL ES)`, Chrome is either blocklisting the GPU or Vulkan is disabled. `chrome://flags/#enable-vulkan` and `chrome://flags/#enable-unsafe-webgpu` can help.

### Making Chrome flags permanent

Chrome reads `~/.config/chrome-flags.conf` at every launch (one flag per line). Add persistent flags there rather than passing them on the command line:

```
--ignore-gpu-blocklist
--enable-unsafe-webgpu
```

---

## Compute shaders

### Compute → vertex buffer pattern

A storage buffer can carry both `WGPUBufferUsage_Storage` and `WGPUBufferUsage_Vertex` simultaneously. This is the core pattern for GPU-driven geometry (particles, marching cubes, SDF):

```cpp
// compute writes here; render reads it as a vertex buffer
g.vbuf = upload_buffer(WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex, ...);
```

In the compute BGL, bind it as `WGPUBufferBindingType_Storage` (read-write). In the render pass, bind it with `wgpuRenderPassEncoderSetVertexBuffer` as normal.

### Implicit barrier between passes

WebGPU guarantees ordering within a single command encoder. A compute pass that ends before a render pass begins does not need an explicit pipeline barrier — the GPU sees the writes before the render reads. Just structure the encoding order correctly:

```cpp
// compute pass first
WGPUComputePassEncoder cp = wgpuCommandEncoderBeginComputePass(encoder, ...);
/* dispatch */
wgpuComputePassEncoderEnd(cp);

// render pass after — vbuf writes are visible
WGPURenderPassEncoder rp = wgpuCommandEncoderBeginRenderPass(encoder, ...);
/* draw */
wgpuRenderPassEncoderEnd(rp);
```

### WGSL vec3 alignment in storage buffers

`array<vec3f>` has element stride **16**, not 12 — vec3 is padded to vec4 alignment in the WGSL memory layout. A tightly-packed `float[N*3]` buffer from C++ will be misread. Use `array<f32>` with manual indexing instead:

```wgsl
@group(0) @binding(1) var<storage, read> src: array<f32>;

let b   = vertex_index * 3u;
let pos = vec3f(src[b], src[b + 1u], src[b + 2u]);
```

This matches the 12-byte-stride vertex buffer layout on the C++ side exactly.

### Uniform struct size must match WGSL layout

WGSL pads structs to the alignment of their largest member. A struct containing `mat4x4f` (align 16) has its total size rounded up to a multiple of 16. A C++ struct that ends in a `float` after a `mat4` needs 12 bytes of explicit padding to match:

```cpp
struct Uniforms {
    glm::mat4 mvp;    // 64 bytes
    float     time;   //  4 bytes
    float     _pad[3]; // 12 bytes — round to 80 (next multiple of 16)
};
```

### Separate bind group layouts for compute and render

The same uniform buffer can be bound in both a compute BGL and a render BGL with different `visibility` flags. Create two layouts and two bind groups; they share the underlying `WGPUBuffer` object:

```cpp
// compute BGL: uniform (compute stage) + src storage + dst storage
// render BGL:  uniform (vertex stage) only
```

Layouts and pipeline layouts can be released immediately after pipeline / bind group creation — Dawn reference-counts them internally.

### Workgroup size and dispatch

With `@compute @workgroup_size(N)`, a single `dispatchWorkgroups(1, 1, 1)` launches exactly N threads. For small fixed-size work (e.g. 8 cube vertices), a workgroup sized to the data avoids wasted threads without needing a grid dispatch. Guard against out-of-bounds anyway:

```wgsl
@compute @workgroup_size(8)
fn cs_main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x >= 8u) { return; }
    ...
}
```
