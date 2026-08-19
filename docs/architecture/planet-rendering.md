# Planet Rendering Direction

## Product Boundary

`projects/planet` is an orbital-rendering product, not a surface simulation or
planet-scale terrain engine. Its V1 purpose is a convincing, deterministic
Earth-like globe for whole-disk and high-orbit views that exercises Cubey's
shared sky, celestial, procedural-cache, and capture foundations.

The supported camera envelope keeps the disk between 15% and 70% of the
viewport. The standard view is an illuminated disk around 48% of the viewport.
There is no surface or low-orbit mode in V1.

## Active Components

- fixed smooth sphere geometry sampled by world direction, with no adaptive
  surface LOD or terrain displacement;
- deterministic cached cubemap fields for elevation, land, ice, and roughness;
- orbit material with land, ocean Fresnel/specular response, a soft terminator,
  polar snow, and broad roughness response;
- shared sun, moon, stars, and atmosphere background;
- optional lightweight material-local cloud veil for broad orbital structure;
- true-headless capture, field debug views, and reusable host profiling output.

The source uses a clean-room, direction-domain layered-noise construction. The
MIT-licensed `threejs-procedural-planets` repository is the primary visual and
structural reference, while the MIT-licensed `Planet-Generator` repository is
a secondary reference for broad landform layering. ShaderToy studies remain
visual oracles only; their code is not copied.

## Explicit Deferrals

V1 does not include cube-sphere traversal, terrain streaming, local clipmaps,
hydrology, erosion, coastlines, waves, vegetation, atmospheric LUT work,
volumetric orbital clouds, rings, or surface-scale composition. Those concerns
must not be hidden inside the orbital material or cloud shell.

The previous `projects/planet_legacy` experiments are preserved in the archived
planet design notes and Git history. They are comparison evidence, not an
implementation base for this product; the retirement rationale and recovery
anchor are in [`docs/archive/retired-projects.md`](../archive/retired-projects.md).

## Validation Gates

The clear planet must read as land, ocean, ice, atmosphere, and a smooth
terminator before cloud rendering is enabled. The review pack covers lit,
terminator, crescent, and night presets plus field debug views at 1600x900.
The planet surface and cloud passes together target a 1.0 ms p50 GPU budget on
the development workstation; full-frame cost is reported separately. The V1
cloud veil is deliberately not a volumetric cloud system or a final orbital
cloud-shell design.

The source product must be deterministic for a seed, continuous across cubemap
edges, cacheable under the worktree-local procedural cache, and prepared
asynchronously so interactive startup remains responsive. Headless capture
waits for the staged product rather than writing a placeholder frame.
