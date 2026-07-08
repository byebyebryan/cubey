# Shader Foundation

Cubey shader code is compiled at build time from GLSL to SPIR-V. Project and
shared shaders should stay readable as source modules while keeping Vulkan
binding layout, descriptor ownership, and render-pass policy explicit at the
project or renderer layer.

## Current Build Flow

`cubey_add_glsl_shaders` in `cmake/CubeyShaders.cmake` is the single shader
compile helper. It finds `glslangValidator`, passes the project and shared
include paths, and emits one `.spv` file per entry shader. `glslangValidator`
is a build-time compiler and validator, not a runtime system, optimizer,
reflection layer, or shader architecture tool.

Entry shaders should list their non-entry includes in `SHADER_DEPENDS` so
CMake rebuilds SPIR-V after helper edits. Shared packages such as
`cubey_forward_pbr_shader_sources` and `cubey_cloud_layer_shader_sources`
should expose the entry shaders for reusable render features; matching
dependency helpers should expose the included `.glsl` files.

## Library Ownership

Shared shader includes live under `shaders/cubey` when they are domain-neutral
or have more than one active consumer:

- `color_space.glsl`: color encoding, tone/display transforms, and color-space
  helpers.
- `procedural/`: shared random, operators, noise, and FastNoiseLite wrappers.
- `lighting.glsl`, `pbr.glsl`, and `environment_lighting.glsl`: reusable light
  and material math for renderer-facing code.
- `atmosphere/`, `sky/`, and `cloud/`: shared environment feature packages
  owned by the atmosphere/cloud foundation.
- `debug.glsl`: false-color and scalar visualization helpers.
- `view.glsl`: screen-space, view-ray, and depth/fade helpers.

Project-local shader helpers should stay beside the entry shader when the code
is domain-specific, binding-specific, or still changing quickly. Good examples
are ocean foam logic, ocean far-field material response, planet surface field
evaluation, and volume diagnostic modes.

## Current Checkpoint

- `shaders/cubey/debug.glsl` and `shaders/cubey/view.glsl` provide the first
  generic debug/view helper includes.
- `cubey_shared_shader_depends`, `cubey_forward_pbr_shader_depends`, and
  `cubey_cloud_layer_shader_depends` keep shared include dependencies visible to
  CMake so helper edits rebuild SPIR-V outputs.
- `projects/ocean/shaders/ocean.frag` is now the entry point for layout,
  constants, cascade sampling, and final composition. Project-local helpers hold
  the bulk of the material logic:
  `ocean_shading.glsl`, `ocean_far_field.glsl`, `ocean_foam.glsl`, and
  `ocean_debug.glsl`.
- `shaders/cubey/atmosphere/atmosphere.frag` is the next active split target.
  It is shared by atmosphere, ocean, planet, water 3D, and forward-PBR users, so
  its helper dependencies need an explicit atmosphere package before extraction.

## Large Shader Inventory

The current oversized shader pressure points are:

- `shaders/cubey/cloud/cloud_march.comp`: active, about 139 KB, largest shader
  pressure point. Defer until the cloud model stabilizes further because it is
  performance-sensitive and still has rendering research in flight.
- `shaders/cubey/atmosphere/atmosphere.frag`: active, about 38 KB, next split
  target. Extract reusable background, night-sky, sun, ground, and debug
  helpers while keeping the entry shader as the binding/layout owner.
- `shaders/cubey/cloud/surface_cloud_march.comp`: active, about 33 KB, future
  cloud surface-view split candidate after the atmosphere split.
- `projects/planet/shaders/planet_surface.frag`: active, about 26 KB, future
  planet surface/rendering split candidate.
- `projects/fluid/sim/water_3d/shaders/water_3d_diagnostics.comp`: active,
  about 24 KB, future volume diagnostics split candidate.
- `projects/ocean/shaders/ocean.frag`: active, already split into project-local
  helpers. Keep the entry shader focused on declarations, cascade sampling, and
  final composition.
- `projects/clouds_legacy` and `projects/cloud_ref_2`: legacy/reference
  snapshots, do not refactor as part of shared shader foundation work.

## Extraction Rules

- Prefer small includes with one clear reason to exist.
- Promote helpers to `shaders/cubey` only after at least two active users need
  the same abstraction, or the helper is clearly generic debug/view/build
  vocabulary.
- Keep project-local includes for domain math that is still being tuned.
- Do not hide Vulkan descriptor sets, bindings, push constants, or image
  formats inside generic shader helpers.
- Do not create a universal ocean/cloud/planet uber shader abstraction.

## Deferred Work

The shader foundation does not currently include runtime hot reload, shader
reflection, generated CPU/GPU ABI headers, SPIR-V optimization passes, or a
shader dependency graph linter. Those are future batches once the shared source
layout is stable.

Near-term shader follow-ups should be cut as separate batches:

- split `atmosphere.frag` into shared background helpers and an entry shader
  that owns bindings, frame constants, and final composition;
- keep `cloud_march.comp` intact until cloud rendering has a clearer stable
  surface/orbit split;
- split `planet_surface.frag` into planet surface field, material, atmosphere
  blend, and debug helpers;
- split `water_3d_diagnostics.comp` into volume sampling, raymarch, and debug
  output helpers;
- promote volume/raymarch helpers only after cloud and fluid share real code,
  not just similar naming;
- add an optional shader dependency audit that verifies each included `.glsl`
  appears in the owning target's `SHADER_DEPENDS`;
- evaluate `spirv-val` or `spirv-opt` only after source layout stabilizes.
