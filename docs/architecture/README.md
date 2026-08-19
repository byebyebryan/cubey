# Architecture Notes

This directory contains current foundation design notes that are more detailed
than the root design and roadmap.

- [Host and engine](host-engine.md): GLFW/windowed host, headless host, input,
  frame flow, and project lifecycle direction.
- [Configuration V2](configuration.md): project-owned typed configuration,
  schema/host ownership, and the completed clean-break migration.
- [Entity and component foundation](entity-component-foundation.md):
  manager-oriented entity/component shape, MT-stable storage, read views, edit
  commits, and transform manager direction.
- [Fluid simulation direction](fluid-simulation.md): project direction for
  2D/2.5D/3D fluid work.
- [Procedural generation foundation](procedural-generation.md): shared source
  field, operator, and process-driver direction for procedural assets.
- [Ocean rendering](ocean-rendering.md):
  active local-ocean runtime, spectral wave/foam fields, clipmap LOD, shared
  environment lighting, and terrain/planet boundaries.
- [Ocean horizon and curved-local scale](ocean-horizon-and-planet-scale.md):
  horizon-scale local ocean, planet-compatible contracts, curved-local
  rendering, and deferred handoff to a future planet-surface product.
- [Planet rendering](planet-rendering.md): orbital planet product, deterministic
  surface fields, celestial composition, and validation policy.
- [Terrain reboot direction](terrain-reboot.md): local terrain product
  generator strategy, previous terrain lessons, reference takeaways, product
  contract, and first vertical slice.
- [Terrain v1 runtime](terrain-v1.md): active external-heightfield backdrop,
  CPU terrain ownership, cached product contract, consumer boundary, and
  acceptance criteria after closing the reference lane.
- [Cloud rendering](cloud-rendering.md): production cloud direction promoted
  from `projects/cloud_ref` and the retired `projects/clouds_legacy` and
  `projects/cloud_ref_2` lessons; see the retirement archive for provenance.
- [Ocean adjacent systems](ocean-adjacent-systems.md): atmosphere, clouds,
  terrain, bathymetry, shoreline, and shallow-water integration boundaries.
- [glTF assets and PBR](gltf-assets.md): glTF import, PBR material contract,
  texture upload, HDR environments, and viewer boundaries.
- [Animation and deformation](animation-deformation.md): glTF animation,
  morph targets, skinning, GPU deformation, and validation asset direction.
- [PBR and IBL direction](pbr-ibl.md): generated and HDR-backed cubemap IBL,
  PBR shader contract, and future environment asset boundaries.
- [Reference-first rendering feature workflow](rendering-feature-workflow.md):
  default workflow for complex visual features: port a known-good reference,
  capture a baseline, then integrate and extend inside Cubey.
- [Render graph direction](render-graph.md): the broadly adopted pass/resource
  declaration, execution, synchronization, and frame-resource boundary plus
  explicitly deferred scheduler complexity.
- [Renderer foundation](renderer-foundation.md): `cubey::render` contracts
  that sit above Vulkan.
- [Shader foundation](shader-foundation.md): GLSL module ownership, build-time
  SPIR-V compilation, shared include boundaries, and first split targets.
- [Threading and async](threading-and-async.md): CPU jobs, queued GPU work,
  ownership, and future threading boundaries.
- [Vulkan abstraction map](vulkan-abstractions.md): reusable Vulkan foundation
  boundaries and planned framework slices.
