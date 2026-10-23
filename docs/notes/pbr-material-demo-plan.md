# PBR material demo plan

Date: 2026-10-23

Status: implementation of Stages A and B authorized on 2026-10-23. Plans committed
as `b35d1a89`; local milestone commits and native qualification are in progress.
Stage C remains deferred. No push is included in this loop.

## Goal and V1 result

Build a PBR material showcase and workshop using ordinary mesh geometry. A
curated gallery should communicate the material range immediately; selecting
one specimen should make its parameters and response easy to inspect and edit.

The foundation deliverable is reusable material content: standalone recipes
for the accepted looks, shared procedural evaluation, and a small renderer
adapter. The existing renderer, environment runtime and capture infrastructure
remain the rendering foundation.

Proposed working name: `projects/pbr_materials`, executable `pbr_materials`.
Keep the furnace's conformance fixtures and the glTF viewer's asset workflow
intact. The new project must run without importing SDF geometry or depending on
files under `outputs/`, optional asset downloads or the SDF Python generators.

## Current implementation and actual gaps

| Existing source | Reuse or required change |
| --- | --- |
| [PBR material factors and definitions](../../include/cubey/render/pbr.h) | Already describe surface factors, IOR, transmission and volume attenuation. Reuse supported factors; named material content is the missing layer. |
| [Forward PBR renderer](../../include/cubey/engine/forward_pbr_renderer_3d.h) | Already owns mesh shading, shadows, transmission, atmosphere/cloud composition and HDR post. Consume it through its normal request API. |
| [Material resource table](../../include/cubey/render/pbr_material_resources.h) | Records are immutable and slots are retained until table clear. Live editing needs an explicit bounded publication/update route. |
| [Primitive mesh generation](../../include/cubey/render/primitive_mesh.h) | Reuse sphere generation. A small beveled specimen may remain project-owned until another consumer needs its builder. |
| [SDF material catalog author](../../projects/sdf/tools/author_material_study.py) | Supplies the seven accepted material values; import their resolved values into standalone recipes. Avoid a permanent PBR dependency on this project script. |
| [SDF CPU material evaluator](../../projects/sdf/sdf_material.cpp) and [GPU evaluator](../../projects/sdf/shaders/surface_pattern.glsl) | Reference implementation for limestone, marble, grain, analytic gradients and filtering. Promote behavior before changing formulas. |
| [SDF optics math](../../projects/sdf/shaders/optics_math.glsl) | Rough-diffuse support is currently local. A nonzero shared diffuse-roughness property needs an opt-in mesh implementation, not just a JSON field. |

Environment services already exist. Reuse their canonical configuration,
readiness, probe blending and foreground composition. The recent SDF cloud
quality findings belong to that integration; do not start another cloud renderer.

## Material recipes and catalog

Propose a versioned `cubey.material.recipe` JSON format with a stable recipe ID,
label, constant surface/optical properties, procedural kind and parameters,
seed, and material-space transform. The recipe is independent of geometry,
camera, lighting, exposure and GPU handles. A library entry is a recipe plus
retained defaults, not another shader program for each named look.

The V1 library lives in a shared data location, provisionally `assets/materials/`.
Project scene presets select a recipe path/ID and explicit overrides. Export
writes the resolved, validated recipe; saved capture settings record its
fingerprint and overrides so later catalog edits cannot silently redefine a
recorded comparison. Keep recipe-content identity separate from renderer/scene
identity. Legacy SDF appearance hashes keep their existing format and meaning.

