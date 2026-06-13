# cubey

Cubey is a native desktop GPU workbench for procedural graphics experiments and
projects. The goal is a small, deliberate C++/Vulkan foundation that keeps
engine, host, resource, input, camera, transform, and project boundaries explicit
while leaving the interesting work in shaders, compute, and project code.

Cubey is not a generic game engine, editor, SDK, or backend-agnostic renderer.
It should still use established graphics terminology and proven public
precedent when shaping new foundation contracts.

Active development lives on `main`. The original OpenGL 4 shader playground is
preserved on the `legacy` branch.

## Current Direction

- Primary target: native Vulkan on desktop.
- Primary library: layered `cubey::*` targets with public headers under
  `include/cubey/` and an aggregate `cubey::cubey` target for examples and
  projects.
- Optional host layer: `cubey_host`, for GLFW-backed window hosting.
- Runnable targets live under `examples/` or `projects/`; examples are
  intentionally cube-focused renderer demos, while richer/non-cube work lives
  under `projects/`.
- Headless PNG and optional MP4 output are first-class verification/capture
  paths for projects that can render without a window.
- Runtime debug UI uses ImGui where projects need live controls; Cubey still
  does not aim to become an editor.

Current examples:

- `spinning_cube`: primitive cube mesh with shared transform/camera math and depth.
- `textured_cube`: primitive cube mesh, compute-generated texture, descriptors,
  scene lighting, and input.
- `shadow_cube`: primitive cube/plane meshes, graph-declared directional shadow
  map, transient scene color target, and fullscreen triangle present pass.
- `instanced_cubes`: real instance-rate vertex input with one cube mesh and many
  cube instances.
- `material_cubes`: multiple material handles and material instances bound per
  scene draw packet.
- `headless_cube`: no-window offscreen cube PNG/MP4 capture path.
- `particle_cubes`: compute-updated cube particles rendered as indexed cube
  instances.

Current projects:

- `atmosphere`: clear-sky scattering workbench with solar time of day, twilight,
  procedural stars, moon rendering, Milky Way atlas layers, and headless output.
- `planet`: Earth-scale planet rendering foundation with camera-relative
  cube-sphere LOD, procedural terrain fields, local-detail diagnostics,
  shared sky/celestial state, physical atmosphere preview, HDR post, and
  headless visual smoke coverage.
- `terrain_lab`: local terrain generation workbench with deterministic CPU
  fields, arid mesa canyon and watershed slices, heightfield mesh rendering,
  field/debug views, material and biome masks, headless visual smoke coverage,
  and future adapters into planet, ocean, or environment projects.
- `smoke_2d`: compute-updated dye/velocity field with MacCormack advection,
  vorticity, pressure projection, debug views, and
  deterministic headless capture output.
- `water_2d`: 2D APIC free-surface liquid with a PIC/FLIP fallback,
  particle-grid transfers, MAC-grid pressure projection, reset presets,
  hose/drain material flow, obstacle shapes, surface/foam debug views, live
  frame/memory diagnostics, and deterministic headless capture output.
- `water_3d`: 3D APIC free-surface liquid in a long showcase tank, with stable
  particle-grid transfer ranges, screen-space water surface rendering, rain,
  hose/drain/wave controls, whitewater, shared procedural environment lighting,
  profiling diagnostics, and headless capture output.
- `fire_3d`: dense volumetric pyro fire demo with 3D storage textures,
  MacCormack advection, combustion, projection, vorticity confinement,
  raymarching, shared procedural environment lighting, orbit camera controls,
  debug views, and headless capture output.
- `explosion_3d`: the same shared 3D pyro solver presented as repeated impulse
  bursts with explosion-specific timing, boost controls, and shared environment
  lighting.
- `ocean`: active ocean renderer derived from the GodotOceanWaves
  spectrum/FFT/unpack core, with configurable cascade slots, atmosphere
  lighting, terrain-field hooks, foam/debug views, and feature-isolation
  controls.
- `procedural_terrain`: deterministic heightfield terrain and bathymetry data
  demo with shoreline/material debug views and headless capture output.
- `fractal_2d`: fullscreen Mandelbrot-style shader with windowed navigation and
  headless output.
- `gltf_viewer`: glTF/glb viewer for imported assets, PBR materials, texture
  upload, rigid/morph/skinned animation, generated or HDR-backed IBL, skybox
  rendering, shadow maps, and headless capture.
