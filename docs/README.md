# Cubey Docs

Cubey's documentation separates practical setup, current design contracts,
project guides, acceptance evidence, and archived history. Start with the
shortest document that answers the question; the deeper architecture notes are
there when implementation boundaries matter.

## Start here

- [Build and run Cubey](getting-started.md): prerequisites, CMake presets,
  interactive and headless runs, video capture, configuration, and controls.
- [Design](DESIGN.md): purpose, tenets, reference sources, architecture, and
  repository structure.
- [Roadmap](roadmap.md): current readiness, completed foundations, and likely
  next work.
- [Public showcase](https://dev.byebyebryan.com/cubey/): the current visual
  collection, organized by subject and demo.
- [Tracked media and retained packages](media/README.md): current site assets,
  the approved root README media, and immutable archive-package ownership.
- [Presentation site](../site/README.md): authored static showcase, web-media
  attribution, build/review workflow and GitHub Pages publishing.

## Current Design

- [Material and SDF demo roadmap](notes/material-demo-roadmap.md): the dated
  implementation plan and scope boundaries for the mesh/SDF workshops and
  authored-content work. Both native workshops are implemented; current entry
  points are the [PBR guide](../projects/pbr_materials/README.md) and
  [SDF guide](../projects/sdf/README.md), with shared behavior in
  [material recipes](architecture/material-recipes.md) and the
  [PBR workshop plan](notes/pbr-material-demo-plan.md). The
  [SDF construction chapter](https://dev.byebyebryan.com/cubey/demos/sdf/inside-a-solid/)
  is published as a web story; it does not by itself complete a separate native
  authored-content or Guardian-film package. The dated
  [material presentation evidence](notes/material-presentation-evidence.md)
  retains native capture qualification and its original review scope.
- [C++ style guide](cpp-style.md): naming, ownership, formatting, and review
  standards.

## Architecture Notes

Detailed current foundation notes live under
[architecture/](architecture/README.md):

- [Vulkan abstraction map](architecture/vulkan-abstractions.md): reusable
  Vulkan foundation boundaries and planned framework slices.
- [Renderer foundation](architecture/renderer-foundation.md): `cubey::render`
  contracts that sit above Vulkan, including the minimal render graph and the
  boundary between render vocabulary and engine-owned renderer policy.
- [Shader foundation](architecture/shader-foundation.md): GLSL module
  ownership, build-time SPIR-V compilation, and shared include boundaries.
- [Reference-first rendering feature workflow](architecture/rendering-feature-workflow.md):
  start complex visual features from known-good references, then integrate and
  extend once a captured baseline exists.
- [Render graph direction](architecture/render-graph.md): current pass/resource
  graph vocabulary, execution boundary, broad adoption, and triggers for any
  expansion beyond the intentionally small contract.
- [Entity and component foundation](architecture/entity-component-foundation.md):
  manager-oriented entity/component shape, MT-stable storage, read views, edit
  commits, and transform manager direction.
- [Host and engine](architecture/host-engine.md): GLFW/windowed host, headless
  host, input, frame flow, and project lifecycle direction.
- [Configuration V2](architecture/configuration.md): project-owned typed
  configuration and the completed repository-wide legacy-config removal.
- [Threading and async](architecture/threading-and-async.md): CPU jobs, queued
  GPU work, ownership, and future threading boundaries.
- [Fluid simulation direction](architecture/fluid-simulation.md): project
  direction for 2D/2.5D/3D fluid work.
- [Procedural generation foundation](architecture/procedural-generation.md):
  shared source field, operator, and process-driver direction for procedural
  assets.
- [Ocean rendering](architecture/ocean-rendering.md):
  active local-ocean runtime, spectral wave/foam fields, clipmap LOD,
  shared environment lighting, and terrain/planet boundaries.
- [Ocean horizon and curved-local scale](architecture/ocean-horizon-and-planet-scale.md):
  ocean/planet scale boundary, curved far-surface mapping, and local-frame
  handoff direction.
- [Terrain v1 runtime](architecture/terrain-v1.md): active external-heightfield
  far-backdrop product, natural placement, cached geometry, material review,
  and consumer boundary.
- [Ocean adjacent systems](architecture/ocean-adjacent-systems.md): atmosphere,
  terrain, bathymetry, shoreline, and shallow-water integration boundaries.
- [Cloud rendering](architecture/cloud-rendering.md): surface-only Cloud V1
  direction hosted by atmosphere, plus deferred aerial/orbit notes from the
  legacy, TerrainEngine, and Godot-v2 reference passes.
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

This section indexes native projects and retained studies with a local README.
The [public showcase](https://dev.byebyebryan.com/cubey/demos/) presents selected
visual subjects; these guides retain source ownership and technical
instructions. The native Fractal 2D project remains available here, while its
public-site presentation is deferred. Terrain Water's recorded SynxFlow route
remains separate from builtin [Fluid 2.5D](../projects/fluid/fluid_25d/README.md).

Active projects:

- [Atmosphere](../projects/atmosphere/README.md)
- [Terrain](../projects/terrain/README.md)
- [Smoke 2D](../projects/fluid/smoke_2d/README.md)
- [Water 2D](../projects/fluid/water_2d/README.md)
- [Water 3D](../projects/fluid/water_3d/README.md)
- [Fluid 2.5D builtin shallow water](../projects/fluid/fluid_25d/README.md)
- [Terrain Water](../projects/terrain_water/README.md)
  — [current behavior](../projects/terrain_water/current_reference.md) and
  [decision/evidence map](../studies/fluid_25d/notes/development_map.md).
- [Fire 3D](../projects/fluid/fire_3d/README.md)
- [Explosion 3D](../projects/fluid/explosion_3d/README.md)
- [glTF Viewer](../projects/gltf_viewer/README.md)
- [Ocean](../projects/ocean/README.md)
- [Fractal 2D](../projects/fractal_2d/README.md)
- [PBR materials](../projects/pbr_materials/README.md): shared mesh material
  gallery and parameter workshop, with a matched SDF workshop.
- [SDF](../projects/sdf/README.md): native procedural and authored scenes,
  saved construction history, the settled Guardian and shared material
  workshop. Older sculpture studies and technical chronology stay in its
  [technical reference](../projects/sdf/docs/technical-reference.md).

Internal validation:

- [PBR furnace](../projects/pbr_furnace/README.md): white-furnace validation, outside the public gallery.

Opt-in reference studies:

- [TerrainEngine cloud reference](../studies/cloud/terrainengine/README.md)
- [Terrain Ref](../studies/terrain/reference/README.md)
- [Terrain ShaderToy Study](../studies/terrain/shadertoy/README.md)

Paused labs:

- [Planet orbital-rendering study](../studies/planet/README.md), with its
  [retained design](../studies/planet/design.md) and explicit opt-in build.
- [Terrain Hydrology Lab](../studies/terrain/hydrology/README.md)

Legacy material:

- [Retained study index](../studies/README.md): dated Fluid/Terrain/glTF harnesses,
  notes and reproduction tools, separated from supported application entry points.
- [Archived terrain attempts](archive/terrain/legacy-attempts.md)
- [Retired project snapshot](archive/retired-projects.md)
- [Terrain-ocean field contract](archive/terrain/terrain-ocean-field-contract.md)
- [Rejected terrain climate response V1.1](archive/terrain/climate-response-v1-1-rejected.md)

## Notes

- [Notes index](notes/README.md): operating guides, integration checkpoints,
  historical research and superseded plans.
- [Performance profiling](notes/performance-profiling.md): repeatable host/GPU
  profiling workflow and the cloud-vs-atmosphere comparison harness.
- [Working notes](notes/working-notes.md): scratchpad for progress, gotchas,
  and context that has not been promoted into current design docs. Treat it as
  useful context, not current authority.

## Evidence

- [Evidence index](evidence/README.md): compact tracked acceptance artifacts
  for promoted foundation milestones.

## Archive

- [Archive index](archive/README.md): completed foundation phases, retired
  applications and retained rejection records.
- [Original README showcase storyboard](showcase.md): historical shot cards
  and capture contracts, superseded by the presentation site for public structure.
- [Original README media package](media/showcase/README.md): retained player
  mappings, committed MP4/poster fallbacks, hashes, recipes, and licensing.
- [Archived terrain reboot direction](architecture/terrain-reboot.md): earlier
  product strategy retained at its original URL; current behavior is in Terrain V1.

- [Spike findings](archive/spike-findings.md): historical WebGPU/Vulkan spike
  notes and decision record. This is archived context, not current direction.
