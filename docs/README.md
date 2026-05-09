# Cubey Docs

This directory separates current design guidance from living notes and archived
history. Keep the top-level docs focused on the current architecture and
direction; move stale investigation notes, superseded decisions, and temporary
scratch material out of the main path.

## Current Design

- [Design](DESIGN.md): project purpose, tenets, reference sources, architecture,
  and repository structure.
- [Roadmap](roadmap.md): current implementation phases and next work.
- [Vulkan abstraction map](vulkan-abstractions.md): reusable Vulkan foundation
  boundaries and planned framework slices.
- [App runtime](app-runtime.md): GLFW/windowed host, headless host, input, frame
  flow, and project lifecycle direction.
- [Threading and async](threading-and-async.md): CPU jobs, queued GPU work,
  ownership, and future threading boundaries.
- [Fluid simulation direction](fluid-simulation.md): project direction for
  2D/2.5D/3D fluid work.
- [C++ style guide](cpp-style.md): naming, ownership, formatting, and review
  standards.

## Project Docs

Project-specific design stays beside the project:

- [Fluid 2D](../projects/fluid_2d/README.md)
- [Fluid 2.5D](../projects/fluid_25d/README.md)

## Notes

- [Working notes](notes/working-notes.md): scratchpad for progress, gotchas,
  and context that has not been promoted into current design docs. Treat it as
  useful context, not current authority.

## Archive

- [Spike findings](archive/spike-findings.md): historical WebGPU/Vulkan spike
  notes and decision record. This is archived context, not current direction.
