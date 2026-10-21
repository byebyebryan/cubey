# SDF

Boxes-only procedural SDF rendering project derived from projectR. The
[implementation plan](../../docs/notes/sdf-projectr-port-plan.md) defines the
checkpoints; the [source review](../../docs/notes/projectr-architecture-review.md)
and [development history](../../docs/notes/projectr-development-history.md)
preserve the reference and its experiments.

[Watch the native demo and review the captures](http://starship:8787/reports/7f1fd699ff363f13294391b0917eac5e1ba9e98f348aaedced78de93d4142211/).
The review includes the 24-second presentation, lighting comparisons,
explicit recipe, applied configurations and qualification receipts.

The native renderer builds a four-stage hierarchy at depths 2/4/6/8, filters
ordered box edits into leaf lists and samples 8-cubed R16F bricks. Paired fields
promote only after a completed batch of at most 128 edits. The complete CPU
recipe remains independent of those caches. Empty cells use conservative box
exits; hits write surface depth, and exhaustion is visible in magenta.

The scene uses Cubey's render graph, shared HDR post, atmosphere sky integration
and atmosphere-derived direct light/diffuse irradiance. Construction, orbit and
the diagnostic presentation use a deterministic logical timeline. Native V1
checks and the fixed performance matrix pass on the selected RTX 5070 Ti;
the captured composition is ready for user visual review.
See the [evidence record](../../docs/notes/sdf-implementation-evidence.md).

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

Drag to orbit, scroll to zoom, R to reset, Space to pause playback and
Escape to close. Static orbit/view changes reuse the existing field.
`--scene` selects `box`, `rotated`, `cut` or `sticks`; `--seed` and `--count`
specify the reproducible stick recipe. `--yaw`, `--pitch` and `--distance`
specify the capture camera in radians/workspace units. `--debug` selects shaded
(0), normal (1), ray-distance (2), traversal-work (3) or brick-cell (4) display. `--shadows 1`
enables hard mapless shadows; `--no-ground` removes the receiver.
`--shadows 2` selects four-sample soft shadows, with angular radius controlled
by `--shadow-softness`. `--ao` enables four local hemisphere queries affecting
environment visibility. Neither mode provides indirect bounced light.

`--sun-elevation` and `--sun-azimuth` use degrees; `--exposure` uses stops.
`--roughness` adjusts the plain material. `--supersample 2` renders at twice
the output width/height and filters through the shared post pass, useful for
thin-feature captures. Default sampling is 1 for timing runs. The sky uses the
foundation's backdrop horizon remapping; this does not remap surface lighting.

`--occluder` draws a raster plane crossing the sculpture to check surface depth.
`--validate-field` compares GPU rays against direct CPU box CSG, including axis
parallel and inside-solid rays. `--validate-batches` checks empty/reset and
128/129/256/257-edit scenes with an individually visible box per edit.

`--export-recipe /path/scene.json` saves every ordered edit, including the final
partial batch. `--recipe /path/scene.json` rebuilds from that explicit list.
The versioned file stores forward quaternions as xyzw, generator metadata and
a geometry hash; invalid geometry, unsupported versions and hash mismatches
fail before upload. The generator seed alone is not the saved sculpture.

Use the [stick-ball preset](presets/stick-ball.json) for the main visual scene:

```sh
build/dev/projects/sdf/sdf --config projects/sdf/presets/stick-ball.json
build/dev/projects/sdf/sdf --config projects/sdf/presets/stick-ball.json \
  --headless --motion presentation --capture video --fps 30 --output /tmp/sdf.mp4
build/dev/projects/sdf/sdf --headless --motion construction --time 4 \
  --count 257 --output /tmp/sdf-half.png
```

`--motion` selects `static`, `orbit`, `construction` or `presentation`.
The default presentation is 24 logical seconds: eight for construction,
twelve for orbit, then traversal and brick diagnostics. `--duration`,
`--construction-seconds` and `--orbit-turns` adjust the timeline. `--time`
selects an exact headless still state. Motion videos default to duration times
FPS frames; an explicit `--frames` overrides that count. First/last capture
frames land at the timeline endpoints. Rendering and capture FPS do not decide
the edit prefix.

Reproduce the fixed native acceptance/capture matrix and assemble its review:

```sh
python3 projects/sdf/tools/qualify.py --video --windowed
python3 projects/sdf/tools/report.py
```

The optional windowed check uses an isolated Xvfb server, libX11/libXtst and
FFmpeg. The headless matrix needs the app and CPU-test targets built above.
Receipts, applied configs, profiles, recipe and captures go under
`outputs/sdf/qualification`; `report.py` prepares a separate static export.
Use a fresh `--output` directory when the binary or shaders change.

The fixed 1080p matrix uses 1× sampling and a closer default camera: median GPU
time was 3.05 ms for primary rendering and 12.56 ms with soft shadows. The
presentation capture uses 2× sampling at 720p. See the evidence record for
frame counts, p95 values, capacity failures and the sampling-error boundary.

The windowed panel supplies playback/scrubbing, scene regeneration, shading,
light and diagnostic controls. A full batch is retained as the reusable base;
the displayed partial tail is rebuilt when it changes. Backward scrubbing can
rebuild from the complete recipe. Updates wait for readers before replacing
either GPU field. Capacity failure stops publication, displays the completed
prefix and reports the limit; headless capture fails.

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
