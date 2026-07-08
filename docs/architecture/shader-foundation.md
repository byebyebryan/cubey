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

## Large Shader Inventory

The current oversized shader pressure points are:

- `projects/ocean/shaders/ocean.frag`: active, about 55 KB, first split target.
- `projects/planet/shaders/planet_surface.frag`: active, about 26 KB, future
  planet surface/rendering split candidate.
- `projects/fluid/sim/water_3d/shaders/water_3d_diagnostics.comp`: active,
  about 24 KB, future volume diagnostics split candidate.
- `projects/cloud_ref/shaders/cloud_ref_march.comp`: reference project, about
  23 KB, keep intact unless the reference role changes.
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
