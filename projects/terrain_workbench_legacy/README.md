# Terrain Workbench Legacy

`projects/terrain_workbench_legacy` preserves the rebooted terrain workbench. It starts as a local
CPU/reference terrain product generator, not as a direct continuation of
`terrain_lab_legacy`, not as a coastal/ocean demo, and not as a planet renderer.

The first goal is a deterministic product contract that downstream systems can
inspect and eventually consume:

- coherent source fields;
- terrain feature and process fields;
- material and vegetation-potential hints;
- summaries and debug exports.

The current first slice is a temperate mountain river catchment over a local
kilometer-scale grid. A renderer-backed preview app now consumes the product for
perspective review, but final terrain rendering, ocean integration, planet
streaming, foliage rendering, and physically complete erosion remain deferred
until the product fields are credible.

The current generator revision is `34`. It emits source fields, height/slope
analysis, static drainage, routing diagnostics, smoothed active river trunk and
tributary fields, incision/process diagnostics, wetness/deposition, material
masks, and vegetation potential.
The drainage pass now repairs local routing pits with a bounded priority-flood
epsilon fill. River topology still uses D8 graph traversal where that is useful
for connectivity, but selected channel paths are converted to sub-cell
centerlines, nudged by continuous flow direction, and rasterized with bounded
meander before they become product masks. Revision 12 restores more active
network coverage by allowing more default tributary/order-seed candidates and by
spacing stress support paths against already accepted support geometry, not only
against confluence points. Revision 13 keeps that routing model but promotes
major stress support, order-seed, and high-scoring branch paths into
`river_trunk`, then tunes the stress trunk band to stay broad at review
thresholds without recreating long high-strength straight runs. Revision 14
keeps the stress-only hierarchy but requires promoted branches to add distinct
visible area away from the current trunk skeleton, pruning nearby parallel
alternatives that read as repeated routes through the same corridor. This
remains process-informed rather than a full hydraulic simulation. Revision 15
keeps the stress trunk as a continuous mainstem, restores the longer
accumulation trunk candidate, and grows a connected basin-tree tributary network
from edge-biased stream-order candidates so the stress review exposes broader
river reach without rendering unrelated graph edges. Revision 16 makes stress
trunk promotion provisional at render time: candidates are accepted only if the
painted trunk remains dominated by one connected component. It also tightens
grid-aligned support caps and pre-curves connected support/order-seed/corridor
branches before rasterization to reduce obvious D8-looking tributary strokes.
Revision 17 pivots the stress recipe away from raster-routing-derived topology:
stress topology now comes from a deterministic jittered river graph over the
padded hidden domain, computes graph discharge/order, and then rasterizes
accepted graph paths through the existing channel pipeline. D-Infinity
accumulation remains a diagnostic and validation field, not the stress recipe's
visible source of truth.
Revision 18 keeps the public field contract stable but changes stress hierarchy:
`river_trunk` now means major channels, so selected high-discharge/high-order
graph tributaries are painted with the mainstem while smaller attached branches
remain in `tributaries`.
Revision 19 adds explicit mountain source products: broad mountain support,
ridge support, peak support, mountain uplift, and peak uplift. The new
`temperate-mountain-range-stress` recipe is an isolated mountain-driver review
slice; existing river recipes emit the fields but keep broad mountain and peak
uplift disabled for stability. Use `mountain-relief.png` as the primary rendered
review image for that recipe; `final.png` remains the river/material debug
composition.
Revision 20 adds a mountain hierarchy layer for that stress recipe:
`mountain_range_spine`, `mountain_ridge_hierarchy`, and
`mountain_peak_candidates`. The default river recipes keep range spine and peak
candidate fields disabled while preserving their existing ridge source.
Revision 21 pivots the stress recipe to a peak-first mountain skeleton:
`mountain_envelope`, `mountain_peak_anchors`, `mountain_peak_prominence`,
`mountain_ridge_skeleton`, and `mountain_ridge_influence`. The older mountain
support, ridge hierarchy, and peak-candidate fields are now derived from that
envelope/anchor/skeleton source path in the mountain stress recipe. Default
river recipes still emit inactive values for the new stress-only diagnostics.
Revision 22 keeps that field contract but makes the mountain stress recipe read
more clearly as terrain building into high peaks. It replaces the generic
base-elevation tilt with an envelope-driven mountain base, strengthens peak
uplift, broadens ridge shoulders, gates residual detail against the mountain
source fields, and retunes `mountain-relief.png` around elevation hierarchy.
Revision 23 makes active rivers modify the terrain height product. It preserves
`pre_process_height_m`, applies channel and valley incision driven by the river
fields, publishes the carved result as `height_m`, and recomputes slope, local
relief, material masks, and vegetation potential from that carved height.
Renderer-backed previews now support material, height, river, and channel color
modes so reviewers can separate geometry from river tint.
Revision 24 adds a clean-room mountain gully diagnostic for
`temperate-mountain-range-stress`: `erosion_delta_m`, `gully_mask`,
`crease_proxy`, and `post_erosion_height_m`. These fields are review-only; they
do not modify `height_m`, materials, rivers, wetness, or vegetation.
Revision 25 adds explicit mountain macro fields for the same stress recipe:
`mountain_mass`, `mountain_shoulder`, and `mountain_summit_core`. The mountain
source now separates broad highland mass, foothill/shoulder buildup, and sparse
summit cores before local detail is applied. `terrain_workbench_preview_legacy` can also render
`height`, `post-erosion`, or `pre-process` surfaces so diagnostic surfaces can
be compared without changing the product contract.
Revision 26 changes the mountain stress recipe from additive feature stacking
to a coherent height profile. `mountain_profile_height_m` is now the source
height for that recipe, ridge/peak/uplift fields are diagnostics and bounded
attribution, and visible ridges come from smooth connection influence instead
of 8-neighbor raster paths. This removes the worst flat shoulder shelves,
needle peaks, and jagged ridge strokes from the revision 25 preview.
Revision 27 curves the ridge source, elongates summit support, and adds
`mountain_saddle_gate` so highland mass can be suppressed where it is not
supported by nearby crest or summit structure.
Revision 28 broadens ridge bodies, softens saturated mass/shoulder/saddle
fields, and shifts more visible mountain shape into broad profile support
instead of direct crest and summit spikes.
Revision 29 adds bounded thermal talus diagnostics for
`temperate-mountain-range-stress`: `thermal_erosion_delta_m`,
`talus_deposition_m`, and `slope_instability`. These fields only affect
`post_erosion_height_m`; `height_m` remains the product surface.
Revision 30 adds `mountain-process-review.png` and retunes the mountain stress
source profile so mass and shoulder buildup carry more of the range shape before
ridge and summit modulation.
Revision 31 adds explicit mountain ridge/valley process fields:
`mountain_ridge_body`, `mountain_valley_floor`, and
`mountain_valley_incision_m`. The mountain stress profile now routes more shape
through broad ridge bodies and a bounded valley-floor lowering stage before
detail is applied. This improves the perspective read, but the new scalar
diagnostics still expose the generated ridge skeleton as source-shaped bands.
Revision 32 pivots the mountain stress recipe to a clean-room visual source:
`mountain_visual_source_height_m`, `mountain_ridged_chain`,
`mountain_detail_weight`, `mountain_morphology_delta_m`,
`mountain_crease_map`, and `mountain_ridge_map`. The visible height now comes
from the ridged visual source plus bounded morphology; the older graph
ridge/valley fields remain diagnostics and no longer drive visible valley
incision.
Revision 34 adds `terrain-engine-ref`, an isolated TerrainEngine-inspired
height/material reference recipe. It samples TerrainEngine's shader-side value
noise/cubic height recipe into the existing CPU terrain product contract while
keeping river carving, mountain source diagnostics, gully diagnostics, and
thermal talus inactive for that recipe.

