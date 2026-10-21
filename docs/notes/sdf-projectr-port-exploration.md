# SDF / projectR port exploration

Date: 2026-10-21

Status: exploration and scope record. The proposed starting point is a bounded
projectR rendering port with procedural content. No Cubey SDF implementation,
native capture, desktop sculpting workflow or visual acceptance is established
by this note. Learned-content experiments are retained below for later selection.
The [implementation plan](sdf-projectr-port-plan.md) develops the bounded port
into proposed decisions, checkpoints and acceptance criteria.

## Purpose

Cubey is a workbench for experimentation. Learning how a representation,
renderer or interaction works is a valid reason to build it, including when
established implementations already exist. A finished visual demonstration is
another important outcome. Technical novelty is not a prerequisite.

Procedural objects can be the intended content of a rendering demo. Starting
with one does not require a hand-sculpted hero asset, a full authoring tool or
a trained content generator. Claims about the quality of agent-authored art
still need an actual visual audition.

The [reference-first workflow](../architecture/rendering-feature-workflow.md)
applies: establish a working visual core before extending or promoting it.

## Source context

The [architecture and source review](projectr-architecture-review.md) traces
the complete pipeline and records batching, persistence and traversal findings
that need resolving before the original serves as a correctness baseline.
The [development history](projectr-development-history.md) retains the original
motivations, experiments and alternatives from full commit messages, including
the earlier sampled/analytic/hybrid brick renderer.

The reference checkout is `/home/bryan/code/projectR`, a Unity 2018 project.
The source review covered:

- `master` at `e2b80796d8f2f9478965a552342bf66fab0e4114`, ending with a
  renderer rollback;
- `develop` at `6a7c5f9a8a6d58ad5d07850b1b625b399385a2f0`, including later
  hierarchical traversal, shadow, procedural-color and inside-volume work.

The later branch is useful rendering reference, not an accepted complete
authoring application. Its controller editing body is largely commented out;
mouse orbit is present. Neither branch was run during this context review.