- `pbr_furnace`: white-furnace PBR validation scene for roughness/metallic
  behavior under uniform generated IBL.

## Documentation

Start with the [docs index](docs/README.md).

Authoritative current docs:

- [Design](docs/DESIGN.md)
- [Roadmap](docs/roadmap.md)
- [Architecture notes](docs/architecture/README.md)
- [Vulkan abstraction map](docs/architecture/vulkan-abstractions.md)
- [Renderer foundation](docs/architecture/renderer-foundation.md)
- [PBR and IBL direction](docs/architecture/pbr-ibl.md)
- [Render graph direction](docs/architecture/render-graph.md)
- [Entity and component foundation](docs/architecture/entity-component-foundation.md)
- [Host and engine](docs/architecture/host-engine.md)
- [Threading and async](docs/architecture/threading-and-async.md)
- [glTF assets and PBR](docs/architecture/gltf-assets.md)
- [Animation and deformation](docs/architecture/animation-deformation.md)
- [Fluid simulation direction](docs/architecture/fluid-simulation.md)
- [Ocean rendering](docs/architecture/ocean-rendering.md)
- [Ocean horizon and curved-local scale](docs/architecture/ocean-horizon-and-planet-scale.md)
- [Planet rendering](docs/architecture/planet-rendering.md)
- [Ocean adjacent systems](docs/architecture/ocean-adjacent-systems.md)
- [C++ style guide](docs/cpp-style.md)
- [Changelog / release notes](CHANGELOG.md)

Project-local docs:

- [Atmosphere](projects/atmosphere/README.md)
- [Fluid overview](projects/fluid/README.md)
- [Smoke 2D](projects/fluid/smoke_2d/README.md)
- [Water 2D](projects/fluid/water_2d/README.md)
- [Water 3D](projects/fluid/water_3d/README.md)
- [Fluid 2.5D design](projects/fluid_25d/README.md)
- [Fire 3D](projects/fluid/fire_3d/README.md)
- [Explosion 3D](projects/fluid/explosion_3d/README.md)
- [Planet](projects/planet/README.md)
- [Terrain Lab](projects/terrain_lab/README.md)
- [Ocean](projects/ocean/README.md)
- [Procedural Terrain](projects/procedural_terrain/README.md)

## Development Setup

Cubey needs a native C++20 toolchain plus system Vulkan development packages.
CMake fetches several project dependencies when needed, but it does not provide
the Vulkan SDK/loader, GPU driver, compiler toolchain, or shader compiler.

Required system dependencies:

- C++20 compiler and standard build tools.
- CMake, Ninja, and Git.
- Vulkan headers.
- Vulkan loader / ICD loader (`libvulkan.so` on Linux).
- A Vulkan-capable GPU driver / ICD for your hardware.
- `glslangValidator` for build-time GLSL to SPIR-V shader compilation.

Package names vary by distro. Examples:

```bash
# Arch Linux
sudo pacman -S --needed base-devel cmake ninja git vulkan-headers vulkan-icd-loader vulkan-tools glslang
# Also install one Vulkan driver package for your GPU, such as vulkan-radeon,
# vulkan-intel, amdvlk, or the NVIDIA driver stack.
# Optional for MP4 capture: sudo pacman -S --needed pkgconf ffmpeg

# Ubuntu / Debian
sudo apt install build-essential cmake ninja-build git libvulkan-dev vulkan-tools glslang-tools
# Also install the Vulkan driver package for your GPU, such as
# mesa-vulkan-drivers or the vendor driver stack.
# Optional for MP4 capture: sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev

# Fedora
sudo dnf install gcc-c++ cmake ninja-build git vulkan-headers vulkan-loader-devel vulkan-tools glslang
# Also install the Vulkan driver package for your GPU, such as
# mesa-vulkan-drivers or the vendor driver stack.
# Optional for MP4 capture: install pkgconf-pkg-config and FFmpeg/libav
# development packages from your enabled repositories.
```

Optional but useful:

- Vulkan validation layers for local smoke runs with `--require-validation`.
- `pkg-config` plus FFmpeg/libav development packages for in-process H.264 MP4
  capture. CMake controls this with `CUBEY_VIDEO_CAPTURE=AUTO|ON|OFF`; `AUTO`
  enables it when `libavcodec`, `libavformat`, `libavutil`, and `libswscale`
  are found.