The current foundation pass keeps terrain process math in terrain-local helpers:
spread, relief-clamped lowering, height lowering, the diagnostic gully pass, and
the bounded thermal talus diagnostic, and the mountain stress ridge/valley
process fields.
The scalar review export writes `manifest.json` with recipe, grid, summary,
field stats, view names, and output filenames.
`terrain` and `terrain_workbench_preview_legacy` also accept `--profile-output <prefix>`. Bare
prefixes are written under `outputs/profiles/`, and each run emits
`<prefix>.terrain_phases.json` with coarse CPU phase timings and run metadata.

The current terrain path is intentionally a CPU debug/product workbench. The
large retained field set and scalar export matrix buy inspectability while the
source/process model is still changing, but they are not the intended
scene-scale runtime shape. Future scene and planet terrain should consume a
smaller runtime product through tiles, clipmaps, shader-side detail, or other
view-dependent sampling. CPU multithreading may reduce review latency after
profiling identifies the hot phases, but it does not replace LOD or streaming.

See [Terrain reboot direction](../../docs/architecture/terrain-reboot.md) for
the current design checkpoint. The staged lane map is captured in
[Terrain project map](../../docs/notes/terrain-project-map.md): source drivers,
process operators, product fields, review consumers, and integration adapters.
The ShaderToy terrain/hydro extraction boundary is captured in
[Terrain ShaderToy operator extraction](../../docs/notes/terrain-shadertoy-operator-extraction.md):
use those refs for clean-room process diagnostics and visual vocabulary, not
river topology or shader ports.
The TerrainEngine reference lane is captured in
[TerrainEngine reference port plan](../../docs/notes/terrain-engine-reference-port-plan.md):
use it as an isolated known-good height/material recipe, not as a commitment to
port TerrainEngine's OpenGL app or tessellation stack. The same note captures
the broader reference review: TerrainEngine has a useful distance-adaptive
tessellated runtime and water presentation path, but it does not provide biome
recipes, hydraulic erosion, lake generation, or foliage rendering.
The preview app now has a `terrain-engine-ref` runtime mode that samples that
reference height function in GLSL over a Cubey clipmap review mesh. This is a
runtime visual reference, not a full infinite terrain renderer: the current
captures still show a finite review patch edge and the water pass is only a
waterline clamp/tint, not reflection/refraction water.

