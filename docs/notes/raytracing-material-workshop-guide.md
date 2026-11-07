# Shared material workshop

The workshop consumes Cubey's twelve canonical recipes under the shared clear
atmosphere. It provides groundless isolated specimens and the existing four by
three sphere gallery. Forward PBR, SDF tracing and hardware RT share material
content, layout, sun/sky and HDR display conventions. Their optical approximations
differ; identical recipe identity does not imply identical glass images.

From the repository root:

```sh
rtk proxy build/dev/projects/raytracing/raytracing --scene materials --material bronze
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/material-workshop/gallery-clear.json
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/material-workshop/gallery-scene.json
```

The gallery presets select the opt-in dense sphere mesh. The ordinary workshop
defaults retain the original 64/128 sphere. `--sphere-density dense` selects
128/256 segments in both forward and RT. Glass uses geometric boundary normals;
denser geometry reduces visible refraction faceting without changing materials.
SDF comparison presets use analytic source SDF sphere tracing, with no mesh scan
or cached voxel field. Existing SDF, forward and RT defaults remain available.

## Compare the consumers

```sh
rtk proxy build/dev/projects/pbr_materials/pbr_materials --config projects/pbr_materials/presets/rt-comparison/gallery-clear.json
rtk proxy build/dev/projects/sdf/sdf --config projects/sdf/presets/rt-comparison/gallery-clear.json
```

The [common manifest](../../projects/raytracing/presets/material-workshop/manifest.json) records canonical
recipe SHA256s, source/effective domains, camera conversion, geometry, complete
environment settings and film timing. SDF domains compensate for instance
translation so all consumers evaluate corresponding local material points.
The generator is `tools/author_material_comparison.py`; `--check` detects stale
inputs. It writes only the new comparison presets/scenes.

The environment route disables local specimen shadows. Opaque materials use the
shared direct sun, diffuse SH, prefiltered environment/DFG response and sheen
approximation. Glass traces its own boundaries and then resolves the environment.
The scene route traces GGX specular chains and neighboring bodies with local
visibility. It retains shared diffuse lighting but launches no diffuse GI rays.
Cached and traced specular contributions are not added together.

The forward comparison presets explicitly select `--transmission-source environment`:
glass resolves the shared probe along its estimated sphere exit ray. Ordinary
forward/glTF consumers retain `screen`, the projected camera-image lookup with
an environment fallback at viewport edges. Neither mode traces neighboring
objects. Forward glass estimates thickness and uses filtered environment optics. SDF
measures central-path segments with its existing filtered optical model. RT uses
geometric interfaces, sharp V1 glass and explicitly rough V2 transmission.
Procedural footprint conservatism also differs; compare component evidence and
local coordinates before attributing a whole-image difference to material data.

The [forward transmission research](forward-transmission-research-plan.md) and
[execution record](forward-transmission-progress.md) explain this distinction.
Forward's estimated ray remains sensitive to its shading normal and approximate
shape. The RGBA8 default normal texture tilts the normal by about .318 degrees;
the source-selection change preserves that legacy behavior. Rough probe LODs
remain an approximation of dielectric transmission, not a sampled rough BTDF.

Forward's **Transmission source** control changes this frame setting without
editing recipes. **Save view** and `--view-output path.json` retain it with the
camera and frozen presentation time; reload using `--config`. Recipe changes
still need a separate recipe export. A view does not serialize all edited
materials or a family-filtered subset of the gallery.

For component qualification only, forward `--hdr-output file.f32 --headless --frames 1`
exports tightly packed little-endian RGBA32F before the display pass. This opts
the diagnostic scene and refraction pyramid into full precision; ordinary runs
retain RGBA16F. `--debug-view transmission-ray` stores outgoing direction xyz
and Beer path length w, `transmission-exit` stores estimated exit xyz and validity
w, and `transmission-radiance` isolates the selected source at mip zero. These
raw signed diagnostics are for numerical inspection, not final-color PNGs.

## Native operation and recipes

Family/material selection, the Gallery checkbox and the sphere/block/swatch/
bored-block selector reuse prepared geometry. Pattern/bump switches and recipe
controls retain the shared meanings. **Reset recipe**, **Save recipe**, **Load
recipe** and **Reload catalog** use the shared asset validator. Rejected input
retains the last working snapshot. Catalog roster/routing changes require restart.
Glass requires homogeneous full dielectric transmission; partial transmission,
metallic glass, textured/patterned glass and arbitrary graphs are unsupported.

