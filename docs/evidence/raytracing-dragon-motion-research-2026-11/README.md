# Moving-camera reconstruction research

Research-only follow-up to the current native motion recording on 2026-11-16.
See the [research and proposed acceptance plan](../../notes/raytracing-dragon-motion-research.md).

- `sources.json`: inspected primary sources, pinned NRD/RTXPT/NRD-Sample
  revisions, and the limits of what was verified.
- `code-audit.json`: local history/sampling/guide gaps and the recorded motion
  evidence. Source locations describe the inspected build, not an implementation.
- `validation.json`: current renderer identity preservation and documentation
  validation. No SDK build or integration is claimed.

The candidate library's Linux/Vulkan support is documented, not runtime-qualified
in this pass. Layered reconstruction and the dedicated-glass/TAA route remain
alternative designs. Neither a first-hit motion vector nor a still-image
denoiser is presented as a complete solution for the nonconvex glass dragon.