| Recipe | Retained content | Useful workshop parameters |
| --- | --- | --- |
| Neutral gray | Existing neutral reference | Base color, roughness, metallic for controlled sweeps. |
| Limestone | Accepted pale stone/grain look | Pattern scale, grain, color/roughness variation and bump height. |
| Balanced marble | Accepted pale/cream palette, Balanced veins, subtle 0.08 body variation | Scale, vein width/frequency, warp and restrained color variation. |
| Dry terracotta | Accepted warm color, granular bump, high specular roughness and diffuse roughness 0.6 | Grain, bump, specular roughness and diffuse roughness as separate controls. |
| Ivory ceramic | Accepted ivory dielectric | Base color and roughness; additional coating layers are outside this slice. |
| Satin bronze | Accepted conductor color and roughness | Roughness and base reflectance color. |
| Tinted glass | Accepted IOR 1.5 and absorption parameters | IOR, attenuation color and attenuation distance, within the existing mesh transport route. |

Retain the existing values as the visual starting point. No second marble
realism pass, new stone palette or replacement clay model is included. V1
procedural kinds are constant, limestone, marble and granular. Surface recipes
use the common opaque subset and one homogeneous glass recipe; importing every
glTF extension into this recipe format is deferred.

Use linear RGB for stored shader colors, explicit display/sRGB conversion in
the editor, and finite validated scalar ranges. Distinguish an absent/default
property from existing glTF compatibility sentinels. Reject invalid recipe
versions, unresolved references, nonfinite values and unsupported combinations
before GPU publication. Do not silently drop a procedural or optical property.

## Coordinates and procedural evaluation

Define a geometry-neutral surface evaluator with inputs consisting of the
resolved recipe, material-space position and sampling footprint. Return base
color, roughness, emission and an analytic height gradient; constant optical
properties remain part of the resolved recipe. All randomness is seeded and
independent of camera/time.

The default material domain is object-anchored 3D space with explicit scale,
orientation and offset. It follows object placement without swimming in world
space. Do not normalize it against mesh bounds, current history bounds or UVs.
A project may map several objects into one authored material frame when a
continuous pattern is desired. V1 does not need a world/object projection UI.

Use a radius-0.25 canonical sphere, matching the existing SDF specimen. Treat
workshop world distances as meters and record the material-space conversion.
The SDF adapter retains its existing workspace-to-material mapping; preserve
its numeric samples rather than silently reinterpreting old bump/attenuation
distances. Material coordinates and geometry transport distances are different
inputs and must have explicit conversions.

For meshes, supply the material-space position through the mesh adapter and
derive the footprint from screen-space derivatives. Transform gradients with
the coordinate Jacobian before constructing a shading normal. Verify rotation,
scale and nonuniform-scale behavior. Pattern filtering must suppress
subpixel grain/vein detail rather than expose a camera-dependent noise pattern.
Bump affects shading only; geometric normals, silhouettes, thickness and
shadow geometry retain their meanings.

Promote the existing CPU/GLSL sampling formulas and their golden values. Keep
thin SDF adapters so current material ownership, smooth blend weights and the
one-material shortcut remain unchanged. This extraction must preserve accepted
SDF source values, appearance identities and native images. Replacing SDF scene
serialization with recipe references belongs to Stage B, not this first port.

## Renderer integration and ownership

Shared material vocabulary/evaluation should have no dependency on either demo
or on SDF instructions. Keep Vulkan resource publication in render/engine code,
portable recipe data and evaluation in the appropriate CPU layer, and JSON
loading within the established asset/config boundary. Confirm the exact new
headers/library placement against layer rules in the first implementation
checkpoint; the logical boundary above is the requirement.

The mesh adapter feeds sampled surface values into the shared forward PBR
shading path. Existing factor/image materials select no procedural source and
keep their behavior. V1 recipe materials use constants/procedural channels with
fallback textures; do not invent image/procedural composition precedence or
rewrite glTF texture handling.

Add only the capability needed for these recipes. Diffuse roughness zero must
preserve current Lambert behavior. Nonzero values reuse the accepted bounded
rough-diffuse response; keep its Fresnel treatment and conservative environment
approximation explicit. Reuse common PBR/environment math rather than adding a
second workshop renderer or copying SDF lighting wholesale.

