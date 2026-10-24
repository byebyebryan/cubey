# Material presentation evidence

Date: 2026-10-23

Status: implemented and technically qualified. The new shot choices await user
visual review. The accepted material recipes and shader appearances are the
baseline; this pass adds presentation and capture content.

[Open the two stories and inspect the saved cut states](http://starship:8787/reports/d0ddec82e8047da315007f815bd30ce9192711b4c420183268faf747b4746d77/).
The [committed media package](../media/material-workshops/README.md) keeps both
native masters, gallery films, posters, recipes and hashes independently of
report hosting.

## Selected direction

The [plan](material-presentation-plan.md) separates two questions. PBR shows
contrasting surface structure and light response. SDF shows how an explicit
subtraction exposes surfaces that evaluate the same solid material field.

Working-tree auditions compared two four-specimen PBR groups in studio/cloud
lighting and two SDF framings under the same alternatives. The candidate PNGs
and command logs remain under `outputs/material-presentation`; they are selection
evidence, not the clean-source final capture receipts.

The selected PBR group uses wood on a bored block, rusted steel on a sphere,
cloth on an oriented swatch and bronze on a sphere. Bronze replaces frosting
because the glass lacked a useful transmitted-background cue in that composition.
The previous library report retains the focused clear/frosted optical study.

Both selected stories use the accepted frozen low-sun environment. One framing
correction improves end-grain visibility. The camera holds for 1.5 seconds at
the beginning and end of the PBR clip, with an eased twenty-degree arc between.
The SDF story opens with the body already present, previews a red subtractor,
commits its bore at 2.65 seconds and makes a small eased camera reveal before
the final hold. No diagnostic view replaces the ending.

## Implementation and ownership

- `df0341d4` adds opt-in mesh presentation layouts with per-specimen shapes and
  transforms, preserving the full catalog workshop and thickness estimates.
- `d43fac35` adds a two-operation V3 authored wood scene and a separate
  `material-story` timeline. Existing construction/presentation modes keep their
  prior behavior. History seek, material domains and resolved recipes survive
  the new scene's round trips.
- `37838ac8` unifies playback with the existing orbit pause state and records
  reproducible captures. All final story images, native films and profiles were
  captured from this clean source identity, with binary, shader, preset and
  recipe hashes retained per run.
- `f1deaf3f` makes the native R key restart the PBR presentation and starts
  presentation controls collapsed. Private Xvfb input/resize checks qualify
  this later UI behavior; the headless film paths are unchanged.
- `b3d7da10` packages the compact presentation and three inspectable SDF states;
  `819290d6` makes video metadata and retained-control checks reproducible.

No shared renderer or material shader changes are required. The existing
five-highlight root-gallery package remains unchanged. Guardian/robot geometry,
deeper material realism and a new authored-content film have separate scopes.

## Native and media qualification

The scoped CTest gate passes eighteen entries, covering core/foundation/layout/
documentation, material recipes, both workshop configurations and SDF tests.
New checks cover initialized opening state, monotonic history, preview before
commit, seek, held camera endpoints, beauty-view endings and V3 recipe/domain
preservation through the cut. Existing documented workshop CLI commands pass;
the final documentation gate also checks the new presentation front doors.

Required Vulkan validation passes for seven final stills, two 480-frame native
films, both profiles and the two retained controls. SDF opening/ending stills
also pass the field and CPU/GPU material oracles and saved-history validation.
Mesh gallery and SDF wood-sphere controls remain byte-identical to the prior
library captures. The full older 57-image matrix is not rerun.

The SDF film performs one initial body build and one committed-cut build,
retaining the 912,997,552-byte field allocation (approximately 871 MiB). Its
recorded committed build takes 9.605 ms on the CPU, with GPU filter/brick scopes
of 0.453/5.871 ms. This is one construction event, not a steady-state cost or a
general editing-performance claim. The environment is generated once and held.

| Held scene | Median GPU ms | P95 ms | Samples |
| --- | --- | --- | --- |
| PBR four-specimen group with clouds | 1.344 | 1.358 | 94 |
| SDF finished wood cut with clouds | 2.134 | 2.179 | 94 |

RTX 5070 Ti, driver 610.57.04, 1920 × 1080, 1×, required validation and sixteen
warmup frames. Whole-scene GPU scopes exclude initialization, field construction,
environment generation and encoding. These measurements check the budget of
each scene; scene complexity differs between the consumers.

Both native presets pass private Xvfb checks for render readiness, Space pause,
resize, pointer orbit, scroll, reset and clean Escape exit with validation.
The presentation controls start collapsed and remain accessible through the
title bar. The PBR workshop can be reopened from the presentation controls;
the SDF scene exposes its two saved history steps.

All four committed films decode to 480 frames, eight seconds, 60 FPS, H.264 and
no audio. The 1080p native masters produce 720p gallery derivatives; each poster
is extracted from its own gallery film at 6.5 seconds. Selected decoded frames
were inspected for initialized openings, the preview/commit event and final
holds.

## Report and delivery

The compact report contains two playable gallery films, seven native state
stills, two posters and both downloadable native masters. It links the larger
library and workshop evidence rather than duplicating their matrices.

Headless Chrome checks seven interactions, all nine PNG assets, actual playback
of both films, script errors and a 390-pixel mobile layout. These checks pass
from both local files and the published HTTP URL. All sixty report files,
8,709,682 bytes, match source SHA-256 over HTTP from Starship and Carbon.
The publication reuses the shared report server; prior reports remain immutable.

Receipts, raw profiles and logs are retained under
`outputs/material-presentation`, including `browser-http-qa.json`,
`delivery-starship.json` and `delivery-carbon.json`. These are technical and
delivery checks. Final aesthetic acceptance remains with the user.
