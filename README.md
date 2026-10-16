# Cubey

Cubey is a native C++20/Vulkan workbench for real-time rendering, GPU
simulation, and procedural environments. It is a collection of focused visual
projects built on a deliberately small shared foundation: enough engine to make
the experiments coherent, without turning the repository into a general game
engine or editor.

Projects own their scenes, simulation, configuration, and presentation. Shared
modules provide the recurring vocabulary—Vulkan resources, render passes,
camera and transform math, asset loading, procedural sky and clouds, terrain,
PBR, profiling, and deterministic headless capture.

[Build and run](docs/getting-started.md) · [Design](docs/DESIGN.md) · [Architecture](docs/architecture/README.md) · [Roadmap](docs/roadmap.md)

## Showcase

Five highlights show the workbench's range. Each highlight is an
independent eight-second, 720p/60 FPS
project story rather than a segment from a combined reel. All five native
players and their poster fallbacks are backed by committed media and reproducible
recipes. Fluid and glTF also retain their native 1080p masters below.

### Ocean

https://github.com/user-attachments/assets/dc923c55-1061-402f-92ad-09b7da2e7208

A Windy spectral sea carries afternoon light through golden hour and into
night. The slow camera arc keeps the horizon stable while the waves, foam,
cloud reflections, and atmosphere change together.

[Project](projects/ocean/README.md) · [Committed MP4](docs/media/showcase/ocean.mp4) · [Poster](docs/media/showcase/ocean.png)

### glTF + Terrain

https://github.com/user-attachments/assets/961c41fe-1bdf-4c0d-a936-1c7fad251f2f

The Damaged Helmet overlooks mountain streams and a lake on generated Terrain
Diffusion terrain. A face-on opening eases into a 45° orbit as afternoon becomes
sunset and early night, combining imported PBR materials with the shared
captured-water landscape renderer. Water levels are a held
SynxFlow storm state; camera, lighting and surface cues animate, not hydrology.

[Project](projects/gltf_viewer/README.md) · [720p MP4](docs/media/showcase/gltf-terrain.mp4) · [1080p master](docs/media/showcase/gltf-terrain-master-1080p.mp4) · [Poster](docs/media/showcase/gltf-terrain.png) · [Recipe](docs/media/showcase/gltf-terrain-recipe.json)

### Water 3D

https://github.com/user-attachments/assets/d1267204-7857-40d8-a259-4205605a5a75

An APIC dam break collapses through a long tank, then spreads and rebounds into
surface foam and secondary whitewater. The clear sky keeps the simulation and
screen-space reconstruction legible.

[Project](projects/fluid/water_3d/README.md) · [Committed MP4](docs/media/showcase/water-3d.mp4) · [Poster](docs/media/showcase/water-3d.png)

### Fire 3D

https://github.com/user-attachments/assets/50052e2e-21f0-46fe-be94-9d8a93a657d0

A warmed-up volumetric plume evolves from daylight into night. The fixed camera
lets combustion, turbulence, flame shaping, and smoke structure carry the
motion.

[Project](projects/fluid/fire_3d/README.md) · [Committed MP4](docs/media/showcase/fire-3d.mp4) · [Poster](docs/media/showcase/fire-3d.png)

<a id="fluid-25d--terrain-rain"></a>
### Terrain Water

https://github.com/user-attachments/assets/453158a4-156c-4031-85ec-f6481780ea08

