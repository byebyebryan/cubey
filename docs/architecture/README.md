# Architecture Notes

This directory contains current foundation design notes that are more detailed
than the root design and roadmap.

- [Host and engine](host-engine.md): GLFW/windowed host, headless host, input,
  frame flow, and project lifecycle direction.
- [Entity and component foundation](entity-component-foundation.md):
  manager-oriented entity/component shape, MT-stable storage, read views, edit
  commits, and transform manager direction.
- [Fluid simulation direction](fluid-simulation.md): project direction for
  2D/2.5D/3D fluid work.
- [glTF assets and PBR](gltf-assets.md): static glTF import, PBR material
  contract, texture upload, and viewer boundaries.
- [Render graph direction](render-graph.md): current and future pass/resource
  graph vocabulary, execution boundary, adoption triggers, and deferred
  complexity.
- [Renderer foundation](renderer-foundation.md): `cubey::render` contracts
  that sit above Vulkan.
- [Threading and async](threading-and-async.md): CPU jobs, queued GPU work,
  ownership, and future threading boundaries.
- [Vulkan abstraction map](vulkan-abstractions.md): reusable Vulkan foundation
  boundaries and planned framework slices.