## Commands

```sh
cmake --build --preset dev --target cubey_project_terrain_workbench_legacy cubey_project_terrain_workbench_preview_legacy cubey_project_terrain_workbench_legacy_tests
ctest --preset dev -R terrain_workbench_legacy --output-on-failure

./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --terrain-debug-view final --output outputs/terrain/current/final.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --terrain-debug-view flow-accumulation --grid-size 129 --output outputs/terrain/current/flow-accumulation.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 513 --terrain-debug-view all --terrain-output-dir outputs/terrain/current-river-network
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 513 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 1025 --recipe temperate-mountain-river-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/stress-river-network-1025
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 513 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/mountain-range-stress
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 1025 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/mountain-range-stress-1025
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 513 --recipe terrain-engine-ref --terrain-debug-view all --terrain-output-dir outputs/terrain/terrain-engine-ref
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe terrain-engine-ref --terrain-preview-runtime terrain-engine-ref --terrain-camera-preset oblique --terrain-water-surface --output outputs/terrain/terrain-engine-ref/runtime-oblique-water.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe terrain-engine-ref --terrain-preview-runtime terrain-engine-ref --terrain-camera-preset oblique --no-terrain-water-surface --output outputs/terrain/terrain-engine-ref/runtime-oblique-dry.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe terrain-engine-ref --terrain-preview-runtime terrain-engine-ref --terrain-camera-preset surface-low --terrain-water-surface --output outputs/terrain/terrain-engine-ref/runtime-surface-low-water.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color material --output outputs/terrain/current-river-network/river-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color height --output outputs/terrain/current-river-network/river-height-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river --terrain-camera-preset oblique --terrain-preview-color channel --output outputs/terrain/current-river-network/river-channel-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color material --output outputs/terrain/stress-river-network/river-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-river-stress --terrain-camera-preset oblique --terrain-preview-color channel --output outputs/terrain/stress-river-network/river-channel-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --output outputs/terrain/mountain-range-stress/mountain-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset profile --terrain-preview-surface height --output outputs/terrain/mountain-range-stress/mountain-profile.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface post-erosion --terrain-preview-color height --output outputs/terrain/mountain-range-stress/mountain-post-erosion-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/mountain-range-stress/mountain-height-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset surface --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/mountain-range-stress/mountain-surface-height.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 1025 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset surface --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/mountain-range-stress-1025/mountain-surface-height.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe terrain-engine-ref --terrain-camera-preset oblique --terrain-preview-surface height --terrain-preview-color material --output outputs/terrain/terrain-engine-ref/terrain-engine-perspective.png
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-recipe terrain-engine-ref --terrain-camera-preset surface --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/terrain-engine-ref/terrain-engine-surface-height.png
```