Rain-fed streams collect into a lake on unmodified Joining valleys terrain.
A low, eased orbit reveals the relief as afternoon turns to sunset. This is
recorded [SynxFlow](https://github.com/SynxFlow/SynxFlow) timelapse rendered in Vulkan:
approximately six physical hours, an independent artistic solar clock, and exact
water states held between saves—not a live-FPS claim. This is the owner-selected sunset cut.

[Project](projects/terrain_water/README.md) · [8-second gallery MP4](docs/media/showcase/fluid-25d.mp4) · [Poster](docs/media/showcase/fluid-25d.png) · [1080p master](docs/media/showcase/fluid-25d-master-1080p.mp4) · [Recipe and provenance](docs/media/showcase/fluid-25d-recipe.json)

The [showcase package](docs/media/showcase/README.md) preserves the exact media,
posters, capture recipes, hashes, attachment mapping, and license boundaries.
The glTF-derived clip has a separate CC-BY-NC-4.0 media license; Cubey source
and the other original showcase media remain MIT licensed.

## Project map

Cubey's runnable projects are independent products, not scenes in one monolithic
application. They reuse foundation code where the contract is genuinely shared
and keep domain-specific policy local.

### Environments and rendering

| Project | Focus |
| --- | --- |
| [Atmosphere](projects/atmosphere/README.md) | Clear-sky scattering, solar time, twilight, stars, moon, Milky Way, and the shared surface-cloud layer. |
| [Ocean](projects/ocean/README.md) | Spectral FFT waves, clipmap LOD, persistent whitecaps, curved local horizon, and atmosphere/cloud lighting. |
| [Terrain](projects/terrain/README.md) | External-raster far backdrop with cached sector geometry, placement, material detail, and self-shadowing. |
| [Terrain Water](projects/terrain_water/README.md) | Terrain-water integration with live external SynxFlow, recorded fields, and shared terrain/water rendering. |
| [glTF Viewer](projects/gltf_viewer/README.md) | Staged glTF 2.0 PBR import, animation and deformation, required-use material extensions, procedural or HDR environments, shadows, and optional terrain. |
| [Fractal 2D](projects/fractal_2d) | Interactive Mandelbrot-style fullscreen shader with windowed navigation and headless output. |

Terrain and Ocean deliberately represent different scales: far backdrop and
local water surface. Planet rendering is a paused study. Surface-scale planet terrain,
shorelines, bathymetry, and shallow-water coupling remain separate work rather
than hidden extensions of those products.

### Simulation

| Project | Focus |
| --- | --- |
| [Smoke 2D](projects/fluid/smoke_2d/README.md) | GPU dye/velocity field with MacCormack advection, vorticity, and pressure projection. |
| [Water 2D](projects/fluid/water_2d/README.md) | APIC free-surface liquid with PIC/FLIP fallback, obstacles, hose/drain flow, and surface diagnostics. |
| [Water 3D](projects/fluid/water_3d/README.md) | APIC liquid, sorted particle-grid transfers, screen-space surface reconstruction, and whitewater. |
| [Fluid 2.5D](projects/fluid/fluid_25d/README.md) | Standalone builtin Vulkan shallow-water experiments, virtual-pipes/finite-volume alternatives and numerical diagnostics. |
| [Fire 3D](projects/fluid/fire_3d/README.md) | Dense volumetric combustion, projection, vorticity, shadow volume, and raymarching. |
| [Explosion 3D](projects/fluid/explosion_3d/README.md) | The shared pyro solver presented as repeated impulses with explosion-specific timing and controls. |

### Foundation examples

The cube-focused programs under [`examples/`](examples) isolate reusable
renderer behavior: transforms and depth, compute-generated textures, render
graph passes, shadow maps, instance-rate input, material handles, particle
compute, and no-window capture. They are small foundation tests rather than
showcase projects.

[`pbr_furnace`](projects/pbr_furnace) is likewise maintained as an internal
white-furnace validation target, not a public demo.

### Studies and retained references

- [Planet](studies/planet/README.md) is a paused orbital-rendering study with an
  explicit opt-in build; its renderer, design, and review tools are retained.
- [Terrain Hydrology](studies/terrain/hydrology/README.md) is a paused snapshot
  of the earlier regional terrain and landscape-evolution work.
- [Cloud reference study](studies/cloud/README.md), [Terrain Ref](studies/terrain/reference/README.md),
  and the [Terrain ShaderToy study](studies/terrain/shadertoy/README.md) retain
  reference evidence without competing with active products.
- [Archived terrain attempts](docs/archive/terrain/legacy-attempts.md) summarize
  the retired terrain workbench, terrain lab, and coastal demo; Git history
  remains the implementation archive.

## Foundation

- Native Vulkan is the primary backend and desktop is the primary target.
- Layered `cubey::*` libraries expose reusable headers under `include/cubey/`;
  projects consume the aggregate `cubey::cubey` target where appropriate.
- `cubey_host` supplies GLFW-backed windowing and the deterministic headless
  host without owning project policy.
- Each executable owns a typed Config V2 facade composed from shared host and
  engine schemas plus only its live project options.
- Headless PNG and optional H.264 MP4 capture are first-class validation and
  publication paths.
- ImGui provides project controls and diagnostics, but Cubey does not aim to
  become an editor.

The architecture is deliberately pressure-driven: shared abstractions are
promoted from repeated project needs, while Vulkan-specific requirements stay
visible when hiding them would make the code less useful.

## Build and run

Cubey needs a C++20 compiler, CMake, Ninja, Vulkan development packages, a
working Vulkan driver, and `glslangValidator`.

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Then launch a project, for example:

```bash
./build/dev/projects/ocean/ocean
./build/dev/projects/fluid/water_3d/water_3d
./build/dev/projects/gltf_viewer/gltf_viewer --input path/to/model.glb
```

See [Build and run Cubey](docs/getting-started.md) for distro packages,
headless PNG/MP4 capture, sample assets, validation, configuration precedence,
and common controls. For remote visual review, the
[local report browser](docs/getting-started.md#browse-local-result-reports)
provides an automatically discovered, searchable index of HTML result pages
under `outputs/`, served by a small Python HTTP server that can run in tmux.

## Documentation

The [docs index](docs/README.md) separates current architecture, project guides,
evidence, and archived work. Good starting points:

- [Design](docs/DESIGN.md) — purpose, tenets, and repository structure.
- [Architecture notes](docs/architecture/README.md) — current engine and
  rendering contracts.
- [Configuration V2](docs/architecture/configuration.md) — typed project-owned
  config, JSON serialization, CLI precedence, and validation.
- [Roadmap](docs/roadmap.md) — current readiness and likely next foundation
  work.
- [Showcase plan](docs/showcase.md) — shot design, capture contract, and media
  provenance.
- [Changelog](CHANGELOG.md) — implementation history and release notes.

Active development lives on `main`. The original OpenGL 4 shader playground is
preserved on the `legacy` branch.

## License

Cubey source and original project media are licensed under the [MIT License](LICENSE).
The Damaged Helmet-derived glTF showcase MP4s (gallery and native master) and poster are a media-only
CC-BY-NC-4.0 exception with their attribution recorded in the
[showcase manifest](docs/media/showcase/manifest.json).
