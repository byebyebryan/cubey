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