Scalar debug PNG exports hand completed RGBA buffers to the shared
`cubey::CaptureQueue`. Multi-view exports use a small encode worker pool and
finish all queued tickets before writing `manifest.json`. This overlaps PNG
encoding with later debug-view rasterization, but terrain generation and
debug-view rasterization are still serial CPU work. `terrain_workbench_preview_legacy` already
uses the host capture path, so its PNG and video artifacts go through the same
queued capture foundation.

Fixed-extent mountain resolution audit:

```sh
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 513 --cell-size 32 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/resolution-mountain-16km/513
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 1025 --cell-size 16 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/resolution-mountain-16km/1025
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_legacy --headless --grid-size 2049 --cell-size 8 --recipe temperate-mountain-range-stress --terrain-debug-view all --terrain-output-dir outputs/terrain/resolution-mountain-16km/2049

/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-cell-size 32 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --output outputs/terrain/resolution-mountain-16km/513/mountain-perspective.png
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 1025 --terrain-cell-size 16 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --output outputs/terrain/resolution-mountain-16km/1025/mountain-perspective.png
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 2049 --terrain-cell-size 8 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface height --output outputs/terrain/resolution-mountain-16km/2049/mountain-perspective.png

/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-cell-size 32 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface post-erosion --terrain-preview-color height --output outputs/terrain/resolution-mountain-16km/513/mountain-post-erosion-perspective.png
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 1025 --terrain-cell-size 16 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface post-erosion --terrain-preview-color height --output outputs/terrain/resolution-mountain-16km/1025/mountain-post-erosion-perspective.png
/usr/bin/time -f 'elapsed=%E maxrss_kb=%M' ./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 2049 --terrain-cell-size 8 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset oblique --terrain-preview-surface post-erosion --terrain-preview-color height --output outputs/terrain/resolution-mountain-16km/2049/mountain-post-erosion-perspective.png

./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 513 --terrain-cell-size 32 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset surface --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/resolution-mountain-16km/513/mountain-surface-height.png --profile-output terrain-res-mountain-16km-513-surface-height
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 1025 --terrain-cell-size 16 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset surface --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/resolution-mountain-16km/1025/mountain-surface-height.png --profile-output terrain-res-mountain-16km-1025-surface-height
./build/dev/projects/terrain_workbench_legacy/terrain_workbench_preview_legacy --headless --width 1280 --height 720 --grid-size 2049 --terrain-cell-size 8 --terrain-recipe temperate-mountain-range-stress --terrain-camera-preset surface --terrain-preview-surface height --terrain-preview-color height --output outputs/terrain/resolution-mountain-16km/2049/mountain-surface-height.png --profile-output terrain-res-mountain-16km-2049-surface-height
```

The fixed-extent audit currently confirms that `1025` improves normal review
readability and that `2049` is useful for artifact hunting, but raw resolution
does not solve the current mountain source-shape problems. The `2049` path took
roughly 7-9 minutes per scalar/preview capture and peaked around `1.8 GB` RSS in
the preview path, so keep it stress-only. Scene-scale detail should eventually
come from tiled or clipmap-style terrain rendering, not from making this
single-patch workbench mesh the default.