The current material table cannot support slider changes by repeatedly
appending records: erased slots persist until the owning generation is cleared.
Design an opt-in editable publication route with a validated CPU revision and
frame-slot-safe GPU factor/pattern data. CPU draw classification and GPU shading
must observe a coherent material revision. Preserve immutable imported-material
behavior. Lifetime/allocation must be bounded by live materials and frames in
flight, not by the number of edits. Settle the concrete API/storage strategy at
checkpoint 1 before adding controls.

Recipe selection may change render routing; continuous V1 controls stay within
the selected recipe's supported route. Material edits must not recreate meshes,
pipelines, environment resources or reflection probes. Capture configuration
must identify the final published revision, not a UI value still awaiting upload.

## Mesh glass boundary

Use the existing forward renderer's transmission/volume implementation. The
glass recipe owns IOR, surface roughness and absorption properties; the specimen
adapter owns thin/thick selection and the thickness input. Start with the
canonical isolated sphere and explicitly document the thickness estimate used
by the mesh path. Absorption distance is a material parameter; specimen
thickness is geometry data and must not be baked into a universal glass preset.

Existing screen-space/background/environment fallback behavior remains visible
in the evidence. Use no new ray tracer, thickness solver, extra bounces,
caustics, nested media or rough-glass development to improve one sphere. Stage B
may use actual SDF path length with the same absorption recipe; that does not
make the two transport implementations pixel-equivalent.

## Gallery and workshop

The default gallery presents the seven recipes on well-framed spheres with
clear material labels. A selected specimen view offers orbit/zoom, reset,
recipe selection, a short description, a few relevant parameters, reset-to-
preset, save/export and reload. A temporary roughness strip can demonstrate
response without changing the retained recipe. Keep unsupported controls
disabled with a short reason, especially for glass.

Use the shared PBR debug views for base color, roughness, metallic and geometric/
shading normals. Add narrowly scoped pattern and bump toggles so color,
roughness and normal effects can be understood separately. Time/profiling and
resource diagnostics belong in an optional diagnostic view, not the main flow.

The sphere explains roughness and reflection; the beveled block reveals planar
pattern scale and edge-normal behavior. Limit the secondary block presentation
to limestone, marble and terracotta. No imported hero asset or new art model is
required. Keep initial glass inspection isolated so nearby gallery specimens do
not obscure its transport limitations.

Use white vertex color and neutral fallback textures on these specimens so
hidden mesh tint cannot change the recipe. Check mesh normals, tangents,
winding and tessellation before attributing a surface difference to material
math. Set specimen transforms explicitly and retain them in capture settings.

Reuse three environments: fixed studio reference, clear low sun and the accepted
frozen-cloud low sun. Studio is the comparison default; the low-sun environments
provide the scenic showcase. Keep one exposure per environment across materials,
with common-exposure neutral calibration images. Do not compensate a material
problem with per-material lights or exposure.

Use shared sun/SH/probe products coherently, without adding the generated diffuse
cube on top of SH or applying an extra pi normalization. Consume previous/current
probes and actual mip counts. Settle complete environment resources before
headless frame zero through explicit readiness/submission state. Freeze weather,
not resource completion. The studio route must not create atmosphere/cloud work.
Match cloud composition to the chosen visible horizon policy and foreground
depth, retaining physical probe radiance. Mesh shadows use the existing forward
renderer's visibility path; this is not a mapless-shadow migration.

## Implementation checkpoints

1. **Freeze references and settle contracts.** Record accepted recipe values,
   SDF scene/preset/binary/shader identities and native controls. Fix recipe
   fields, units/domain, mesh thickness input, shared ownership and the bounded
   live-update strategy. Capture unmodified mesh PBR/furnace/glTF controls.
   Commit the reviewed plan when implementation is authorized.
2. **Establish the mesh front door.** Add the small project using the shared
   renderer, sphere, fixed environment and existing constant PBR factors.
   Capture an early neutral/bronze gallery and selected specimen. Confirm the
   layout is readable before procedural extraction. Commit a working baseline.