Use the CMake presets as the default entrypoint:

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

GLFW, cgltf, stb, Basis Universal, and GLM fallback sources are resolved by
CMake through `FetchContent` or `find_package` where appropriate; they are not
the system packages that make Vulkan itself available.

Optional sample assets can be fetched at configure time:

```bash
cmake --preset dev -DCUBEY_FETCH_GLTF_SAMPLE_ASSETS=ON -DCUBEY_FETCH_HDR_SAMPLE_ASSETS=ON
```

Useful windowed smokes:

```bash
./build/dev/examples/spinning_cube/spinning_cube --frames 300 --width 1280 --height 720
./build/dev/examples/textured_cube/textured_cube --frames 300 --width 1280 --height 720
./build/dev/examples/shadow_cube/shadow_cube --frames 300 --width 1280 --height 720
./build/dev/examples/instanced_cubes/instanced_cubes --frames 300 --width 1280 --height 720
./build/dev/examples/material_cubes/material_cubes --frames 300 --width 1280 --height 720
./build/dev/examples/material_cubes/material_cubes --debug-view normal --frames 300 --width 1280 --height 720
./build/dev/examples/particle_cubes/particle_cubes --frames 300 --width 1280 --height 720
./build/dev/projects/fractal_2d/fractal_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/smoke_2d/smoke_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/water_2d/water_2d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/water_3d/water_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/fire_3d/fire_3d --frames 300 --width 1280 --height 720
./build/dev/projects/fluid/explosion_3d/explosion_3d --frames 300 --width 1280 --height 720
./build/dev/projects/atmosphere/atmosphere --frames 300 --width 1280 --height 720
./build/dev/projects/planet/planet --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --ocean-map-size 128 --frames 300 --width 1280 --height 720
./build/dev/projects/procedural_terrain/procedural_terrain --frames 300 --width 1280 --height 720
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/model.glb --environment path/to/env.hdr --animation-index 0 --animation-speed 1.0 --frames 300 --width 1280 --height 720
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/model.glb --debug-view roughness --frames 300 --width 1280 --height 720
./build/dev/projects/pbr_furnace/pbr_furnace --frames 300 --width 1280 --height 720
```

Windowed `--frames` runs print a final `windowed_perf` FPS/frame-time summary.
Use `--print-frame-stats` for periodic stdout samples while a window remains
open; the window title also shows the latest sampled FPS and frame time.
Shared run options can also be loaded from JSON config files:

```bash
./build/dev/projects/ocean/ocean --config ocean.json --set ocean.map_size=512
./build/dev/projects/atmosphere/atmosphere --write-config-template atmosphere-template.json
```