Surface-perspective profiling makes the bottleneck explicit:

| Grid | Surface/color | Generate | Mesh build | Host render | Total |
| ---: | --- | ---: | ---: | ---: | ---: |
| `513` | `height`/`height` | `47.56s` | `0.14s` | `0.34s` | `48.06s` |
| `1025` | `height`/`height` | `96.27s` | `0.56s` | `0.36s` | `97.21s` |
| `2049` | `height`/`height` | `412.75s` | `2.24s` | `0.42s` | `415.44s` |
| `513` | `height`/`material` | `47.74s` | `1.94s` | `0.35s` | `50.06s` |
| `1025` | `height`/`material` | `96.89s` | `7.76s` | `0.37s` | `105.03s` |

The generator dominates these captures. Material previews add a secondary mesh
cost because each vertex samples the product fields needed for material color,
but the renderer host path is not the current problem. `surface` and
`surface-low` make near-field review possible; they also show that additional
samples mostly improve edge cleanliness. The foreground still reads smooth and
synthetic because the source/process model lacks local terrain detail and an
LOD/shader-detail path.

## Current Review Outputs

Use `--terrain-debug-view all --terrain-output-dir outputs/terrain/current-river-network`
for the standard review set. The current local review images are generated at
`513x513`, large enough to inspect the field structure rather than just a tiny
thumbnail. `outputs/` is ignored by git, so this directory is a disposable local
review artifact.

The scalar review set includes 59 PNG views plus `manifest.json`:

- `final.png`
- `mountain-relief.png`
- `mountain-process-review.png`
- `height.png`
- `pre-process-height.png`
- `mountain-profile-height.png`
- `mountain-visual-source-height.png`
- `mountain-ridged-chain.png`
- `mountain-detail-weight.png`
- `mountain-morphology-delta.png`
- `mountain-crease-map.png`
- `mountain-ridge-map.png`
- `slope.png`
- `erosion-delta.png`
- `gully-mask.png`
- `crease-proxy.png`
- `thermal-erosion-delta.png`
- `talus-deposition.png`
- `slope-instability.png`
- `post-erosion-height.png`
- `mountain-envelope.png`
- `mountain-peak-anchors.png`
- `mountain-peak-prominence.png`
- `mountain-ridge-skeleton.png`
- `mountain-ridge-influence.png`
- `mountain-ridge-body.png`
- `mountain-valley-floor.png`
- `mountain-valley-incision.png`
- `mountain-range-spine.png`
- `mountain-mass.png`
- `mountain-shoulder.png`
- `mountain-summit-core.png`
- `mountain-support.png`
- `mountain-ridge-hierarchy.png`
- `ridge-support.png`
- `mountain-peak-candidates.png`
- `peak-support.png`
- `mountain-uplift.png`
- `ridge-uplift.png`
- `peak-uplift.png`
- `drainage-potential.png`
- `routing-fill-delta.png`
- `flow-direction.png`
- `flow-accumulation.png`
- `stream-order.png`
- `river-graph-plan.png`
- `river-graph-discharge.png`
- `river-mask.png`
- `river-trunk.png`
- `tributaries.png`
- `sink-mask.png`
- `channel-width.png`
- `channel-incision.png`
- `valley-incision.png`
- `wetness.png`
- `deposition.png`
- `material.png`
- `vegetation.png`

Revision 32 expands the product contract to 64 fields and 59 scalar/debug views.
Revision 33 retunes the mountain stress visual source away from high-frequency
ridged height. Broad mass/support now carries more of the elevation, summit
support is a bounded accent, ridged-chain detail has much lower geometric
weight, and bounded morphology is restricted to a few meters of crease shaping.
The refreshed `outputs/terrain/mountain-range-stress` manifest reports
`generator_revision = 33`, `height_m.span = 2632.857`,
`mountain_visual_source_height_m.span = 2458.848`,
`mountain_profile_height_m.span = 2458.848`,
`mountain_ridged_chain.mean = 0.0922`, and
`mountain_morphology_delta_m.max = 5.459`. The 1025 mountain stress manifest
reports `height_m.span = 2472.466`,
`mountain_visual_source_height_m.span = 2472.466`, and
`mountain_morphology_delta_m.max = 5.134`.

