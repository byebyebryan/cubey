# Sky Visual Baseline Review

This review covers the 1280x720 PNG captures generated under
`outputs/sky-baseline/` after the initial sky validation baseline. The images are
not tracked by git; this note records the actionable observations.

## Findings

- The unified atmosphere path is the right planet sky foundation. Its orbit limb
  and twilight gradients are coherent with the current planet-scale atmosphere
  contract.
- The archived legacy `sky-frame-legacy` captures had a more obvious sun glow
  and more visible stars in dawn surface views, but they read like a space
  backdrop rather than a lower-atmosphere sky.
- Unified and legacy orbit-limb captures are close enough that the legacy path
  does not provide a meaningful runtime fallback for orbit validation.
- The planet day-moon and surface-night captures are not strong moon/night-sky
  validation cases yet. They differ numerically, but they are too similar by eye
  to drive moon or star decisions.
- The red vertical marker in standalone atmosphere captures is the optional
  ground reference geometry, not a planet sky artifact.

## Decision

Use the unified atmosphere path as the only active planet sky path. Remove the
legacy fullscreen `SkyFrame` backend from planet runtime, UI, CLI, tests, and
active docs. Keep explicit celestial body rendering for the moon.

## Follow-Ups

- Tune unified-atmosphere sun disk/glow after the cleanup lands.
- Add better moon and night-sky capture recipes before moon-specific rendering
  work.
- Consider cleaner standalone atmosphere review captures with ground reference
  geometry disabled or documented as a debug overlay.