Config precedence is defaults, `--config`, named CLI flags, then `--set
path=value`. Config descriptors carry stable paths, labels, groups, value
ranges, enum choices, and help text for config templates, generic `--set`
overrides, and descriptor-backed named CLI flags. Runtime ImGui panels are still
hand-authored project surfaces, but active projects use shared group/control
helpers so hover help and hierarchy stay visually consistent.
`smoke_2d` defaults to a `1024x1024` solver grid and five procedural
injectors; use `--grid-width`, `--grid-height`, and `--smoke-injectors 1..16` to
compare other simulation/demo shapes. Use `--smoke-pressure-solver jacobi|rbgs`
to compare the Jacobi and red-black Gauss-Seidel pressure paths;
`--profile-diagnostics` is available in headless smoke runs when paired with
`--profile-output`.
`water_2d` defaults to a `256x144` MAC grid with APIC particle-grid transfer and
a PIC/FLIP fallback. It uses particles for liquid motion and a face-centered
grid for pressure, so it is intentionally a different solver family from
`smoke_2d`. Runtime UI controls cover reset presets, transfer mode, fill volume,
hose emission, bottom drain, obstacle shape, substeps, pressure iterations,
PIC/FLIP blend, collision damping, particle separation, surface/foam shading,
and Water2D frame/memory diagnostics.
`water_3d` defaults to a `128x64x48` long-tank APIC liquid scene. It keeps
particle storage stable and builds sorted particle-index ranges for transfer,
then renders the default view through a screen-space surface path with
refraction, absorption, environment reflection, foam, and secondary whitewater.
By default it uses the shared procedural atmosphere as its sky/reflection/
lighting source; `--pbr-environment-source static` keeps the older static IBL
fallback.
Use `--water3d-transfer apic|pic-flip`, `--water3d-transfer-limit N`,
`--water3d-p2g-mode active|tiled`, `--water3d-hose`, `--water3d-drain`,
`--water3d-rain`, `--water3d-wave`, `--water3d-whitewater`, and
`--profile-diagnostics` for focused solver/render profiling.
`fire_3d` and `explosion_3d` share the `pyro_3d` dense solver core. They default
to a `128x128x128` solver volume with a decoupled `64x64x64` shadow volume.
Their raymarch and shadow paths use the shared procedural atmosphere for dynamic
light direction, color, sky tint, and exposure by default; pass
`--pbr-environment-source static` for the legacy fixed-light fallback. Use
`--grid-width`, `--grid-height`, `--grid-depth`, `--shadow-grid-width`,
`--shadow-grid-height`, `--shadow-grid-depth`, `--shadow-steps`,
`--shadow-update-interval`, `--pyro-sources`, `--pyro-source-radius`,
`--pyro-source-force`, `--pyro-soot`, `--pyro-temperature`, `--pyro-fuel`,
`--pyro-buoyancy`, `--pyro-ignition-temperature`, `--pyro-burn-rate`,
`--pyro-heat-output`, `--pyro-soot-yield`, `--pyro-expansion`,
`--pyro-flame-cooling`, `--pyro-shredding`, `--pyro-turbulence`,
`--pyro-obstacle-height`, and `--pyro-obstacle-radius` for lower-cost smoke
tests or heavier local runs. `explosion_3d` also accepts `--explosion-interval`,
`--explosion-duration`, and `--explosion-boost`.
`--print-frame-stats` also emits periodic `pyro_3d_gpu` pass timings when
timestamp queries are available.
`ocean` is a rendering-focused water project rather than a CFD solver. It now
starts from the GodotOceanWaves-derived spectrum/FFT/unpack path and exposes
`--ocean-map-size 128|256|512|1024`,
`--debug-view final|height|displacement|normal|foam|foam-source|foam-history|foam-core|foam-candidate|foam-detail|lod|sky-radiance|reflection|direct-light|ambient-light|exposure|raw-foam|lit-foam|terrain-depth|terrain-shore|terrain-slope`,
and `--ocean-cascade all|0|1|2|3|4` for focused inspection. Use
`--ocean-wire-overlay`, `--ocean-wire-opacity 0.0..1.0`,
`--ocean-spectral-domains`, `--no-ocean-spectral-domains`,
`--ocean-terrain-fields`, and `--no-ocean-terrain-fields` for captured
diagnostics. The GUI's Feature Isolation section exposes global shape and foam
strength, foam history, active cascade-slot work toggles, shape/detail
anti-repeat, split atmosphere material influence, shape/normal/foam fade
distances, and terrain foam controls for checking which additions help or hurt
the reference-derived core.
The old `ocean_ref` and `ocean_legacy` projects were retired after their useful
comparison and donor work landed in `ocean`; use git history if a deleted
implementation detail is needed for archaeology.
`procedural_terrain` generates deterministic island/coast/shelf/seabed fields
and renders an oblique heightfield mesh. It exposes
`--debug-view final|height|water-depth|shoreline|material|slope`, plus
`--grid-width` and `--grid-height` for lower-cost checks or denser captures.
Underscore spellings such as `water_depth` remain accepted for older scripts.

Useful headless PNG smokes:

```bash
./build/dev/examples/headless_cube/headless_cube --width 640 --height 360 --output /tmp/cubey-headless-cube.png
./build/dev/projects/fractal_2d/fractal_2d --headless --width 640 --height 360 --output /tmp/cubey-fractal-2d.png
./build/dev/projects/fluid/smoke_2d/smoke_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-smoke-2d.png
./build/dev/projects/fluid/water_2d/water_2d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-water-2d.png
./build/dev/projects/fluid/water_3d/water_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-water-3d.png
./build/dev/projects/fluid/fire_3d/fire_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-fire-3d.png
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-explosion-3d.png
./build/dev/projects/atmosphere/atmosphere --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-atmosphere.png
./build/dev/projects/planet/planet --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-planet.png
./build/dev/projects/ocean/ocean --headless --frames 120 --width 640 --height 360 --ocean-map-size 128 --output /tmp/cubey-ocean.png
./build/dev/projects/procedural_terrain/procedural_terrain --headless --width 640 --height 360 --output /tmp/cubey-procedural-terrain.png
./build/dev/projects/pbr_furnace/pbr_furnace --headless --width 640 --height 360 --output /tmp/cubey-pbr-furnace.png
```