`terrain_workbench_preview_legacy` is a separate renderer-backed consumer for perspective
review. It turns the selected `TerrainRegionProduct` height field into a lit
mesh through the normal Vulkan windowed/headless app path. For the mountain
stress recipe, `mountain-perspective.png` is the primary 3D read for peak,
basin, and valley hierarchy, while `mountain-profile.png` is a lower side view
for checking whether peak height and valley contrast are plausible.
`mountain-surface-height.png` is the near-ground shape diagnostic; keep it in the
normal mountain review bundle because it exposes smooth foreground slopes,
rounded peaks, and missing local detail that oblique views can hide. Revision 33
raises and backs off the `surface` camera preset so the capture is a usable
diagnostic instead of a foreground-occluded close view; use `surface-low` when a
more aggressive near-ground angle is intentionally needed.
`mountain-post-erosion-perspective.png` renders the diagnostic
`post_erosion_height_m` surface with height color so process detail can be
compared against the actual `height_m` product. The current
`outputs/terrain/mountain-range-stress` directory holds 59 scalar/debug PNGs
after the scalar set plus oblique, profile, surface-height, post-erosion, and
height-colored perspective captures are generated. The 1025 mountain stress
directory is scalar-first, with `mountain-surface-height.png` generated when a
higher-resolution surface check is needed. `manifest.json` is not a rendered
view; use it to check the recipe, seed, generator revision, grid size, field
ranges, and content hash for a scalar capture directory.

The optional `temperate-mountain-river-stress` recipe keeps the same source
terrain and routing diagnostics but uses a graph-first visible river source for
review stress testing. Earlier revisions used a stronger basin-grade routing
profile, selected one connected support basin, spaced accepted support
confluences, and painted extra support paths through the same de-gridded channel
pipeline. Revision 12 also
spaces support paths against previously accepted support paths so
`outputs/terrain/stress-river-network` exposes more of the patch to
river-network artifacts without rendering unrelated watershed clusters.
Revision 13 adds a stress-only hierarchy pass so major branches read as a
branching trunk skeleton while smaller attached paths remain tributaries.
Revision 14 rejects near-parallel promoted branches that do not add distinct
visible drainage area, so the stress trunk review is sparser but less clustered
around one corridor. Revision 15 shifts broad stress reach into connected
basin-tree tributaries while keeping `river_trunk` as the continuous mainstem;
this exposes more network coverage without turning side branches into
disconnected trunk fragments. Revision 16 keeps that hierarchy but rejects
rendered trunk promotions that would fragment the trunk and filters the worst
straight support paths before they become visible tributaries.
Revision 17 replaces the stress recipe's visible topology source with a
deterministic graph-first drainage tree, then paints accepted graph paths
through the same smoothing, lateral-offset, relaxation, and width pipeline. It
also exports `river-graph-plan.png` and `river-graph-discharge.png` so the
source topology can be reviewed independently from the rendered river mask.
Revision 18 promotes selected major graph tributaries into `river_trunk` while
leaving smaller graph paths in `tributaries`.

