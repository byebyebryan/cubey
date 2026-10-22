# Negative sphere: matched shape comparison

Date: 2026-10-21

[Video and matched native captures](http://starship:8787/reports/597b5db0fb6edef3a52ec569083ae0719b10b39a3104102eb719a0da9c5fadf9/).

The user requested a ball after reviewing the rounded-cube carving demo.
`negative-sphere` starts with an exact radius-0.36 sphere and reuses every
cutter from `negative-sticks`, without changing the seed, camera, material,
light, width bands or carving timeline. `presets/negative-sphere.json` selects
the new scene. The rounded-cube generator/preset remains unchanged.

## Representation

The existing distance formula supports this without another GPU primitive:
`box_distance(p, vec3(0), radius) = length(p) - radius`. CPU validation now
explicitly permits an all-zero box core with positive rounding. Positive box
cores retain their previous contract; mixed-zero/negative cores and an
unrounded zero-size point remain invalid. No epsilon core approximates the
sphere, and no field-builder, tracing or shader code changed.

The existing version-1 recipe encoding retains its explicit parameters and
geometry hash. New sphere files require the updated reader; older readers
reject zero-size cores. Existing box files and GPU lane packing are unchanged.
The generator identifier is `negative-sphere-pcg32-v1`.

## Native evidence

`outputs/sdf/sphere-comparison/manifest.json` records source `63519263`, clean
at capture start, the native binary, nine compiled shaders, commands, applied
configs, recipes, native captures and logs. On RTX 5070 Ti / driver 610.57.04:

- CPU tests pass for sphere distances, arbitrary direction/transform samples,
  persistence, invalid core rejection, and exact cutter equality between shapes.
  The rounded-cube recipe still hashes to `c92ff9ee08a5af98`.
- Both shapes pass native ray validation at 0, 4, 16 and 72 cuts. The intact
  sphere passes 259 rays / 151 hits, including inside-solid origins. The
  carved sphere and reloaded explicit recipe pass 547 rays / 352 hits, with
  zero mismatches, exhausted rays or sampling ambiguities and maximum surface
  residual 0.000119902.
- The final sphere hash is `abc8005ad6ce0210`; 73 edits produce 532,144 active
  bricks. The axis-92 field allocation remains 1,974,880,928 bytes. A cold final
  build was about 51 ms; the cached update path is unchanged.
- Private Xvfb interaction passes resize, orbit, pause/reset, carve scrubbing,
  staged seed editing and explicit sphere regeneration. Prefixes are
  `[73, 1, 73, 39, 1, 73]`; seed 43 changes the hash to `11e06e38daab2797`.
- The sphere video is 720 H.264 frames at 1280x720 / 30 FPS / 24 seconds,
  with 2x internal sampling, hard self-shadows and shared atmosphere sky.

Native intact, carved, normal and decoded-video captures were inspected. The
curved exterior distinguishes the planar cuts more clearly to the agent's eye;
the matched 0/4/16/72-cut comparisons let the user judge that independently.
This is a focused shape trial, not a new full rendering-performance qualification.

The report reuses the seekable-Blob video setup for static HTTP delivery.
An isolated Chrome check over HTTP without Range support passes all six
decoded chapter seeks, playback, all four paired comparison stages, image
decoding, links and 390px layout. The report includes that review receipt;
served-endpoint verification is recorded in `outputs/sdf/sphere-delivery.json`.
