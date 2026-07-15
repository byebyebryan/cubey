# ShaderToy Mountains Fidelity Study

Date: 2026-07-15

## Decision

The clean-room mountain source comparison did not identify a production source
worth promoting. Its candidates isolated useful operators, but none reproduced
the composition that makes the reference renders convincing. The next step is
therefore a fidelity study rather than another generalized recipe.

Add an optional `projects/terrain_shadertoy_ref` application with two paths:

- an unchanged external `mountains.glsl` raymarch wrapped for Cubey/Vulkan;
- the same external height functions transferred to a dense Cubey mesh.

This creates a direct control for separating terrain source, geometric sampling,
fine normal evaluation, material, atmosphere, and camera composition. It does
not change `projects/terrain`, and `projects/terrain_ref` remains frozen.

## Source And License Boundary

The local reference is expected at `~/code/ref/ShaderToy/mountains.glsl` and
identifies ShaderToy entry <https://www.shadertoy.com/view/4slGD4> under Creative
Commons Attribution-NonCommercial-ShareAlike 3.0 Unported terms. Cubey must not
vendor that source or generated SPIR-V.

The optional target includes the external file at shader-build time. A missing
source disables the target without breaking normal configuration, builds, or
tests. Evidence records the source path, SHA-256, declared license, reference
URL, Cubey commit, and all input substitutions. Deterministic generated textures
stand in for ShaderToy channels; this makes the study reproducible but means
water glint is not a pixel-exact input reproduction.

## Fixed Comparison

The raymarch path calls the reference `mainImage` and preserves its camera path,
terrain intersection, detailed normal field, trees, material, water, fog, sky,
and post-processing. It is the visual control.

The mesh path uses:

- a `2048x2048` floating-point height atlas evaluated from the external source;
- a `1024x1024` cell regular grid with 32-bit indices for the full comparison;
- a 512-reference-unit square domain centered on each fixed reference camera;
- a shader-side detailed-height normal option matching the reference's
  `Terrain2` evaluation;
- source-compatible terrain material, water, fog, sky, and post-processing;
- a small source-evaluation probe for the exact camera-height inputs.

Fixed review times are `0`, `20`, and `40` seconds at `1920x1080`. Time `20`
also carries `256`, `512`, and `1024` cell topology comparisons plus source
surface, normal, and shading ablations. Top-height and slope diagnostics expose
the field without presentation camouflage.

## Acceptance And Stop Point

The direct path must recover the recognizable Mountains composition apart from
documented channel-input differences. The dense mesh must preserve the same
macro silhouette and ridge placement across all three views without holes,
cracks, or visibly clipped peaks. Ablations must make topology, detailed
normals, trees, and full source shading individually legible.

This is an offline reference lane with no frame-time gate. The batch stops after
the comparison pack and written findings. A successful transfer is evidence for
which source and rendering pieces to adapt clean-room later; it is not permission
to move restricted source into production.

## Completed Finding

The v1 evidence pack is under
`outputs/terrain_shadertoy_ref/mountains-fidelity-v1/`. The direct raymarch
recovers the recognizable Mountains scenes at all three fixed times. The
`1024`-cell mesh preserves their macro silhouettes, ridge placement, waterline,
and camera composition without cracks or missing terrain. This is materially
closer than the earlier operator proxies and demonstrates that the source can
be transferred to conventional mesh rendering.

The transfer also explains why the reference works:

- the five-octave `Terrain` field supplies broad, continuous relief, but its top
  view is still a cloudy field rather than a self-evident authored mountain map;
- ray intersection uses the broad field while six additional `Terrain2` octaves
  provide distance-aware normals, separating silhouette frequency from shading
  frequency;
- the camera follows a source-selected path and samples terrain ahead to keep
  useful clearance and compositions instead of asking arbitrary views to work;
- trees, slope/elevation material bands, water, sky, fog, and exposure provide
  scale and hide transitions that are conspicuous in neutral diagnostics.

The topology ladder is the main warning. Even `1024` cells across the
512-reference-unit domain shows obvious facets with geometry normals because
the source camera approaches the terrain closely. Detailed normals conceal much
of that error in the final image, but they do not increase silhouette density.
A production mesh adaptation therefore needs view-dependent geometry density or
an equivalent LOD/tessellation strategy; a single uniform grid is not a
close-surface solution.

Removing procedural tree displacement changes the silhouette much less than
removing detailed normals or source shading. The strongest transferable lesson
is therefore the layered frequency and presentation contract, not the tree
height hack or a claim that one compact noise field solves terrain generally.

This study passes its fidelity goal but does not promote restricted code or a
new production source. The next clean-room terrain work should preserve broad
geometry, independent shader-scale normal detail, source-aware framing, and
distance-hiding atmosphere as explicit layers, then evaluate them through the
cached backdrop and its sub-millisecond runtime budget.
