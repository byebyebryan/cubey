# Selected observatory: three detail passes

Date: 2026-11-03

Status: the user liked all three dioramas aesthetically and selected the
observatory as the next SDF subject. Three native modeling passes are complete;
the new appearance and cutaway await visual review.

[Review the observatory](http://starship:8787/reports/fc82736d322eebefa504777f8e2e4fb3ad7e4860655da61cb89bc056198317c4/).
The report contains the original draft, each modeling pass, alternate views,
dome and interior close-ups, plus downloadable scenes and presets. Its
[publication receipt](../evidence/sdf-observatory-detail-report-2026-11.json)
separates verified HTTP delivery from unavailable browser interaction review.

## What changed

| Pass | Native content | Leaves / saved actions |
| --- | --- | --- |
| Site and architecture | Fractured rock planes and ledges, offset retaining courses, a supported stair, arched windows, entrance stones, paneled doors, shutters, clock, roof vane and better instrument supports. | 392 / 22 |
| Dome and telescope | Exact circular meridian seams, rivets, turntable teeth/brackets, hollow optical tube, recessed objective, collars, eyepiece, fork mount, focus wheel and counterweight. | 493 / 23 |
| Furnished observatory | Timber floors, observing-log shelves, eyepiece cabinet, keeper's chart desk, rolled chart, stool, ink pot and more planting. | 569 / 26 |
| Architectural section | The same furnished content, with one local cutter applied to the shell, trim, roofs, doors, railings and wall-mounted lanterns. | 570 / 27 |

The existing teal/cream/brass direction and matched hero camera/lighting remain.
The rock is still stylized and the materials remain simple. The authoring uses
existing primitives and Boolean operations. Earlier drafts, the other dioramas,
runtime code and renderer defaults are preserved.

The section makes the hollow walls/dome and room contents inspectable. All 569
shared primitive definitions match the intact scene exactly. The architectural
cut excludes 270 leaves, including the instruments, floors, furnishings and site.
This is a static authored CSG variant; it is not an animated cut or evidence of
interactive editing performance. Saved grouped history is accepted by the
native reader, but construction playback was not captured.

## Native review and retained corrections

Selected captures are in `outputs/sdf/observatory-detail-20261103/final-v2`:
eight new stills at 1600×1200, Balanced, SS2, required Vulkan validation. The
report also reuses the original 1280×960 observatory hero. All native runs
completed without validation errors. The executable and 220 SPV fingerprints
remained unchanged, and all eight scene/preset files regenerate byte for byte.

The earlier capture rounds retain the actual review progression. Close-up
inspection caught a solid telescope collar hiding the objective; the assembled
tube and collars are now bored together. Roof seams were raised above their
panels, the armillary/tripod supports were connected, and wall-mounted lanterns
were included in the sectionable group to avoid floating fixtures in the cutaway.
No cleanup of these earlier images or receipts was performed.

## Reproduction and evidence

From the repository root:

```sh
rtk proxy python3 -B projects/sdf/tools/author_observatory.py
rtk proxy build/release/projects/sdf/sdf --config projects/sdf/presets/observatory-2026-11/furnished.json --render-mode balanced --supersample 2
rtk proxy build/release/projects/sdf/sdf --config projects/sdf/presets/observatory-2026-11/section.json --render-mode balanced --supersample 2
rtk proxy python3 -B projects/sdf/tools/author_observatory.py --capture-dir outputs/sdf/observatory-detail-fresh
```

Fresh captures need a new directory. Exact inputs, commands, logs, all three
authoring dependencies, hashes and completion state are retained. The existing
frozen public executable is the runtime dependency; no build was performed.
Rebuilding later is a new runtime identity.

The [report builder](../../projects/sdf/tools/observatory_detail_report.py)
and [template](../../projects/sdf/tools/observatory_detail_report.html) use the
explicit final-v2 cohort and original draft; supply a new `--output` folder.
The [summary](../evidence/sdf-observatory-detail-2026-11/summary.json) and
[retention manifest](../evidence/sdf-observatory-detail-2026-11/retention.json)
pin the local evidence and runtime dependencies. Static report checks passed;
browser interaction review was unavailable because no browser was connected.
