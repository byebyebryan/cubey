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
