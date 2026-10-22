# Procedural limestone and honed-marble guardian

Implemented and natively qualified on 2026-10-22.

The [published comparison](http://starship:8787/reports/65b3ef76be120d14f259412977d3e50fa6e59e435298cd415ece6edb84d1ec87/)
contains plain, limestone and marble views, three native turntables, inspection
passes, the 80-second marble carving film and raw qualification receipts.
The [implementation plan](sdf-procedural-materials-plan.md) records the checkpoints.
User review accepted the limestone appearance and found marble interesting,
requesting more detail. The [finer-marble follow-up](#finer-marble-follow-up)
below records the subsequently revised marble preset and its comparison.

## Result and scope

Separate `sculpture-guardian-limestone[-build].json` and
`sculpture-guardian-marble[-build].json` presets copy the mirrored guardian's
geometry and history exactly: 1,011 primitives, 2,021 instructions and 65 atomic
operations (48 mirrored, 17 single). All three retain geometry identity
`d5fc89f0abe076f0`, hard self-shadows, no ground plane and shared atmosphere/sky.
Construction takes 60 logical seconds, followed by a 20-second finished orbit.
The plain source and earlier sculpture variants remain available.

Limestone adds warm, cloudy color variation and fine, rough grain. Marble uses
a pale honed base, warped veins of varying width, softer beds and thin secondary
veins, with much subtler bump. Native inspection favored limestone. The first
marble trial resembled painted contour lines; the revised version varies their
width and contrast, but remains a stylized approximation. Neither native checks
nor this verdict constitute user artistic acceptance.

`author_guardian_materials.py` reproduces both scenes and four presets byte for
byte. The material vocabulary, parser bounds and runtime switches are in the
[project guide](../../projects/sdf/README.md#procedural-stone-surfaces).

## Surface contracts

- Each material may carry a seeded procedural record. Its parameters survive
  export/reload and enter the appearance fingerprint; geometry fingerprints
  and distance bricks exclude them. Plain-material appearance hashes remain
  compatible. The original 32-byte material record is unchanged; a parallel
  80-byte record stores each pattern.
- Patterns use fixed scene/model coordinates across every primitive and both
  mirrored sides. Revealed bounds and edit order do not move the pattern; cuts
  expose the same volume. Grain is asymmetric even where geometry is mirrored.
  These coordinates do not follow independently moving parts.
- CPU and GPU use the shared seeded value-noise family, with analytic height
  gradients. Patterns evaluate once per material per surface query, outside
  distance tracing. Explicit cut-material ownership and smooth material
  blending retain their existing rules, including gradient blending.
- Fine grain fades with a conservative projected pixel footprint; unresolved
  marble ramps broaden and average. Filtering avoids fragment derivatives in
  divergent hit/miss branches. Bump slope is bounded in the geometric normal's
  hemisphere. Only lighting uses the perturbed normal: shadow/AO traces and
  origin offsets use the geometric normal. Silhouettes and depths are unchanged.
- One-material shaded hits can bypass the full CSG material-ownership walk.
  `--no-material-fast-path` retains the general evaluator for comparison.
  Multimaterial and part-ID queries still walk the instructions. Direct/full
  PNGs match exactly for all three tested guardian variants.
- `--no-procedural` shows flat material values; `--no-bump` retains patterned
  color/roughness. Debug 1 remains geometric normals and debug 8 shows shading
  normals. Native controls expose the enabled pattern and its parameters.

This pass adds no image maps, UVs, geometric displacement, specular environment
lighting, translucency/subsurface scattering, GI or procedural weathering.
Those remain separately scoped experiments. The volume mapping is useful for
a static sculpt; separately moving parts would need explicit material frames.

## Native qualification

Final Release binary SHA-256:
`9ffc5c6681943c9775fde1d814d94059bd6fcf6e6c22de4c025a7cabaa60635c`.
All final manifests agree on binary and shader hashes. Native implementation
ends at `90352d8e`; earlier receipts also record their source commit/dirty state,
so the recorded binary/shader hashes identify the actual qualified artifacts.

Five focused CPU suites pass: authored, material, recipe, normal projection and
mirror authoring. The authored suite retains its 720-order nested-subtraction
oracle and adds procedural parsing, persistence, identity and rejection cases.
The material suite checks 400 noise queries against shared scalar noise and
finite-difference gradients on all axes, plus 200 patterned samples for bounds,
filtering, seeds, normal hemisphere and zero-bump behavior.

Both complete material histories pass: 132 distinct material/state combinations
and 138 checkpoints, including backward, empty and final revisits. Across these
processes, 296,062 sampled-field rays and 558,756 direct CPU/GPU points report
zero mismatches, exhausted rays, finite-grid ambiguities or part disagreements.
Their geometry identities agree at every step. Maximum material-value error is
0.0000212193, height-gradient error 0.000000149012 and shading-normal error
0.000000178814; shortcut error is zero.

Procedural color uses a 0.0002 linear-value agreement bound. Thin vein ramps
amplified floating-point noise/trigonometric differences beyond the former
0.00002 color bound during development. Plain color, distance, gradients,
normals and shortcut validation retain 0.00002 bounds. This is bounded numeric
agreement, not a claim of zero procedural error.

`material-foundation-final/manifest.json` passes deterministic re-renders,
seed changes, export/reload/re-export, disabled-pattern equivalence and visible
bump checks. Plain direct/full rendering and the prior qualified plain PNG are
byte-identical. Geometric-normal and distance-debug PNGs match exactly across
plain/patterned scenes. A mixed stone/marble/plain fixture covers smooth union,
nested transforms and explicit patterned cut material. A small uniform-material
history covers the empty state. Its initial shortcut validator incorrectly
compared an empty surface with a material; `90352d8e` excludes empty geometry.
Both full histories and the final foundation run include that correction.

Twelve matching native views per variant cover hero, front, opposite, profile,
rear, face detail, alternate light, grazing light, color, roughness, shading
normal and clay. Eight structural intent probes pass. Each eight-second
turntable builds the field once; all 720 combined frames decode and pass
screening. The marble construction film visits all 65 operations, with 66
builds and 2,400 screened frames. All 66 marble stills and ten selected limestone
stills are retained; both histories are fully numerically checked. Two selected
cut operations have preview-on/off stills. Final replay/static PNGs match exactly.

Native desktop material controls pass resize, orbit, zoom, pause/reset,
roughness editing, pattern/bump toggles and grain editing. The run builds the
field once; two appearance updates preserve geometry and report no rebuild.
Rendered pixels outside the UI change for each procedural action. The first
test driver clicked below the panel's scroll clip and failed; the final driver
scrolls the material panel five notches before operating the inspected controls.
Its passing receipt is `guardian-materials-controls/windowed-interaction.json`.

The separate history-controls receipt passes seek/undo/redo for both paired
horns and sockets, plus playback, camera and window controls. All committed
identities agree with qualified marble states. It records 23 builds, including
incidental playback visits. A legacy 16-box renderer smoke passes required
Vulkan validation and its 274-ray field oracle without mismatches/exhaustion.

## Measured costs

Controlled Release profiles use RTX 5070 Ti / driver 610.57.04, 1920×1080,
supersample 1, hard shadows, 180 frames / 30 warmup and 148 completed GPU samples
per case. No competing owned native GPU work ran during measurement. Timings
sum SDF shading and shared post; cold field construction is separate.

| Surface / evaluator | GPU median | GPU p95 | Cold field build |
| --- | ---: | ---: | ---: |
| Plain, full evaluator (diagnostic) | 14.0111 ms | 14.5414 ms | 917.598 ms |
| Plain, uniform-material shortcut | 1.7625 ms | 1.8094 ms | 917.965 ms |
| Limestone color/roughness | 1.7662 ms | 1.8076 ms | 917.525 ms |
| Limestone with bump | 1.7670 ms | 1.8154 ms | 920.406 ms |
| Marble color/roughness | 1.7604 ms | 1.8104 ms | 920.982 ms |
| Marble with bump | 1.7688 ms | 1.8274 ms | 921.576 ms |

Pattern/bump differences are within timing variation. The shortcut removes the
dominant ownership walk for this uniform sculpt. The 14.01 ms diagnostic is the
full evaluator in this new binary, not an old-version benchmark. Earlier
guardian timing records retain their original binary and measurement scope.
The general multimaterial path has not received the same optimization.

All cases use 107,286 active bricks and 912,997,552 field bytes (about 870.7 MiB),
with capacity 262,144. The pattern table adds 1,280 renderer bytes, outside that
field accounting. Geometry changes still rebuild synchronously in about 0.92
seconds. Material edits avoid that rebuild; live construction can skip actions
while rebuilding, whereas the offline film visits each operation.

## Evidence and delivery

Artifacts under `outputs/sdf/`:

- `material-foundation-final/`: final parity, image regression, persistence,
  filtering/debug, seed, ownership and cache fixtures.
- `guardian-materials-comparison/`: 36 matched images, three turntables, source
  snapshots, video receipts and structural probes.
- `guardian-limestone-history/`, `guardian-marble-history/`: complete numeric
  histories, static/replay checks, selected previews, exports and marble film.
- `guardian-materials-controls/`, `guardian-materials-history-controls/`:
  passing desktop captures, commands, logs and identity checks.
- `guardian-materials-profile-final/`: authoritative timing manifest, raw pass
  CSVs, traces and native logs. Earlier profile directories are working evidence.
- `material-reproduction.json`: byte-identical generator outputs.
- `guardian-materials-report/`: 547-file, 72,292,914-byte published bundle.
  It keeps the captioned film; the native source film remains in working
  evidence, with its hash and omission explicitly recorded.
- `guardian-materials-served-review.json`: HTTP SHA-256 parity for every served
  file. The shared report server was reused and remains running.

Native stills, desktop controls and extracted film frames were visually
inspected. Static and dynamically selected report assets and JavaScript syntax
were checked. Browser interaction review was unavailable: browser discovery
returned no connected browsers. HTTP delivery and native inspection do not
establish browser interaction acceptance.

The five documentation/layout policy checks and the SDF-only documented CLI
recipes pass. Other demo executables are absent from this SDF build; no full
repository recipe-lane pass is claimed.

Reproduce from the repository root (new output directories must not exist):

```sh
python3 projects/sdf/tools/author_guardian_materials.py
build/release/projects/sdf/sdf \
  --config projects/sdf/presets/sculpture-guardian-limestone.json
build/release/projects/sdf/sdf \
  --config projects/sdf/presets/sculpture-guardian-marble-build.json
python3 projects/sdf/tools/qualify_materials.py \
  --output outputs/sdf/material-foundation-new
python3 projects/sdf/tools/capture_guardian.py \
  --metadata projects/sdf/guardian_materials.json \
  --version before projects/sdf/presets/sculpture-guardian-mirrored.json \
  --version limestone projects/sdf/presets/sculpture-guardian-limestone.json \
  --version after projects/sdf/presets/sculpture-guardian-marble.json \
  --orbits --output outputs/sdf/guardian-materials-comparison-new
python3 projects/sdf/tools/qualify_history.py \
  --binary build/release/projects/sdf/sdf \
  --config projects/sdf/presets/sculpture-guardian-marble-build.json \
  --export-name guardian-marble.json --preview-node left-socket \
  --preview-node left-muzzle-crease-0 --video \
  --output outputs/sdf/guardian-marble-history-new
```

## Finer marble follow-up

After the initial material audition, user review accepted limestone and
requested more detail / higher frequency in marble. The
[finer marble comparison](http://starship:8787/reports/b34aab013e54f0baca6966075de69df69f10bf56e88543b2a13eb60d7dec96b0/)
shows the original, an intermediate denser trial and the revised current preset.
The earlier report and its full carving film preserve the original treatment.

This is a five-parameter material revision, implemented in `dc6c7d4f`:

| Parameter | Original | Intermediate trial | Revised preset |
| --- | ---: | ---: | ---: |
| Pattern scale | 4.5 | 9 | 12 |
| Vein frequency | 5.5 | 11 | 12 |
| Vein width | 0.08 | 0.055 | 0.05 |
| Warp strength | 5 | 7 | 8.5 |
| Grain scale | 220 | 360 | 360 |

The revised material has smaller bends and denser, narrower veins. Its axial
vein frequency (`scale × vein_frequency`) is about 5.8 times the original;
turbulence adds local variation, so this is not a uniform line-count multiplier.
Base colors, roughness amounts, seed, vein direction, offset and the restrained
0.00008 bump amplitude remain unchanged. Geometry and all 65 operations match
exactly. Limestone and both scenes' static/build preset files are unchanged.
The generator reproduces both scenes and all four presets byte for byte.

No native renderer or shader changes were needed. The binary/shader identities
match the initial final qualification. Six matched views per treatment cover
hero, rear, detail, grazing light, color and clay; all eight structural probes
pass. Three native turntables build once each, and all 720 decoded frames pass
screening. Native close-ups, grazing-light and extracted rear orbit frames were
visually inspected before selection. The finest version was selected for the
current preset; user artistic review of that revision remains separate.

The revised material's full history passes 66 distinct states / 69 checkpoints,
148,031 sampled-field rays and 279,378 direct CPU/GPU points, with zero
mismatches, exhaustion, sampling ambiguities or part disagreements. Maximum
CPU/GPU color error is 0.0000758171, within the existing 0.0002 procedural bound;
thresholds were not changed. Final static/replay PNGs are exact. Both
material-only and procedural-only cache checks pass without an additional
geometry rebuild. Four selected committed stills and the paired socket preview
are captured. This follow-up does not claim a new full construction film,
desktop-controls run or performance measurement; the earlier timings retain
their original material snapshot.

Follow-up artifacts under `outputs/sdf/`:

- `marble-frequency-audition/`: five parameter trials and their hero/detail
  renders, including the original snapshot used for matching.
- `marble-frequency-comparison/`: eighteen native inspection PNGs, three
  turntables, saved source/configuration, hashes, video receipts and probes.
- `marble-frequency-history/`: complete revised-material history validation,
  selected stills, preview, exported scene and static/replay comparison.
- `marble-frequency-reproduction.json`: exact generator reproduction receipt.
- `marble-frequency-report/`: 106-file, 20,671,280-byte publication bundle.
- `marble-frequency-served-review.json`: HTTP/hash parity for every served file.

The report's static/dynamic assets and JavaScript syntax pass checks. Browser
discovery again returned no connected browsers, so browser interaction review
remains unavailable. The five documentation/layout checks and all 18 scoped
SDF CLI recipes pass after this follow-up.

Reproduce the three-way report from the captured inputs:

```sh
python3 projects/sdf/tools/marble_frequency_report.py \
  --comparison outputs/sdf/marble-frequency-comparison \
  --history outputs/sdf/marble-frequency-history \
  --output outputs/sdf/marble-frequency-report-new
```
