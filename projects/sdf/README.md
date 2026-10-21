# SDF

Boxes-only procedural SDF rendering project derived from projectR. The
[implementation plan](../../docs/notes/sdf-projectr-port-plan.md) defines the
checkpoints; the [source review](../../docs/notes/projectr-architecture-review.md)
and [development history](../../docs/notes/projectr-development-history.md)
preserve the reference and its experiments.

The native renderer builds a four-stage hierarchy at depths 2/4/6/8, filters
ordered box edits into leaf lists and samples 8-cubed R16F bricks. Paired fields
promote only after a completed batch of at most 128 edits. The complete CPU
recipe remains independent of those caches. Empty cells use conservative box
exits; hits write surface depth, and exhaustion is visible in magenta.

This is an implementation checkpoint. Shared atmosphere/HDR integration,
soft shadows, presentation playback, persistence and performance qualification
are still in progress. See the [evidence record](../../docs/notes/sdf-implementation-evidence.md).

```sh
cmake --preset dev
cmake --build --preset dev --target cubey_project_sdf cubey_project_sdf_recipe_tests
ctest --preset dev -R '^sdf_recipe_tests$'
build/dev/projects/sdf/sdf
build/dev/projects/sdf/sdf --headless --scene cut --debug 1 --no-ground \
  --validate-field --require-validation --output /tmp/sdf-cut.png
build/dev/projects/sdf/sdf --headless --count 2048 --atlas-axis 92 \
  --shadows 1 --output /tmp/sdf-sticks.png
```

Drag to orbit, scroll to zoom, R to reset, Space to pause auto-rotation and
Escape to close. Static orbit/view changes reuse the existing field.
`--scene` selects `box`, `rotated`, `cut` or `sticks`; `--seed` and `--count`
specify the reproducible stick recipe. `--yaw`, `--pitch` and `--distance`
specify the capture camera in radians/workspace units. `--debug` selects shaded
(0), normal (1), ray-distance (2) or traversal-work (3) display. `--shadows 1`
enables hard mapless shadows; `--no-ground` removes the receiver.

`--occluder` draws a raster plane crossing the sculpture to check surface depth.
`--validate-field` compares GPU rays against direct CPU box CSG, including axis
parallel and inside-solid rays. `--validate-batches` checks empty/reset and
128/129/256/257-edit scenes with an individually visible box per edit.

Atlas profiles are explicit: `--atlas-axis 64` (default, 262,144 bricks), `80`
(512,000) or `92` (778,688). All remain below a 2 GiB field resource budget.
The default 512-stick scene fits the first profile; the seed-42 2,048-stick
scene needs 92. The 6,400-stick stress scene exceeds even that profile and fails
without writing a successful capture. Profiles do not change sample spacing.

The field sample spacing is `1/(256*7)`. The hit tolerance is 0.2 sample and
normal differences are half a sample, clamped within the brick. An inside-solid
ray finds its exit. The CPU oracle bounds sampled visibility with the
1-Lipschitz trilinear error bound plus R16F allowance, and reports grazing
ambiguities separately. This does not establish analytic-surface pixel parity.
