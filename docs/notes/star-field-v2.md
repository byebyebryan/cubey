# Procedural Star Field V2

## Status

This note defines the bounded Star Field V2 pass for the shared atmosphere
background. The field remains procedural and analytic so every atmosphere
consumer inherits it without a separate render pass or authored sky asset.

The pre-V2 field already models bright, naked-eye, and faint populations,
magnitude falloff, color-temperature variation, Galactic density, sidereal
rotation, surface extinction, moon washout, and a separate space path. Its main
problems are mechanical and diagnostic:

- stars are generated on independently seeded cube faces, so face boundaries
  are not continuous or equal-area;
- faint populations are much smaller than a pixel while their derivative width
  is capped, causing stars to disappear or pop during motion;
- a few large bright anchors dominate captures while the middle population is
  under-resolved;
- human/camera mode affects the Milky Way but not foreground stars;
- combined night-sky captures make the foreground field difficult to inspect.

## V2 Direction

Use a periodic equal-area spherical field based on longitude and normalized
direction Y. Generate a bounded bright, naked-eye, and camera-only faint
population from deterministic spherical cells. Resolve points in angular space
against the pixel footprint and conserve energy when a source becomes subpixel.

Do not add temporal randomization, twinkle, a real catalog, constellations, or a
new GPU resource. Milky Way V2 remains the unresolved Galactic background.

## Review Surface

`projects/atmosphere/capture_star_field_review.sh` owns the focused visual pack.
It isolates stars from airglow and Milky Way, sweeps camera yaw, compares human
and camera response, checks surface pollution and moon washout, and includes
planet surface/orbit integration. `DEEP=1` also records a slow sidereal-motion
video when video capture is available.

The isolated pre-V2 baseline is under `outputs/star-field-v2-baseline/`. At
1920x1080 it confirms that human and camera rows are effectively identical,
most faint stars do not survive reconstruction, and a small number of bright
anchors carry nearly all visible structure. The yaw sweep also changes apparent
density enough that the cube-face distribution cannot be treated as uniform.

Acceptance requires:

- no cube-face seam or directional density step in the yaw sweep;
- a readable middle-brightness population without oversized default stars;
- stable motion at 720p and 1080p without temporal noise;
- pollution and horizon extinction on the surface but not in clear space;
- camera mode revealing additional faint stars instead of only boosting the
  Milky Way;
- no more than a 10 percent 1920x1080 night-frame regression.