3. **Promote material content.** Add standalone recipes and share existing
   procedural CPU/GLSL evaluation with SDF compatibility adapters. Add the
   opt-in mesh surface sampling and rough-diffuse property. Validate formulas,
   coordinate transforms, gradients/filtering and unselected-route neutrality.
   Commit after mesh procedural specimens and unchanged SDF controls pass.
4. **Add bounded editing and glass.** Implement the agreed safe publication
   path, recipe-specific controls and persistence. Qualify repeated edits and
   render routing. Apply the glass recipe through existing mesh transmission;
   retain its transport/thickness limits. Commit this workshop milestone.
5. **Reuse the presentation environment.** Integrate shared clear sky and
   frozen clouds with coherent lighting, probes, depth and readiness. Reuse
   accepted settings; allow one initial layout/lighting audition and one bounded
   correction. Keep material formulas/defaults fixed. Commit qualified integration.
6. **Deliver the demo.** Capture the bounded matrix and movies, profile fresh
   sequential runs, exercise native controls and publish one comparison report.
   Update current guides and the roadmap with native and human verdicts as
   separate facts. Commit the closeout; push only when separately requested.

## Acceptance and presentation evidence

| Area | Required evidence |
| --- | --- |
| Recipes | All seven entries load independently of SDF, deterministic seeds, strict validation, resolved override identity and save/reload equivalence. |
| Shared evaluation | CPU/GPU sample/gradient agreement at the existing SDF oracle tolerances; fixed-domain rotation/scale tests and footprint minification views. Set fixture-specific bounds before implementation, preserving existing oracle limits. |
| Compatibility | Existing glTF/furnace/constant materials and accepted SDF controls preserve their meanings and captured appearance. Default/no-procedural/no-rough-diffuse branches stay neutral. |
| Live edits | At least 1,000 parameter updates after warmup with bounded material/descriptor/uniform residency, correct final revision, no stale routing and no mesh/probe rebuilds. |
| Glass | The existing mesh route validates on the isolated specimen, with documented thickness input, finite output and labeled fallback behavior. Its success is not an SDF transport qualification. |
| Native lifecycle | Vulkan validation, relevant synchronization checks, resize/recreate, material/environment switches, export/reload and failed recipe publication retaining the last valid state. |
| Environment | Studio skips dynamic resources; frozen sky/cloud probes settle before capture and remain reusable during material/camera edits. Inspect horizon, sun/reflection orientation and foreground composition. |
| Performance | Separate CPU recipe/publication, startup/probe setup and mesh creation from steady full-scene GPU median/p95. Include cloud and transmission passes; do not sum enclosing and inner timestamps. |
| Visual acceptance | Inspect native stills, close-ups and movies. Technical checks and served-byte parity do not decide whether the materials or gallery look good. |

Use 1920x1080, 1x and a fixed warmup/sample policy for cost comparisons on the
current RTX 5070 Ti, with no competing owned GPU work. Provisional V1 target:
whole-scene median at or below 16.7 ms for the retained gallery and selected
specimen. Measure plain versus procedural and studio versus clouds separately.
A miss gets a bounded documented quality choice or limitation; it does not
open an optimization project or permit changing the gate after measurement.

Bound the final report to seven sphere materials x three environments x two
views (42 stills), six secondary block views (three opaque recipes x studio/
clouds), and at most four eight-second selected-specimen orbits (bronze/glass x
studio/clouds). Add one gallery overview and neutral/common-exposure controls.
Include recipe/scene/command/source/binary/shader identities, native validation
and resource/cost receipts. Keep the published bundle below 100 MiB, verify
served files, and distinguish browser/client checks from native/server evidence.

The existing accepted SDF implementation is the reference donor. Reuse its
formulas and the shared renderer first. Any new complex visual technique or
material family requires a separately selected reference-first experiment.
The V1 plan does not include UV image authoring, a node graph, arbitrary shader
plugins, an asset database, terrain/ocean backgrounds, new cloud models,
animation, learned material generation, or SDF sculpting interaction work.