The optional `temperate-mountain-range-stress` recipe uses the same product
contract to review mountain shape independently from the river stress graph. It
adds broad mountain mass from `mountain-support.png`, sharper ridge structure
from `ridge-support.png`, localized summit accents from `peak-support.png`, and
routes over a softened combination of those uplifts without including fine
detail. The intended first-pass visual review is `mountain-relief.png`, which
hillshades height and tints the mountain source fields without river, wetness,
vegetation, or material overlays. `final.png` is still useful as a product
composition check, but it is not the right image for judging mountain form.
Revision 20 makes the source hierarchy inspectable: `mountain-range-spine.png`
shows broad range organization, `mountain-ridge-hierarchy.png` shows ranked
primary/secondary ridge structure, and `mountain-peak-candidates.png` shows
sparse summit candidates before they become peak support.
Revision 21 changes the primary review order for this recipe. Inspect
`mountain-relief.png` first, then compare `mountain-envelope.png`,
`mountain-peak-anchors.png`, `mountain-peak-prominence.png`,
`mountain-ridge-skeleton.png`, and `mountain-ridge-influence.png`. The raw
skeleton view is a source diagnostic; the relief view intentionally does not
draw raw graph strokes over the surface.
Revision 22 keeps `mountain-relief.png` as the primary visual review image, but
it now uses a clearer elevation ramp and softer hillshade. For hierarchy review,
inspect `mountain-relief.png`, `height.png`, `mountain-envelope.png`,
`mountain-ridge-influence.png`, `mountain-peak-prominence.png`, and
`peak-uplift.png` together. For height readability, inspect
`mountain-perspective.png` and `mountain-profile.png` after the scalar views;
these are renderer-backed mesh captures, not CPU software perspective exports.
Revision 24 adds diagnostic erosion-shaped fields for the same recipe. Inspect
`erosion-delta.png`, `gully-mask.png`, `crease-proxy.png`, and
`post-erosion-height.png` next to `height.png` and `mountain-perspective.png`.
Revision 25 adds `mountain-mass.png`, `mountain-shoulder.png`, and
`mountain-summit-core.png`. Inspect those before judging summit detail: the
intended read is broad highland mass, shoulder buildup, then sparse summit core.
Revision 26 adds `mountain-profile-height.png` and makes that field the
coherent source height for the mountain stress recipe. Inspect
`mountain-profile-height.png`, `pre-process-height.png`, and
`mountain-perspective.png` together: pre-process height should differ from the
profile mostly by bounded detail, not by independent ridge/peak uplift stacks.
Revision 27 adds `mountain-saddle-gate.png`, curves the ridge source, elongates
summit support, and uses saddle suppression in the coherent profile solve. The
regenerated 513 and 1025 mountain review sets report generator revision 27, 52
fields, and 46 scalar views. The 513 manifest reports
`height_m.span = 1598.938`, `mountain_profile_height_m.span = 1505.036`,
`mountain_saddle_gate.mean = 0.2554`, `mountain_ridge_influence.mean = 0.0659`,
and `mountain_summit_core.mean = 0.0256`.
Revision 28 broadens ridge bodies, softens the saturated mountain mass,
shoulder, and saddle fields, and reduces direct crest/summit height so broad
support carries more of the visible mountain profile. The regenerated 513
mountain manifest reports generator revision 28, 52 fields, 46 scalar views,
`height_m.span = 1548.804`, `mountain_profile_height_m.span = 1443.501`,
`mountain_ridge_influence.mean = 0.1600`,
`mountain_ridge_skeleton.mean = 0.0154`, and
`mountain_summit_core.mean = 0.0270`. This is still a diagnostic profile: the
perspective captures are less fin-like and less shelfy, but the mountain range
still needs a better process model for natural peaks, shoulders, and ridge
evolution.
Revision 29 keeps `height_m` unchanged and layers a bounded thermal talus
diagnostic into `post_erosion_height_m`. Inspect
`thermal-erosion-delta.png`, `talus-deposition.png`, and
`slope-instability.png` beside `mountain-perspective.png` and
`mountain-post-erosion-perspective.png`. The regenerated 513 mountain manifest
reports generator revision 29, 55 fields, 49 scalar views,
`height_m.span = 1548.804`, `post_erosion_height_m.span = 1540.493`,
`thermal_erosion_delta_m.max = 56.159`,
`talus_deposition_m.max = 63.752`, and
`slope_instability.mean = 0.0566`. The 1025 manifest reports generator revision
29, 55 fields, 49 scalar views, `height_m.span = 1572.752`,
`post_erosion_height_m.span = 1567.492`,
`thermal_erosion_delta_m.max = 53.625`,
`talus_deposition_m.max = 58.847`, and
`slope_instability.mean = 0.0283`. The perspective comparison is smoother and
less synthetically sharp, but the diagnostic masks still expose localized
straight or stepped source artifacts.
Revision 30 adds the compact `mountain-process-review.png` comparison and
retunes the mountain stress profile toward broader mass and shoulder buildup.
The regenerated 513 mountain manifest reports generator revision 30, 55 fields,
50 scalar/review views, `height_m.span = 1695.575`,
`mountain_profile_height_m.span = 1562.146`,
`mountain_mass.mean = 0.4465`, `mountain_shoulder.mean = 0.3861`,
`mountain_summit_core.mean = 0.0356`, `post_erosion_height_m.span = 1693.804`,
`thermal_erosion_delta_m.max = 68.054`,
`talus_deposition_m.max = 77.452`, and
`slope_instability.mean = 0.0643`. The 1025 manifest reports generator revision
30, 55 fields, 50 scalar/review views, `height_m.span = 1623.204`,
`mountain_profile_height_m.span = 1607.322`,
`mountain_mass.mean = 0.4633`, `mountain_shoulder.mean = 0.4104`,
`mountain_summit_core.mean = 0.0436`,
`post_erosion_height_m.span = 1622.455`,
`thermal_erosion_delta_m.max = 64.265`,
`talus_deposition_m.max = 71.355`, and
`slope_instability.mean = 0.0518`. The visual read is more cohesive, but still
not final mountain quality: rounded high areas and source-tied ridge/process
bands remain visible.
Revision 31 adds the ridge/valley process fields and expands
`mountain-process-review.png` to a 3x3 source/product/process comparison. The
regenerated 513 mountain manifest reports generator revision 31, 58 fields, 53
scalar/review views, `height_m.span = 1750.187`,
`mountain_profile_height_m.span = 1606.441`, `mountain_ridge_body.mean = 0.1750`,
`mountain_valley_floor.mean = 0.1976`, `mountain_valley_incision_m.max = 37.287`,
and `post_erosion_height_m.span = 1748.110`. The perspective read is broader
and less fin-like, but rounded high areas and source-shaped ridge/valley bands
remain visible in the scalar diagnostics.
Both stress recipes are diagnostic recipes, not the default product target.

