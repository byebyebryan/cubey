# Procedural Star Field V2

## Status

This note records the accepted Star Field V2 pass for the shared atmosphere
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

## Accepted Checkpoint

Star Field V2 landed on 2026-07-10 as a deterministic equal-area spherical
field with three bounded populations:

- sparse bright anchors;
- the default naked-eye middle population;
- an additional faint population evaluated only in camera-response mode.

The shader resolves stars in angular space against a shared per-pixel footprint
and samples at most the current spherical cell plus one neighbor per axis. The
longitude seam wraps periodically and pole crossings reflect into the opposite
longitude hemisphere. Galactic density is evaluated once per pixel and remains
a population bias rather than a second visible Milky Way layer. A `2.4` display
gain keeps that subpixel energy readable in SDR captures while the existing star
intensity control remains the user-facing scale.

Celestial content now retains the physical camera ray when the atmosphere
background path repairs horizon rays for scattering. This prevents the horizon
repair from stretching stars radially in orbital views while leaving the
atmosphere classifier and transmittance path unchanged.

The final full-resolution review is under
`outputs/star-field-v2-final-readable/`. It contains the surface response
matrix, yaw sweep, planet surface/orbit checks, and a 240-frame sidereal-motion
capture. Review found no directional seam or lattice, the human/camera
distinction remains controlled, pollution and moon washout suppress the surface
field, and orbital stars remain round.

`projects/atmosphere/profile_star_field.sh` owns the repeatable isolated timing
review. The accepted 1920x1080, 600-frame, three-repeat run is under
`outputs/perf-star-field-v2-final/` and reports stable median GPU totals:

- stars off: `0.188 ms`;
- human response: `0.242 ms` (`+0.055 ms`);
- camera response: `0.269 ms` (`+0.082 ms`).

The median windowed frame time moves from `0.57 ms` with stars disabled to
`0.62 ms` in the default human mode, keeping the default path inside the planned
10 percent regression gate. Camera response is intentionally opt-in and costs
`0.64 ms` total in the same isolated run.
