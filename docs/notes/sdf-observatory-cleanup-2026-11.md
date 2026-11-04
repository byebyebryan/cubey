# Observatory layout and dome cleanup

Date: 2026-11-03

Status: retained mechanical/architectural study. The placement/roof review was
addressed in native candidates, with enlarged round and rounded-square bases.
The user subsequently reconsidered the overall diorama direction as too regular
and mechanical; the next audition is [sculptural clay drafts](sdf-sculptural-drafts-2026-11.md).
The base recommendation below belongs to this earlier layout study.

[Review the cleanup](http://starship:8787/reports/5c97f621008fe2a960d7ec5ae3fd52dce5bd0b15881be278abf6bf5a9a5d12e5/).
It includes the previous layout, enlarged round and square bases, a plan view,
front/rear dome inspections and the carried-over furnished section. The three
hero comparisons use the same camera, lighting, exposure and building scale.

## Corrections

- The old lower terrace supported only 31 of 40 tread corners. The new stair is
  pulled back, with a bottom landing inside the enlarged terrace and a solid
  top-landing support. All 40 tread and four landing corners have at least 0.010
  authoring units of boundary clearance in both new layouts.
- The survey tripod has its own pad on the left observing apron. All three
  actual capsule foot endpoints contact that pad and lie clear of the stair.
  The armillary and pots were repositioned to leave a readable approach route.
- The slit now has exact circular side guides and a rear header on the dome.
  Meridian seams terminate under the trim; rivets are excluded from the aperture
  margin. Annex standing seams are seated using the roof plane and normal.
- The lower base is simpler and wider. Both shapes keep the building at scale
  0.68; the default comparison camera is backed up to distance 1.34 to show the
  enlarged footprint. The square is recommended for its readable front edge,
  landing and observing areas. Base shape remains an aesthetic choice.

The square has 536 leaves; its local section has 537. Every shared primitive
definition matches between the intact and section variants. The original scene,
previous reports, runtime code and shader set are preserved. These are static
content captures, with no animation or new performance qualification.

## Evidence and reproduction

The selected cohort is `outputs/sdf/observatory-cleanup-20261103/final`: nine
1600×1200 Balanced/SS2 native captures with required Vulkan validation, including
a fresh capture of the previous layout at the matched camera. All passed.
The executable and 220 SPV hashes remained unchanged. All six new scene/preset
files regenerate byte for byte.

The support audit reads serialized scene poses and composes group transforms;
it checks expanded tread/landing corners and the actual tripod capsule endpoints
against the floor and observing-pad geometry. This is a bounded layout check,
not a general collision or walkability system. The
[summary](../evidence/sdf-observatory-cleanup-2026-11/summary.json) and
[retention manifest](../evidence/sdf-observatory-cleanup-2026-11/retention.json)
pin the raw evidence, original comparison and qualified runtime dependencies.

```sh
rtk proxy python3 -B projects/sdf/tools/author_observatory_cleanup.py
rtk proxy build/release/projects/sdf/sdf --config projects/sdf/presets/observatory-cleanup-2026-11/square.json --render-mode balanced --supersample 2
rtk proxy python3 -B projects/sdf/tools/author_observatory_cleanup.py --capture-dir outputs/sdf/observatory-cleanup-fresh
```

A fresh capture directory is required. Full capture retains copied inputs,
authoring dependencies, native commands, logs and hashes. The
[report builder](../../projects/sdf/tools/observatory_cleanup_report.py) and
[template](../../projects/sdf/tools/observatory_cleanup_report.html) use the
selected final cohort and require a new output directory. The
[publication receipt](../evidence/sdf-observatory-cleanup-report-2026-11.json)
records verified HTTP delivery separately from browser acceptance: Chrome
connected, but navigation failed through the available browser APIs. Native
images and static bundle checks remain the acceptance evidence for this pass.
