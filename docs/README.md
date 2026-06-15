# Cubey Docs

This directory separates current design guidance from living notes and archived
history. Keep the top-level docs focused on the current architecture and
direction; move stale investigation notes, superseded decisions, and temporary
scratch material out of the main path.

## Current Design

- [Design](DESIGN.md): project purpose, tenets, reference sources, architecture,
  and repository structure.
- [Roadmap](roadmap.md): current implementation phases and next work.
- [C++ style guide](cpp-style.md): naming, ownership, formatting, and review
  standards.

## Architecture Notes

Detailed current foundation notes live under
[architecture/](architecture/README.md):

- [Vulkan abstraction map](architecture/vulkan-abstractions.md): reusable
  Vulkan foundation boundaries and planned framework slices.
- [Renderer foundation](architecture/renderer-foundation.md): `cubey::render`
  contracts that sit above Vulkan without introducing scene, material, or
  render-graph policy.
- [Render graph direction](architecture/render-graph.md): current and future
  pass/resource graph vocabulary, execution boundary, adoption triggers, and
  deferred complexity.
- [Entity and component foundation](architecture/entity-component-foundation.md):
  manager-oriented entity/component shape, MT-stable storage, read views, edit
  commits, and transform manager direction.
- [Host and engine](architecture/host-engine.md): GLFW/windowed host, headless
  host, input, frame flow, and project lifecycle direction.
- [Threading and async](architecture/threading-and-async.md): CPU jobs, queued
  GPU work, ownership, and future threading boundaries.
- [Fluid simulation direction](architecture/fluid-simulation.md): project
  direction for 2D/2.5D/3D fluid work.
- [Ocean rendering](architecture/ocean-rendering.md):
  active/reference/legacy ocean split, reference-derived wave
  core, feature donor boundaries, and breaking-wave tradeoffs.
- [Ocean horizon and curved-local scale](architecture/ocean-horizon-and-planet-scale.md):
  ocean/planet scale boundary, curved far-surface mapping, and local-frame
  handoff direction.
- [Planet rendering](architecture/planet-rendering.md): planet-scale frame, LOD,
  terrain-field, sky/celestial, and ocean handoff direction.
- [Ocean adjacent systems](architecture/ocean-adjacent-systems.md): atmosphere,
  terrain, bathymetry, shoreline, and shallow-water integration boundaries.
- [Cloud rendering](architecture/cloud-rendering.md): production cloud renderer
  direction from the legacy, TerrainEngine, and Godot-v2 reference passes.
- [glTF assets and PBR](architecture/gltf-assets.md): glTF import, PBR material
  contract, animation/deformation, texture upload, HDR environments, and viewer
  boundaries.
- [Animation and deformation](architecture/animation-deformation.md): glTF
  animation, morph targets, skinning, GPU deformation, and validation asset
  direction.
- [PBR and IBL direction](architecture/pbr-ibl.md): generated and HDR-backed
  cubemap IBL, PBR shader contract, and environment asset boundaries.

## Project Docs

Project-specific design stays beside the project:

- [Atmosphere](../projects/atmosphere/README.md)
- [Cloud Ref](../projects/cloud_ref/README.md)
- [Cloud Ref 2](../projects/cloud_ref_2/README.md)
- [Clouds Legacy](../projects/clouds_legacy/README.md)
- [Smoke 2D](../projects/fluid/smoke_2d/README.md)
- [Water 2D](../projects/fluid/water_2d/README.md)
- [Water 3D](../projects/fluid/water_3d/README.md)
- [Fluid 2.5D](../projects/fluid_25d/README.md)
- [Fire 3D](../projects/fluid/fire_3d/README.md)
- [Explosion 3D](../projects/fluid/explosion_3d/README.md)
- [Ocean](../projects/ocean/README.md)
- [Planet](../projects/planet/README.md)
- [Procedural Terrain](../projects/procedural_terrain/README.md)

## Notes

- [Notes index](notes/README.md): current living notes, research records, and
  promoted implementation checkpoints.
- [Working notes](notes/working-notes.md): scratchpad for progress, gotchas,
  and context that has not been promoted into current design docs. Treat it as
  useful context, not current authority.

## Archive

- [Spike findings](archive/spike-findings.md): historical WebGPU/Vulkan spike
  notes and decision record. This is archived context, not current direction.