The existing [stick generator on develop](https://github.com/byebyebryan/projectR/blob/6a7c5f9a8a6d58ad5d07850b1b625b399385a2f0/Assets/Scripts/BrickEngine/BrickEngineTest.cs)
places randomly rotated thin boxes around a spherical shell and adds them in
batches. On `master`, positions use radius 0.39-0.40 and the size vector is
`(0.02, 0.002, 0.002)`. On `develop`, radius is 0.29-0.30 and the size vector
is `(random(0.05, 0.2), 0.001, 0.001)`. These are source parameters in the
bounded workspace, not measured native performance or an approved appearance.

The [edit representation](https://github.com/byebyebryan/projectR/blob/6a7c5f9a8a6d58ad5d07850b1b625b399385a2f0/Assets/Scripts/BrickEngine/Edit.cs)
already separates primitive/operation, transform, dimensions and mirroring
from VR input. Sphere, box, torus, capsule, cylinder and cone primitives give
us further small procedural scenes if a later comparison needs them.

## Review before choosing the port boundary

Recommendation after the source review: preserve the primitive/edit semantics
and procedural recipe, then port a reviewed rendering core. The age of the
reference warrants checking its assumptions; it does not by itself justify
replacing the algorithm. Keep correctness changes distinguishable from later
performance and appearance experiments.

| Area | Proposed treatment |
| --- | --- |
| Geometry evaluation and operations | Start with boxes, source dimensions/rounding, transforms and fill/remove. Additional primitives and mirroring wait for a concrete use. Smooth unions remain a possible later extension, not an established active feature of the source path. |
| Stick generation and construction playback | Preserve the recipe, with explicit deterministic edit data and playback independent of render FPS. |
| Hierarchy, edit pruning and sampled bricks | Use as the starting algorithm. Review traversal and capacity before fixing the native layout; a new sparse architecture is not a prerequisite. |
| Allocations, resource lifetime and synchronization | Adapt to Cubey ownership and an explicit memory budget. Diagnose overflow; do not inherit large fixed allocations without accounting for them. |
| Ray stepping, normals and depth | Review conservative traversal, brick boundaries, inside-volume behavior and scale-dependent tolerances. Export the actual surface-hit depth for scene composition. |
| Unity/VR host, UI and old workspace persistence | Replace with the bounded Cubey host and recipe controls. Keep the canonical edit recipe separate from any derived or incrementally baked field. |

Two concrete source findings motivate this review:

- The [builder](https://github.com/byebyebryan/projectR/blob/6a7c5f9a8a6d58ad5d07850b1b625b399385a2f0/Assets/Scripts/BrickEngine/BrickEngine.cs)
  allocates metadata for 19,173,961 nodes plus double-buffered brick textures,
  and reads append counters back between hierarchy stages. These are allocation
  and synchronization choices to reassess, not measured native bottlenecks.
- The [fragment shader](https://github.com/byebyebryan/projectR/blob/6a7c5f9a8a6d58ad5d07850b1b625b399385a2f0/Assets/Shaders/BrickEngineRenderer.cginc)
  returns `SV_Target` without exporting ray-hit depth. It also uses hard-coded
  normal/trace tolerances and circumscribed-sphere exits for empty hierarchy
  cells. Check that skipping cannot cross an occupied neighboring cell; this
  is a correctness concern from source inspection, not a reproduced artifact.

Use a few small, directly evaluated primitive/CSG fixtures to check the cached
field and traversal. This is a bounded correctness oracle, not a second
permanent rendering product. Establish a native captured baseline before
changing acceleration strategy or adding richer shading. Old Unity captures,
if practical to obtain, would strengthen comparison; source inspection alone
does not establish visual parity with the original.

## Proposed first experiment: procedural SDF rendering port

Start with one reference rendering path and one ball-of-sticks recipe. Use
the later traversal work as reference and the earlier renderer for comparison;
select a captured working baseline before extending it. Supporting both paths
as permanent Cubey products is not a requirement.

The bounded scope is:

1. Select and port the reviewed edit/primitive evaluation, required field
   construction and rendering path into an isolated Cubey project.
2. Make scene generation reproducible from an explicit seed and parameters.
   For cross-engine parity, compare the same explicit edit list: equal seed
   numbers alone do not imply equal Unity and C++ random sequences.
3. Provide orbit/pan/zoom, a small set of generation controls, and construction
   playback with play/pause/reset. Define progress by edit index or explicit
   time so the final object is independent of render FPS.
4. Support deterministic headless captures of the same scene and playback.

Port correctness may require changing old allocation, lifetime or API details.
Broader resource architecture work should follow a demonstrated need from this
scene. The old scene's presence in source does not establish that its renderer
or memory policy is ready to adopt unchanged.

### Visual audition

The stick ball has useful visual structure: fine edges, intersecting solids,
negative space, a dense silhouette and opportunities for self-shadowing.
Try three presentations of the same object:

- A clean, closely framed slow orbit, using a simple material and lighting that
  makes depth and intersections legible.
- A construction sequence that grows into the finished object, then holds it
  long enough to inspect. This reuses the original procedural idea.
- A separate diagnostic view of the existing traversal/brick machinery to
  explain how the surface is represented and rendered.

Match the source-compatible baseline first; evaluate any richer lighting or
material treatment separately. Geometry, shading and motion should each remain
inspectable. A more elaborate authored model is not an acceptance prerequisite.
Preserving geometry semantics does not require retaining the identified
batching and traversal defects from the source review.

Judge the first result on an actual native turntable and construction replay:
stable fine features, correct occlusion, no obvious brick seams or holes,
readable shape and motion, and recorded performance on the selected scene.
Numerical/build checks and human visual review establish different things.

Full desktop sculpting, VR interaction, a general authoring interface, neural
inference and custom training are separate later experiments. A clipped sphere,
perforated shell or repeated primitive bundle could be a small later procedural
variant if it answers a specific rendering question.

## Rendering and lighting experiments

The distance field supplies geometry queries for secondary rays as well as
primary visibility. That makes lighting experiments a natural continuation of
the port. It does not provide reflected radiance or a complete GI solution by
itself. Keep each effect independently switchable and measure the dense stick
scene rather than assuming secondary rays will be cheap.

| Step | Intended experiment and visual purpose |
| --- | --- |
| Reference baseline | Surface normals, direct light and environment lighting, matching the saved mode that bypasses shadows. |
| First lighting check | Review and enable the existing hard-shadow traversal, including its ray origin and termination. Use the checked result as a comparison for soft shadows. |
| First lighting extension | Mapless SDF soft shadows, with controllable source angle/radius. Show both self-shadowing and a shadow on a simple receiver plane. |
| First lighting extension | Local SDF ambient/sky occlusion to make dense intersections readable. Compare scalar visibility with a bent-normal approximation for environment lighting. |
| Later comparison | One-bounce diffuse GI or local reflections in a bounded scene. A progressive stationary view is a useful first target before attempting an interactive denoised solution. |

[Distance-field soft shadows](https://dev.epicgames.com/documentation/en-us/unreal-engine/distance-field-soft-shadows-in-unreal-engine)
can approximate area-light visibility along a ray without rendering a shadow
map for the SDF occluder. [Distance-field ambient occlusion](https://dev.epicgames.com/documentation/en-us/unreal-engine/distance-field-ambient-occlusion-in-unreal-engine)
can account for world-space occluders outside the camera image and modify sky
lighting using visibility and a bent normal. Field resolution and tracing
accuracy still limit both effects, especially on thin sticks.

Treat ambient/sky occlusion separately from bounced illumination. A later GI
experiment needs material and lighting evaluation at secondary hits, a sampling
budget, and possibly accumulation/denoising. A progressive mode must invalidate
history when the camera, geometry, materials or lighting change. The separation
between SDF tracing and radiance evaluation is also visible in
[Lumen's technical description](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine);
its full scene/cache architecture is not a dependency of this experiment.

## Integration with the current foundation

Use a project-local SDF pass on the existing render graph. The current
[forward PBR renderer](../../include/cubey/engine/forward_pbr_renderer_3d.h)
accepts mesh scene resources; it does not expose a generic SDF draw interface.
Reuse the lower-level shared contracts before proposing a new renderer-wide
abstraction:

- GPU resources and field construction follow the existing owner, upload and
  [render-graph](../../include/cubey/render/render_graph_builder.h) contracts.
- [AtmosphereEnvironmentRuntime](../../include/cubey/engine/atmosphere_environment_runtime.h)
  supplies the same primary sun/moon lighting, diffuse environment lighting and
  reflection bindings used elsewhere. Reuse common
  [PBR math](../../shaders/cubey/pbr.glsl) where it fits; SDF material evaluation
  remains local to this project initially.
- [AtmosphereBackgroundFrame](../../include/cubey/render/atmosphere_background_frame.h)
  and [TerrainBackdropRuntime](../../include/cubey/engine/terrain_backdrop_runtime.h)
  can draw the sky and an optional prepared far backdrop. The
  [Pyro 3D graph](../../projects/fluid/sim/pyro_3d/pyro_3d_commands.cpp)
  is an existing custom raymarching consumer of those services, although its
  volume composition is different from an opaque SDF surface.
- Compose in linear scene HDR, with the actual SDF-hit depth and one final
  [HDR/post pass](../../include/cubey/render/hdr_post_frame.h). Define compatible
  camera/depth conventions and the conversion from the unit SDF workspace to
  world units. Atmospheric attenuation, if needed, uses the surface-hit
  distance rather than the proxy-box distance.

The proposed opaque composition is sky/optional terrain into scene color and
depth, then an SDF surface pass that depth-tests and writes its hit depth, then
the shared display transform/capture. If depth must be sampled for ray bounds,
use a separate readable depth resource or explicit composition; do not assume
simultaneous sampling and attachment writes are valid. Optional cloud/volume
composition must consume the resulting depth with explicit ordering.

Shared depth establishes visibility between surfaces. It does not establish
mutual shadows, reflections or GI. In particular:

- Mapless SDF self-shadows only see geometry represented by the queried field.
  A simple receiver plane can query that field to receive the object's shadow.
- Terrain receiving SDF shadows needs an explicit receiver integration; the
  SDF receiving terrain shadows needs terrain shadow-map or heightfield queries.
  The current terrain shadow path remains useful.
- Atmosphere reflection bindings supply environment light; they do not
  automatically include the new object or terrain in local reflected rays.

Recommended first presentation: stick ball and simple ground under shared
atmosphere lighting, with orbit/construction playback and separate hard-shadow,
soft-shadow and occlusion comparisons. Add the prepared terrain background
after foreground depth is proven. GI and cross-renderer shadow exchange remain
separately selectable experiments. This is a proposed integration, not a native
runtime qualification or a reason to reopen unrelated foundation work.

## Retained experiments for later

These are options, not dependencies of the port or a committed implementation
queue. The model research was reviewed on 2026-10-20; no weights were installed
or inference/training qualified in that review. Recheck released artifacts and
requirements before selecting a model.

| Experiment | Learning or demo question | Starting references |
| --- | --- | --- |
| Pretrained image-to-shape/SDF content | Can generated geometry supply a compelling editable base object? Compare neutral multi-view renders, then the native SDF conversion. | [TripoSG](https://github.com/VAST-AI-Research/TripoSG), [Hunyuan3D 2.1](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1), [TRELLIS.2](https://github.com/microsoft/TRELLIS.2) |
| Neural SDF fitted to one existing object | Show a representation learning a surface; inspect approximation error, detail and rendering cost. This represents supplied content rather than inventing new assets. | [instant-ngp SDF example](https://github.com/NVlabs/instant-ngp#sdf-armadillo) |
| Small specialized image-to-SDF training | Learn shape priors for a constrained family using generated shapes, rendered images and SDF supervision; evaluate held-out shapes and views. | [DeepSDF](https://github.com/facebookresearch/DeepSDF) as representation context; a conditional training design still needs selection. |
| Fine-tuning or broader generative training | Explore domain adaptation and data preparation as their own experiment, with measured compute requirements. | [Hunyuan shape training guide](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/hy3dshape/README.md) |
| Desktop sculpting | Compare surface placement with work-plane/gizmo placement, including depth ambiguity and empty-space construction. | [Modeler placement modes](https://helpx.adobe.com/substance-3d-modeler/desktop/create-with-clay/placement-modes.html) |
| Authored sculpture and edit choreography | Test whether procedural parts, scripted strokes, materials and camera work can produce a finished piece; assess agent-authored content through native images. | projectR edit model and the bounded port above |

Generated fields or meshes do not recover the original primitive/edit history.
A later import experiment could preserve a base field and layer new edits over
it. Geometry and material appearance need separate treatment. Single-image
generation also needs review of unseen sides, and neural/mesh-to-SDF conversion
needs a comparison against the supplied geometry.

Existing desktop modelers, procedural SDF toolkits and learned generators are
useful references. Their existence does not decide whether a Cubey experiment
is worthwhile; the learning objective and the resulting demonstration do.