The active river fields come from a coherent low-frequency drainage potential
plus routed flow accumulation over a padded hidden routing domain. Revision `17`
routes over a priority-flood-repaired drainage surface and exposes the
raw-to-repaired delta as `routing_fill_delta`. Continuous D-Infinity-style flow
angles and fractional accumulation remain the diagnostic catchment path, while
`stream_order` selects connected support for active channels. The default recipe
now avoids raw support-cell painting and keeps a traced trunk plus attached
branch network. Additional branches are accepted only when they visibly reach an
existing active channel, which avoids independent local strokes and straight
snap connectors. Before rasterization, grid-selected paths are resampled into
sub-cell centerlines, smoothed, constrained by the continuous flow field, and
given discharge/stream-order width and strength variation. Revisions 14 through
16 tried to improve the stress recipe by pruning near-parallel support paths,
gating rendered trunk connectivity, and pre-curving support branches. Revision
17 keeps the default path but switches stress river masks to the graph-first
source model, using graph discharge to drive stress channel and valley widths.
Revision 18 adds graph-major-channel promotion to that stress path. Some
tributary joins can still read too angular because this is not yet a
Delaunay/Poisson river graph, hydraulic erosion pass, lake/breach routing model,
or tiled world-coordinate basin graph. See
[Terrain routing repair plan](../../docs/notes/terrain-routing-repair-plan.md).
Revision 23 makes those active river fields terrain-form drivers by carving
broad valley incision and narrower channel incision into `height_m`. Use
`pre-process-height.png` to inspect the source surface, `height.png` to inspect
the carved product, and `channel-incision.png` / `valley-incision.png` plus
`river-channel-perspective.png` to verify where the network actually cuts the
mesh. `river-height-perspective.png` is intentionally color-neutral; if a river
only appears in `river-perspective.png`, the product is back to a texture-overlay
failure.
