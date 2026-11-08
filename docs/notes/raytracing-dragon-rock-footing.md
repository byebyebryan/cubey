# Dragon rock footing: native composition audition

Date: 2026-11-07. Candidate based on published `3c1cda12`.

[Native comparison and camera arc](http://starship:8787/reports/4e5308e2709b0ffcb91a001a6e6c417c482d97e0f3b24ad9a3a78d75c4d3b1e6/).
The report bundles direct native images, the preset, commands and evidence.
All 14 published files match their authored bytes over HTTP. Comparison controls,
camera arc, hash reload, contact detail and the 390 px phone layout were checked
in the browser; the shared report server remains running.

The design whiteboard led to a bounded native experiment: one dragon on one
low, irregular rock, surrounded by the shared clear sky. The new
`projects/raytracing/presets/optics/dragon-rock-footing.json` starts in clay.
There is no floor or neighboring primitive. The existing reflective-dragon
preset and primitive optical proof scenes retain their behavior.

The first rock was generated once at scene preparation. Its outline fit the dragon's
lowest 15 mm vertex band, then adds small deterministic chips. An uneven top
approaches that band, and broken side facets taper to a closed underside.
That first support had 504 triangles and spanned approximately 0.805 by 0.516 m,
with 0.106 m total height. It was a flat-shaded shape study with a uniform rough
matte material. The subsequent user review found its faceting too polygonal.
The [flat marble-base follow-up](raytracing-dragon-marble.md) records the current
slab geometry and the shared marble recipe on the support.

`--dragon-support plinth|rock|none` is a startup selection for the solo scene.
It round-trips through saved configuration. Clay/mirror selection still uses
the existing native controls without rebuilding geometry. The optional mirror
capture exercises existing sharp reflection transport; it does not introduce
the material workshop's rough metal sampling onto the dragon.

## Evidence and review boundary

Ignored artifacts live in `outputs/raytracing/dragon-rock-footing-20261107/`:

- Native 1920 by 1080 clay views at yaw 0.35, 0.65 and 0.95 radians, a closer
  contact view, matched old-base/no-support views, and a sharp mirror control.
- An independent check of the actual scene dump confirms exactly two instances,
  a closed consistently wound rock, positive volume and finite unit normals.
  Every vertex within 5 mm of the dragon minimum has support beneath it. Vertical
  separation is 0.75 mm minimum, 3.0 mm median, 9.7 mm p95 and 19 mm maximum;
  this is a fitted support, not an exact conformal surface or physical contact
  simulation. The closer native image keeps that limitation inspectable.
- All 18 focused RT CTest checks pass, including synthetic support topology,
  saved-config roundtrip, existing material routes and resource retry checks.
- Owned Xvfb checks pass clay/mirror changes, reflection routes, saved support
  identity and resize/restore, with three AS builds and three target generations.
- The old reflective-dragon preset remains PNG-byte-identical to its capture
  before this experiment at 1280 by 720.

This audition changes geometry, material values, light and camera in an opt-in
preset. It adds no shader path, GI, clouds, denoising or progressive primary
sampling. Fine edges retain the legacy single-sample aliasing. Human artistic
acceptance and a public-site update remain separate from these local checks.

The prepared Stanford dragon retains its source topology and provenance; see
the [project guide](../../projects/raytracing/README.md#stanford-dragon).
