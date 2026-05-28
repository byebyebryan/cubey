# Ocean

`ocean` is a practical water-rendering project. It is deliberately separate from
the particle-grid `water_3d` tank: this demo starts from a camera-relative
surface renderer, not from a CFD solver.

The v1 target is scale. The surface follows the camera with a small ocean
clipmap, evaluates a multi-cascade spectral wave field in world space, fades
short wavelengths with distance, and blends the far mesh into the sky so the
ocean can read all the way to the horizon without obvious texture repetition.

Implemented:

- camera-relative clipmap mesh generated in the vertex shader, with denser
  geometry near the camera and coarser patches toward the horizon;
- in-repo Stockham FFT path for three spectral ocean cascades, including packed
  choppy displacement, slope, curvature, and crest-compression fields;
- bounded nonlinear macro crests layered over the FFT field for stronger
  asymmetric wave shape near the camera;
- persistent ping-ponged crest foam with coverage/freshness state, dispersion,
  and filtered breakup detail, instead of full-surface procedural normal
  shimmer;
- separated controls for wind direction, wind speed, fetch, spectrum spread,
  animation speed, and foam drift so visual scale, motion, and foam history can
  be tuned independently;
- compute-backed directional short-wave packets, storing geometric height,
  slope, and crest-gated foam source for both silhouette and shading;
- cascade-aware choppy displacement with a one-step displaced-position resample
  so crests and shading follow the folded spectral surface;
- coherent procedural sky pass, procedural seabed scene color/depth, Fresnel sky
  reflection, scene-depth refraction, Beer-style absorption, sun glint,
  crest/shallow-water foam, exposure/tonemap handling, and debug views including
  wireframe LOD, crest compression, foam source/history, and translucency
  inspection;
- first-class future hooks for local disturbances and shoreline/bathymetry
  masks, disabled by default so the baseline reads as wind-driven ocean.

Current translucency direction:

- render an opaque scene color/depth layer first, currently a procedural seabed
  plus sky;
- shade the ocean as a single water layer that samples scene color and depth
  behind the water for refracted bottom/scene color;
- derive optical thickness from water depth vs sampled scene depth, then apply
  Beer absorption, shallow scattering, Fresnel reflection, and foam on top;
- keep this ocean-local until there is pressure for a general transparent
  material/ordering system.

Deferred:

- arbitrary opaque scene integration, underwater camera volumes, waterline
  transitions, and true volumetric in-scattering;
- boats, buoyancy, wake databases, shoreline authoring, caustics, and underwater
  rendering;
- particle whitewater and physically meaningful plunging shore breakers.

Breaking waves are intentionally not part of v1. Deep-water whitecaps can be
approximated from crest steepness and foam, which is what this project does now.
Plunging shore breakers are a different class of problem: they need terrain or
bathymetry, surf-zone wave transformation, local nonlinear simulation or
particles, and strong scene interaction. The renderer keeps the hooks, but the
first milestone keeps the water surface scalable and readable.

Useful runs:

```sh
./build/dev/projects/ocean/ocean --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view detail --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view foam --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view wireframe --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view thickness --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view compression --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view foam-source --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --debug-view foam-history --frames 300 --width 1280 --height 720
./build/dev/projects/ocean/ocean --headless --frames 120 --width 640 --height 360 --output /tmp/cubey-ocean.png
./build/dev/projects/ocean/ocean --headless --capture video --frames 180 --fps 60 --width 1280 --height 720 --output /tmp/cubey-ocean.mp4
```

Controls:

- Left-drag: orbit camera.
- Mouse wheel: zoom.
- Space: pause or resume wave time.
- `R`: reset camera and wave time.
- `D`: cycle final, height, displacement, normal, foam, detail, reflection,
  refraction, spectrum, wireframe, scene-depth, thickness, transmittance,
  refraction-offset, compression, foam-source, and foam-history debug views.
- Escape: close.

Tuning notes:

- `Wind speed` and `Fetch` change the spectral peak and therefore the
  distribution of wave sizes.
- The FFT spectrum is split into swell, wind-wave, and short-wave bands so
  long shape, mid-scale ridges, and close detail can be balanced without
  restoring random macro bumps.
- `Spread` broadens directional variance, while `Small detail` strengthens the
  short/mid wave bands after unresolved wavelengths are filtered out.
- `Macro swell` adds restrained long-period swell only; the FFT/detail layers
  are expected to carry choppy wave shape.
- The default near/mid/far spectrum patch lengths are deliberately detuned
  instead of exact multiples, which keeps low-angle views from locking onto a
  repeated cascade period.
- `Animation speed` changes phase progression only.
- `Foam drift` moves the persistent foam history along the wind direction only.
- `Spectral geometry` controls FFT displacement strength independently from
  `Normal strength`, so reducing normals does not flatten the surface.
- Cascade-aware chop keeps near crests stronger than far cascades, and the
  surface shaders resample once at the displaced position so sharp features are
  not only a normal-map effect.
- `Detail chop`, `Detail spread`, `Detail geometry`, and `Crest sharpness`
  control packeted directional short-wave shape.
- `Foam coverage`, `Foam breakup`, and `Foam dispersion` control how much
  compression-driven foam is generated, how patchy it is, and how old foam
  spreads before decay.
- `Absorption`, `Refraction px`, `Water opacity`, `Scattering`, and `Seafloor`
  controls shape the single-layer-water translucency path. The procedural
  seafloor is optional and off by default for open-ocean shots; when enabled,
  seafloor detail is the sediment/relief contrast, separate from brightness.
- Lower `Absorption` with `Seafloor` enabled pushes the look toward clear
  tropical shallows; higher absorption with seafloor disabled reads as deeper
  open ocean.
- `Base cells`, `LOD levels`, and `Extent` control the camera-relative clipmap.
  Wireframe view is the fastest way to inspect close-up density and transition
  placement.

Primary references:

- Jerry Tessendorf, "Simulating Ocean Water":
  <https://people.computing.clemson.edu/~jtessen/reports/papers_files/coursenotes2004.pdf>.
- GPU Gems Chapter 1, "Effective Water Simulation from Physical Models":
  <https://developer.nvidia.com/gpugems/gpugems/part-i-natural-effects/chapter-1-effective-water-simulation-physical-models>.
- GPU Gems 2 Chapter 19, "Generic Refraction Simulation":
  <https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation>.
- TDM/Inigo Quilez-style Shadertoy seascape references for presentation:
  <https://www.shadertoy.com/view/MdXyzX>.
- Unreal Single Layer Water for scene color/depth based water composition:
  <https://dev.epicgames.com/documentation/unreal-engine/single-layer-water-shading-model-in-unreal-engine>.
- Crest Ocean System docs for transparent ocean/refraction tradeoffs and
  production-facing water controls: <https://docs.crest.waveharmonic.com/>.
- Sea of Thieves technical art notes for art-directed wave peak masking,
  scattering, and foam persistence:
  <https://history.siggraph.org/wp-content/uploads/2022/09/2018-Talks-Ang_The-Technical-Art-of-Sea-of-Thieves.pdf>.
- Subnautica rendering notes for stylized depth attenuation, water volumes, and
  environmental readability:
  <https://www.gamedeveloper.com/design/how-i-subnautica-i-plunges-deeper-into-rendering-realistic-water>.
- Unreal Water System and Unity HDRP Water System public docs for water-system
  scope and production tradeoffs.