Drag to orbit, scroll to zoom and press R for the loaded home view. Camera,
material, lighting/probe, source and transport changes reset linear HDR history.
Exposure changes preserve it. The panel shows sample count and history epoch.
An appearance edit does not rebuild BLAS/TLAS. Resize retires the old target and
history generation before destruction. Five BLAS and five TLAS routes are
prepared once, including isolated and gallery routes.

`--recipe-input path.json` and `--recipe-output path.json` apply to an isolated
slot. A live edited recipe is saved separately from **Save view**. To replay both,
load the saved view and pass `--recipe-input` with the exported recipe. A saved
gallery view clears single-slot recipe paths/roughness overrides. Save view
freezes the resolved camera/light state and clears animation, probes and diagnostic output paths;
it does not serialize all live edited materials into a scene snapshot.

## Sampling and limits

`--samples 256` is a primary/history iteration target, bounded to 1..8192.
`--seed` reproduces pixel/sample sequences. Native/render-only operation adds
one primary sample per iteration. PNG/video capture completes the requested
target for each frozen output state. Ray/normal decisions use linear values;
shared exposure/ACES conversion occurs once after accumulation.

Rough glass uses ten conditional secondary trials sharing one jittered primary
hit, Fresnel splitting at the first two event depths, separate solar sampling
and BSDF continuation. These are included in its measured cost. SPP does not
count the trials as independent camera samples. Every branch/trial shares the
original per-primary limits: eight events, 32 radiance calls and 128 total calls,
including visibility. Raw throughput cutoff is zero. The diagnostic control is
`--max-events 12 --max-path-rays 64 --max-total-rays 256`.

Exhaustion discards residual throughput and reports a status; it does not acquire
fallback sky. This is a bounded, biased estimator. In the accepted P3 control,
neutral frosted-glass furnace mean is about .867 and default versus the deeper
control differs by normalized mean .0148/p95 .0606. Same-budget 256-SPP convergence
is a separate test. More samples cannot remove event/ray truncation or mesh error.
Furnace loss includes the single-scattering rough-interface model as well as
bounded truncation; the deeper-budget comparison measures the latter separately.

Sequential disjoint closed bodies use their own IOR and Beer attenuation. Nested/
overlapping media and a camera inside conservative glass bounds are rejected.
These bounds can also reject a camera in an empty cavity. Straight sun-shadow
transmission uses bounded Fresnel/Beer visibility without bent-shadow caustics.
Diffuse GI, dispersion, caustics, emissive diffuse lighting, clouds, animated
geometry, denoising and history reprojection remain outside this milestone.

## Clear films and evidence

```sh
rtk proxy build/dev/projects/raytracing/raytracing --config projects/raytracing/presets/material-workshop/gallery-dawn.json --headless --capture video --frames 240 --fps 30 --width 1920 --height 1080 --output outputs/raytracing/workshop-dawn.mp4
```

Orbit, dawn and dusk presets exist for all three consumers. The eight-second
authored timeline uses output progress, independent of secondary sample count.
Orbit moves twenty degrees linearly; dawn/dusk use the shared smooth elevation
transition and atmosphere auto exposure. Probe cadence is 30 Hz during cycles,
4 Hz when frozen. `--presentation-time 0..8` freezes RT/forward and `--time 0..8`
freezes SDF. Movies accumulate each output state before advancing time. Their
offline convergence cost does not establish real-time converged motion.

Use the qualification tools serially, without another owned GPU workload:

```sh
rtk proxy uv run projects/raytracing/tools/qualify_material_gallery.py --mode quality --output outputs/raytracing/gallery-quality
rtk proxy uv run projects/raytracing/tools/qualify_material_gallery.py --mode cost --output outputs/raytracing/gallery-cost
rtk proxy uv run projects/raytracing/tools/qualify_material_gallery.py --preset projects/raytracing/presets/material-workshop/gallery-scene.json --mode cost --output outputs/raytracing/scene-gallery-cost
rtk proxy python projects/raytracing/tools/qualify_material_presentation.py --stage films --output outputs/raytracing/workshop-films
```

Additional tools qualify recipe records, GGX/dielectric arithmetic and independent
quadrature, two-body Snell/Beer controls, glass energy/budget/density/lifecycle,
native operations and isolated-material costs. The [progress record](raytracing-material-workshop-progress.md)
links source-pinned receipts. Each capture retains commands, executable/shaders,
recipes, validation logs and GPU observations. Raw diagnostics are separate from
timings. Memory requests, environment allocation sizes and the process-local
`VK_EXT_memory_budget` shutdown estimate have distinct scopes; the latter is not
a physical peak. Host targets, staging and driver internals are not inferred from
requested project bytes. Existing public material media remain unchanged.