Useful headless video captures when FFmpeg/libav support is enabled:

```bash
./build/dev/examples/headless_cube/headless_cube --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-headless-cube.mp4
./build/dev/projects/gltf_viewer/gltf_viewer --headless --capture video --frames 180 --fps 60 --input path/to/model.glb --environment path/to/env.hdr --output /tmp/cubey-gltf-viewer.mp4
./build/dev/projects/fluid/smoke_2d/smoke_2d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-smoke-2d.mp4
./build/dev/projects/fluid/water_2d/water_2d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-water-2d.mp4
./build/dev/projects/fluid/water_3d/water_3d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-water-3d.mp4
./build/dev/projects/fluid/fire_3d/fire_3d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-fire-3d.mp4
./build/dev/projects/fluid/explosion_3d/explosion_3d --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-explosion-3d.mp4
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 23.0 --planet-time-speed-hours-per-second 0.0 --planet-camera-mode orbit --planet-camera-altitude-m 14000000 --planet-camera-orbit-spin-deg-per-sec 8.0 --planet-atmosphere-mode physical --output /tmp/cubey-planet-orbit.mp4
./build/dev/projects/planet/planet --headless --capture video --frames 900 --fps 30 --width 1280 --height 720 --planet-day-of-year 80 --planet-time-hours 4.35 --planet-time-speed-hours-per-second 0.03 --planet-camera-mode surface --planet-camera-altitude-m 1200 --planet-camera-surface-look sun --planet-camera-surface-pitch-deg 22 --planet-atmosphere-mode physical --output /tmp/cubey-planet-surface-twilight.mp4
./build/dev/projects/ocean/ocean --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --ocean-map-size 128 --output /tmp/cubey-ocean.mp4
```

Use `--require-validation` on local smoke commands when Vulkan validation
layers are installed.

## Controls

- `textured_cube`: left-drag orbits the camera, Space pauses/resumes
  auto-orbit, `R` resets the camera, Escape closes.
- `shadow_cube`: left-drag orbits the camera, `R` resets the camera, Escape
  closes.
- `instanced_cubes`: left-drag orbits the camera, `R` resets the camera, Escape
  closes.
- `material_cubes`: left-drag orbits the camera, `R` resets the camera, `D`
  cycles PBR debug views, Escape closes.
- `particle_cubes`: left-drag orbits the camera, Space pauses/resumes compute
  updates, `R` resets the camera and cube field, Escape closes.
- `fractal_2d`: left-drag pans, mouse wheel zooms around the cursor, `R` resets,
  Escape closes.
- `smoke_2d`: Space pauses/resumes, `R` resets, `D` cycles
  dye/velocity/divergence/pressure/speed/vorticity views, Escape
  closes.
- `water_2d`: Space pauses/resumes, `R` resets, `D` cycles
  surface/particles/cells/velocity/divergence/pressure/solid/foam views, Escape
  closes.
- `water_3d`: left-drag orbits the camera, mouse wheel zooms, Space
  pauses/resumes, `R` resets, `D` cycles surface/particles/cells/velocity/
  pressure/solid/overpack/surface diagnostic/whitewater views, Escape closes.
- `fire_3d` / `explosion_3d`: left-drag orbits the camera, mouse wheel zooms,
  Space pauses/resumes, `R` resets, `D` cycles smoke/density/velocity views,
  Escape closes.
- `gltf_viewer`: left-drag orbits the camera, `D` cycles PBR debug views,
  Escape closes.
- `ocean`: left-drag orbits the camera, mouse wheel
  zooms, Space pauses/resumes wave time, `R` resets, `D` cycles wave-core debug
  views, Escape closes.
- `planet`: left-drag orbits the planet, right-drag looks around in surface
  mode, mouse wheel changes distance, WASD moves the surface camera, Escape
  closes.
- `pbr_furnace`: left-drag orbits the camera, Escape closes.

`--debug-view` currently accepts `final`, `base-color`, `normal`,
`geometric-normal`, `roughness`, `metallic`, `occlusion`, `emissive`, `shadow`,
`alpha`, and `uv0` on the shared forward-PBR path.

## License

Cubey is licensed under the [MIT License](LICENSE).
