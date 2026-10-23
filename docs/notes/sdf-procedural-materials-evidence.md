# Procedural limestone and honed-marble guardian

Implemented and natively qualified on 2026-10-22.

The [published comparison](http://starship:8787/reports/65b3ef76be120d14f259412977d3e50fa6e59e435298cd415ece6edb84d1ec87/)
contains plain, limestone and marble views, three native turntables, inspection
passes, the 80-second marble carving film and raw qualification receipts.
The [implementation plan](sdf-procedural-materials-plan.md) records the checkpoints.
User review accepted the limestone appearance and found marble interesting,
requesting more detail. After the [finer-marble audition](#finer-marble-follow-up)
proved too busy, the [balanced follow-up](#balanced-marble-follow-up) revised
the current preset to sit between the original and the denser trial.
User review accepted Balanced; the
[configurable marble and body-color follow-up](#configurable-marble-and-body-color)
adds named detail choices and independent subtle tint.
The first tint was too faint in user review; the
[color visibility correction](#color-visibility-correction) retains those
controls while making the warm/cool patches visible.
User review then retained cream and rejected the bluish cool-gray end; the
[original and cream palette](#original-and-cream-palette) replaced that endpoint.
Further review found the soft placement stain-like; the
[defined mineral pockets](#defined-mineral-pockets) improved the placement.
The [coherent mineral color](#coherent-mineral-color) derived colored cores,
cream shoulders and pockets directly from the marbling. User review found
that relationship too strict; the latest
[partially correlated mineral regions](#partially-correlated-mineral-regions)
give cream regions their own boundaries and internal streaks, first reviewed
on flat native swatches and then on the guardian.

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
shows the original, an intermediate denser trial and the fine revision from
that pass. Both denser versions were subsequently too busy in user review.
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
visually inspected before selection. The finest version was initially selected;
user review then rejected its density and found even the intermediate trial too
busy. The balanced follow-up below supersedes that selection.

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

## Balanced marble follow-up

User review found both the fine revision and the denser trial too busy, asking
for something between the original and denser trial. The
[balanced marble comparison](http://starship:8787/reports/f59ca1bd61e2982b7a4d83a45fd0dc4c821e33078218a3cbf52cbbe1aadec403/)
shows original, balanced revision and previous denser trial in that order.
The balanced version is now the current preset, pending further artistic review.

Two quieter hero/detail candidates were rendered. The more restrained candidate
was selected: scale 6, vein frequency 7.25, width 0.07, warp strength 5.5 and
grain scale 280. Its axial frequency is about 1.76 times the original, compared
with 4 times in the denser trial. Increasing both scale and vein frequency in
the previous pass had compounded the density increase. Gentler warp now keeps
more quiet space around the facial carving. Base colors, roughness amounts,
seed, direction, offset and bump amplitude remain unchanged, as does limestone.

The exact geometry and 65-operation history match the earlier snapshots.
The renderer/shaders are unchanged. Six fresh matching views and a 240-frame
turntable pass native validation and video screening; eight structural probes
pass. Final-state checks cover 4,297 sampled-field rays and 7,806 direct CPU/GPU
points, without mismatches, exhaustion, sampling ambiguities or part
disagreements. Maximum CPU/GPU color error is 0.0000147223. Both material-cache
checks retain a single field build. All six generator outputs reproduce exactly.
Hero/detail stills and an extracted rear orbit frame were visually inspected.

This bounded appearance adjustment does not rerun the entire history, desktop
controls or profiles. The previous complete history receipts retain their
original material snapshots. Original and denser comparison assets are reused
only after checking their recorded hashes, binary/shaders/GPU, source geometry
and matching render settings. Static/dynamic report assets and JavaScript
syntax pass checks; browser interaction review was not performed.

Artifacts under `outputs/sdf/` are `marble-balance-audition/`,
`marble-balance-native/`, `marble-balance-reproduction.json` and the
78-file, 15,986,434-byte `marble-balance-report/`. The report includes its
standalone builder and both source receipts. `marble-balance-served-review.json`
records HTTP/hash parity for every published file. Documentation/layout policy
checks and all 18 scoped SDF CLI recipes pass.

## Configurable marble and body color

User review accepted Balanced and requested configurable detail, with Balanced
as default, plus subtle color variation. Implemented in `c8ca487d`; the
[controls and color comparison](http://starship:8787/reports/182df0953d72e6c65e962feee540fc01b8c75e1524bec3bc5bc05c5ec8495cd6/)
shows balanced marble at color-variation amounts 0, 0.08 and 0.16. The saved
guardian uses 0.08. Acceptance of the balanced pattern predates the new tint;
artistic review of the tint remains separate.

The native material panel offers Original, Balanced, Denser and Fine. These
choices set only scale, vein frequency/width/warp and grain scale, retaining the
artist's palette, roughness, seed, orientation, offset, tint and bump amplitude.
Manual changes to those five controls display Custom. JSON continues to store
explicit parameters; the UI infers the name instead of overwriting custom
values at load. Save/export/reload retains the detail choice and tint.
The scripted generator exposes `--marble-detail` and
`--marble-color-variation`, defaulting to balanced and 0.08.

The optional `color_variation` parameter is finite and bounded to 0–0.25.
Omission defaults to zero, retaining existing scene appearance. One additional
seeded value-noise sample makes broad warm/cool body patches at 0.45 times the
pattern scale. Pixel-footprint filtering fades unresolved color towards neutral;
the existing vein mask attenuates tint near veins. Color changes only: roughness,
height gradients, normals and geometry stay intact. This uses the formerly
reserved W component of the domain-offset vector, retaining the 80-byte pattern
record and 1,280-byte renderer table. There is no added appearance volume.

Final Release binary SHA-256:
`45bc8ba67dafb44f62ac7dadfb77b7cd41f1566a045588ff633dc261aa3073cd`.
All final native qualification, controls, comparison and profile manifests
agree on this binary and their complete shader maps. The earlier working
foundation folder was superseded after rebuilding the final UI binary; only
`marble-controls-foundation-final/` is authoritative here.

Five focused CPU suites pass. New tests cover four detail choices retaining
unrelated appearance parameters, Custom detection, both signs of warm/cool
variation, its filtered-away limit, unchanged roughness/gradients, finite bounds,
appearance-only identity and persistence. The prior noise, geometry, nested-cut,
mirror and normal-projection tests remain in the same passing suites.

The expanded native foundation run passes plain-image regression, procedural
determinism, shortcut/full agreement, save/reload/re-export, seed variation,
material ownership through smooth/nested/explicit cuts, and empty/short cut
histories including tinted marble. Zero tint reproduces the previous balanced
PNG exactly. Tint on/off roughness and shading-normal PNGs agree exactly.
Material, procedural and tint-only cache checks retain one field build.
Maximum CPU/GPU color error across these fixtures is 0.0000147223, below the
existing procedural bound; no tolerance changed. This does not requalify the
complete 66-state guardian history.

The desktop test selects Original, Denser, Fine and Balanced in order, then
edits frequency to Custom and adjusts tint. Eight logged appearance updates
retain geometry `d5fc89f0abe076f0` and report no rebuild; the process builds once.
Tint changes 64,891 rendered pixels outside the UI. Default, restored and Custom
states were visually inspected. The inspection driver initially assumed eight
full wheel steps despite reaching the panel's scroll limit; its corrected
coordinates and mouse selections are recorded in the passing final receipt.

Fifteen matching native stills and three turntables cover neutral, subtle and
stronger amounts. All eight design probes and 720 decoded frames pass. Native
color/detail stills and UI states were inspected. Browser discovery returned
no connected browsers; browser interaction review remains unavailable.

Matched RTX 5070 Ti / driver 610.57.04 Release profiles use 1920×1080,
supersample 1, hard shadows, 180 frames / 30 warmup and 148 completed samples
per case. GPU timings sum shading and post; no competing owned GPU job ran.

| Color amount | GPU median | GPU p95 | Cold field build |
| --- | ---: | ---: | ---: |
| 0 / neutral | 1.7597 ms | 1.8034 ms | 917.874 ms |
| 0.08 / subtle default | 1.7763 ms | 1.8278 ms | 918.095 ms |

This matched pair measures the default uniform-material sculpt. It does not
establish a general cost model for multimaterial scenes or other hardware.

Artifacts under `outputs/sdf/`:

- `marble-controls-foundation-final/`: final native regression, tint invariants,
  ownership, short histories, export and cache evidence.
- `marble-live-controls/`: passing owned desktop captures, identities and logs.
- `marble-colors-comparison/`, `marble-colors-inputs/`: matched saved inputs,
  native images, orbits, hashes, video checks and design probes.
- `marble-colors-profile/`: raw pass CSVs, traces, commands, logs and summary.
- `marble-controls-reproduction.json`: six byte-identical generator outputs;
  invalid NaN/out-of-range amounts rejected without edits.
- `marble-colors-report/`: 185 files / 31,740,586 bytes, including its builder.
- `marble-colors-served-review.json`: HTTP/hash parity for every published file.

Static/dynamic report targets and JavaScript syntax pass. Documentation/layout
checks and all 18 scoped SDF CLI recipes pass. The work is committed locally;
no push is part of this follow-up.

## Color visibility correction

User review found even the stronger 0.16 trial hard to distinguish. The
[clearer marble color review](http://starship:8787/reports/967d2eb6fbd98ae3e61200739d2620da3e7d10eb4cc9a80736ab5f08f42423b3/)
shows neutral, revised 0.08 default and revised 0.16 stronger tint, plus a
same-amount 0.16 before/after pair. Implemented in `f8f2c8df`.

The original tint was a small signed RGB perturbation driven by one very broad
noise sample. In the shaded close-up, its 0.08 and 0.16 cases both had a median
largest-channel change of one 8-bit level from neutral. The 95th percentile
was only two / four levels. The original source images and measurements are
retained; numerical visibility alone did not establish a useful visual result.

CPU and GLSL now blend between cream and cool-gray mineral multipliers, using
contrast-shaped seeded noise. Color frequency rises from 0.45 to 0.9 times
pattern scale, placing several soft patches across the face. The 0–0.25 amount
maps linearly to 0–1 palette blend strength. Vein masking attenuates tint near
veins, and footprint filtering fades unresolved variation to neutral. The saved
0.08 amount, Balanced detail, parameter ranges, scene files, palette controls,
roughness, bump, geometry and all 65 history operations remain unchanged.

Compared with the matching neutral image, the revised default's shaded
largest-channel change has median 6 / p95 11 levels; the revised 0.16 case has
median 13 / p95 25. Base-color changes are larger. These statistics use changed
pixels only; they are diagnostics, not a perceptual quality score. Native
default/stronger close-ups were visually inspected, with cream cheeks/jaw and
cooler brow regions now readable. User artistic acceptance remains separate.

Five focused CPU suites and 36 native foundation render/validation runs pass.
The latter retain plain/limestone regression, CPU/GPU agreement, deterministic
rendering, shortcut/full equivalence, save/reload, material ownership, empty
and short cut histories, and cache checks. Zero amount still matches the prior
balanced PNG exactly. Roughness and shading-normal images remain exact across
tint on/off. Maximum observed CPU/GPU color error is 0.0000147223; thresholds
were unchanged. Fifteen matching stills, eight structural probes and all 720
decoded orbit frames pass. This correction does not rerun the full guardian
history, desktop controls or performance profiles; prior receipts keep their
original binary/shader scope.

Final native binary SHA-256:
`5f17c0008da6b294037b41d4343f19bbca1ee27c860c08c51a57b9f192e72e8c`.
Foundation and comparison manifests agree on that identity and all shader
hashes. The old 0.16 image retains its prior receipt, and scene parameters and
render settings match exactly across the before/after pair.

Artifacts under `outputs/sdf/` are `marble-color-visibility-foundation/`,
`marble-color-visibility-comparison/`, `marble-color-visibility-before.json`,
`marble-color-visibility-after.json` and the 156-file / 28,540,512-byte
`marble-color-visibility-report/`, including its builder. The
`marble-color-visibility-served-review.json` receipt checks every served file's
HTTP bytes/hash. Report targets, JavaScript syntax, documentation/layout policy
checks and all 18 scoped SDF CLI recipes pass. Browser discovery returned no
connected browsers, so browser interaction review remains unavailable.

## Original and cream palette

User review accepted the cream direction but found the cool-gray end bluish
and dirty, requesting cream plus the original color. Implemented in `c9e30022`.
The [original and cream review](http://starship:8787/reports/de9be4cc6f48d9b5723fb4e0f6378d222bce6ae528c5844b3dbdb8927a1f136b/)
compares no variation, default 0.08 and stronger 0.16 amounts. A matched 0.16
before/after pair shows the replaced endpoint.

CPU and GLSL replace the cool multiplier with identity RGB `(1,1,1)`, retaining
the existing cream endpoint, noise placement/contrast, vein attenuation and
filtering. The original pale material now occupies the uncolored portions.
Balanced detail, amount/range, scene files, roughness, bump, geometry and all
65 operations remain unchanged. Lighting can still produce cool shadows; the
material itself adds no blue endpoint. The generator help text now describes
original/cream variation.

Five CPU suites pass. The material suite now requires one-sided color movement
relative to the untinted sample: red does not decrease and green/blue do not
increase beyond rounding tolerance. Warm and untouched samples must both occur;
roughness, gradients and the filtered-away limit retain their checks.
All 36 native foundation render/validation runs pass, including exact zero-tint
agreement with the prior balanced image, tint-on/off roughness and normal
invariants, persistence, ownership, short histories and material cache reuse.
Maximum CPU/GPU color error is 0.0000147223, with unchanged bounds.

Fifteen fresh matching stills, eight design probes and all 720 decoded turntable
frames pass. Default/stronger shaded close-ups and the base-color view were
visually inspected. Native binary SHA-256 is
`d469869b009e320a2b58dbcdce905bdeedc06bdc8526686defe19d867213d757`;
foundation and comparison manifests agree on that and all shader identities.
The old cool-palette image retains its previous receipt, with equal saved
scene parameters and render settings. The full guardian history, desktop
controls and performance profiles are not rerun for this endpoint change.

Artifacts under `outputs/sdf/` are `marble-cream-foundation/`,
`marble-cream-comparison/`, the 156-file / 28,482,528-byte `marble-cream-report/`
(including its builder), and `marble-cream-served-review.json` for complete
HTTP/hash verification. Static/dynamic targets, JavaScript syntax, five
documentation/layout checks and all 18 scoped SDF CLI recipes pass. Browser
discovery returned no connected browser, so browser interaction review remains
unavailable. Artistic acceptance of this palette revision remains separate.

## Defined mineral pockets

User review liked the cream/original colors but found their placement dirty
rather than marble-like, requesting higher frequency and more definition.
Implemented in `9acb410e`. The
[mineral-pocket review](http://starship:8787/reports/2fe7ca71500f4f1a4318deafb4429b80d10cb192f38a5776a1e1414520b4eb96/)
leads with matched 0.16 before/after images, followed by neutral, default 0.08
and stronger 0.16 stills and native turntables.

### Reference and interpretation

The slab and close-up of
[Antolini Calacatta Macchia Vecchia Original](https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original)
were inspected as a natural-marble reference. Warm color appears near veins
and in irregular pockets, with pale areas between them. This observation
describes that chosen cream/white look; marble has many other appearances.
The manufacturer photographs are linked, not redistributed in the report.

[PBRT's MarbleTexture discussion](https://www.pbr-book.org/3ed-2018/Texture/Noise)
organizes color using a noise-perturbed strata coordinate. The implementation
inference is to relate warm pockets to the existing warped vein structure,
rather than overlay an unrelated cloud. This is an artistic approximation,
not a geological simulation or a measured fit to the reference slab.

### Placement change

The old tint smoothly covered much of the surface, using one noise sample at
0.9 times pattern scale. The current pocket field uses 4 times pattern scale
(about 4.4 times the former frequency) plus finer noise at another factor of
2.43. Subtracting a term from the existing warped vein-band coordinate favors
warm pockets near those bands. A narrower threshold defines the boundaries;
interior color strength varies instead of making every island a flat swatch.
The first native trial was rejected during assistant inspection because its
larger, nearly uniform islands resembled painted patches. The refined trial
reduces their size and varies their interior warmth. User artistic acceptance
remains pending; this is still a stylized surface treatment.

Analytic noise gradients and a conservative vein-gradient bound broaden the
pocket threshold with projected pixel footprint. Fine noise and the complete
pocket color fade as their respective scales become unresolved. CPU and GLSL
share the calculation. The cream/original multipliers, amount/range, saved
0.08 value, Balanced veins, roughness, bump, seed, geometry, 80-byte pattern
record and all 65 history operations retain their values. No new control or
scene parameter is introduced; pocket scale follows pattern scale.

### Fresh qualification

Five CPU suites pass. The material suite additionally checks four intermediate
footprints over 200 points: finite/bounded colors, one-sided cream movement
and unchanged roughness/height gradients relative to untinted samples.
All 36 native foundation runs pass, including CPU/GPU agreement, plain-image
preservation, shortcut/full equality, persistence, material ownership, short
histories and one-build appearance edits. Zero variation still matches the
previously accepted Balanced image exactly. Roughness and shading-normal
images match exactly with tint on/off. Maximum observed CPU/GPU color error
is 0.0000158548; the existing 0.0002 color and 0.00002 geometry/gradient/normal
bounds were retained.

Fifteen matching stills, eight structural probes and all 720 decoded orbit
frames pass. Default/stronger shaded close-ups, the rear and unlit base colors
were visually inspected. The before images retain their original binary
receipt; the 0.16 scene parameters and render settings match across revisions.
Full guardian history and desktop-control qualification were not rerun for
this color-only change.

The fresh RTX 5070 Ti / driver 610.57.04 profile uses 1920×1080, supersample 1,
hard shadows, 180 submitted frames / 30 warmup and 148 completed GPU samples
per case. Shading-plus-post medians are 1.753920 ms at variation 0 and
1.769168 ms at 0.08; p95 values are 1.814080 / 1.826816 ms. Cold field builds
are 917.658 / 918.631 ms. No competing owned GPU job ran. This compares tint
off/on within the revised build; it is not a performance comparison against
the previous shader. Field construction, readback and encoding are excluded
from the GPU frame measurements.

Native binary SHA-256 is
`94b2d37731813c1c695f9d0caa6a23deb1496e5e8971d4dfbbd87bc5f4623967`.
Foundation, comparison and profile manifests agree on that identity and every
shader hash. Artifacts under `outputs/sdf/` are `marble-minerals-foundation/`,
`marble-minerals-comparison/`, `marble-minerals-profile/`, research provenance
in `marble-minerals-research/`, and the 193-file / 33,017,269-byte
`marble-minerals-report/`, including its builder and implementation snapshots.
Browser discovery found no connected browser, so browser interaction review
was not performed. Static/dynamic report targets and JavaScript syntax pass.
All 193 served files match the local bytes and hashes, recorded in
`marble-minerals-served-review.json`. Five documentation/layout checks and all
18 scoped SDF CLI recipes pass. Changes are committed locally; no push is
included in this follow-up.

## Coherent mineral color

The user requested a proper plan and implementation goal loop after reviewing
the relationship between color and marbling. The
[focused plan](sdf-marble-minerals-plan.md) was committed first in `ae82666e`;
the implementation and expanded qualification were committed in `c5a199c3`.
The [coherent-color review](http://starship:8787/reports/0feff6f5a8207ee1f3ed7b6933cbb434b55ff191e8365f248e414699972978ec/)
compares the previous pockets and current mineral structure at identical
scene parameters, amounts, cameras and lighting.

### One shared structure

The prior pattern used independent noise islands with a bias toward the vein
bands and attenuated color at dark cores. The current pattern reuses the
existing warped main and secondary vein coordinates to place every colored
core, surrounding cream shoulder and fine wisp. Seeded noise selects warm
segments and varies their widths; wider shoulders form attached pockets.
It no longer independently places cream islands between the vein families.
The full color amount can reach a selected core, retaining the original
pale/cream multiplier endpoints and amount mapping.

The first native trial established this relationship but had overly smooth
edges. The selected second trial adds filtered edge variation at 11 times
pattern scale and slightly stronger warmth along existing secondary veins.
Native default/stronger shaded and unlit face views, rear views and extracted
orbit frames were inspected. Color now follows the marbling more clearly,
while the underlying vein flow remains smooth and contour-like in places.
This is still a stylized approximation; user artistic acceptance is pending.

The reference interpretation comes from the previously inspected
[Calacatta Cremo](https://www.antolini.com/en/c6/natural-stone-collection/m1556/calacatta-cremo)
and [Macchia Vecchia Original](https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original)
photographs: warm streaks, irregular vein edges and localized wider pockets,
with pale areas between them. It is not a measured reproduction or a
geological simulation. Manufacturer photographs are linked, not redistributed.

Analytic noise derivatives account for the varying shoulder width in its
pixel-footprint ramp. The uncapped width derivative conservatively broadens
the boundary where width is capped. Unresolved warm main/secondary structure
fades rather than generating fine alternating colored lines. CPU and GLSL
use the same calculation. The zero-amount branch retains the accepted neutral
material exactly. Balanced vein paths, four detail choices, scene parameters,
the 80-byte pattern record, limestone, roughness, bump, geometric tracing
normals, geometry and all 65 operations retain their prior values.

### Qualification and presentation

Five focused CPU suites pass. Added spatial checks use an unwarped field:
warm and gray-only main-vein cores both occur, while sampled regions well
between the main and secondary bands retain the pale material exactly.
Existing checks retain bounded, finite, one-sided cream color and unchanged
roughness/height gradients across intermediate and distant filter footprints.

All 40 native foundation render/validation runs pass. Existing neutral/plain,
seed, shortcut/full, persistence, material ownership, short cut-history and
one-build appearance-cache checks remain. Four additional runs exercise every
named detail preset at the maximum 0.25 color amount and another seed.
Maximum observed CPU/GPU color error is 0.0000576973, within the unchanged
0.0002 bound. Geometry/gradient/normal bounds remain 0.00002. Tint-on/off
roughness and shading-normal PNGs match exactly.

Eighteen current native stills, eight structural probes and all 720 current
turntable frames pass. Old images and films retain their original receipts.
All three old/new scene hashes and matching render settings agree; neutral
images match exactly in every common view, and current clay images are exact
across amounts. A new unlit face close-up is presented separately because it
was not captured in the older comparison. Full guardian history and unchanged
desktop controls were not rerun for this color-only revision.

Fresh RTX 5070 Ti / driver 610.57.04 profiles use 1920×1080, supersample 1,
hard shadows, 180 submitted frames / 30 warmup and 148 completed GPU samples
per case. Shading-plus-post medians for variation 0 / 0.08 / 0.16 are
1.782704 / 1.792496 / 1.794864 ms; p95 values are
1.829984 / 1.842784 / 1.852224 ms. Cold field builds are
917.165 / 917.255 / 917.333 ms. No competing owned GPU job ran. These compare
amounts within the revised build, not old/new shader performance. Field
construction, readback and encoding are excluded from GPU frame measurements.

The final native binary SHA-256 is
`ea0046e13e0dd442da986016af39830dcf37aecead16d92e0e73313725e74caa`.
Foundation, comparison and profile receipts agree on that identity and all
shader hashes. The qualification ran before the implementation commit, and
its recorded artifact hashes match the committed implementation and final
captures. The report includes exact implementation snapshots.

Artifacts under `outputs/sdf/` include `marble-connected-foundation/`,
`marble-connected-comparison/`, `marble-connected-profile/`,
`marble-connected-motion-review/`, the two retained native trials and their
inputs, and the 265-file / 46,027,369-byte `marble-connected-report/`.
The tracked `projects/sdf/tools/marble_mineral_report.py` packages the prior
comparison, matching current captures, foundation and profile receipts.
It checks source hashes, exact neutral images, paired settings and image/video
integrity before packaging. Static targets, all 30 dynamic old/new image paths
and JavaScript syntax pass. Browser discovery found no connected browser;
browser interaction review remains unverified.
All 265 served files match the local bytes and hashes, recorded in
`marble-connected-served-review.json`. Five documentation/layout checks and
all 18 scoped SDF CLI recipes pass. The plan, implementation and presentation
are committed as local milestones; no push is included.

## Partially correlated mineral regions

User review found the preceding color placement too strictly tied to veins.
The [revised plan](sdf-marble-regions-plan.md), committed in `0707df80`, starts
with native flat SDF swatches before reviewing the same material on the
guardian. The selected implementation is committed in `6d870222`. The
[swatch and guardian review](http://starship:8787/reports/0a7461860a17aeccae6adc3f5e16cdafa358f9d28031cbd5b6fa3299ceb2a1ca/)
pairs the previous vein-bound color with the new regions at identical amounts,
scene parameters, coordinates, cameras and lighting. Saved variation remains
0.08; the initial 0.16 comparison makes the placement easier to inspect.

### Reference and selected trial

The primary reference is Antolini's
[Macchia Vecchia Original](https://www.antolini.com/en/c3/exclusive-collection/m1559/calacatta-macchia-vecchia-original)
close-up: broad cream areas, narrow dark lines and isolated flecks have
different boundaries. Supplementary
[Cremo Fantasy](https://www.antolini.com/en/c6/natural-stone-collection/m2264/calacatta-cremo-fantasy)
and [Nuvolato](https://www.antolini.com/en/c6/natural-stone-collection/m2253/calacatta-nuvolato)
photographs show directional bands and larger regions crossed by separate
lines. Partial correlation is an inferred procedural direction from those
photographs, not a measured color/scale fit or geological reconstruction.

The pre-edit binary was captured on a rounded flat SDF slab at amounts
0 / 0.08 / 0.16, using unlit, shaded, close and distant views. Its identity is
the previous `ea0046e1…` binary recorded in the preceding section. The first
new swatch trial was broad and soft. The second used smaller, more defined
regions but had flatter interiors. The selected third trial adds finer
directional interior streaks. Native swatches, shaded and unlit guardian
details, the default rear view and extracted motion frames were inspected.
The result remains stylized: the existing smooth, closed vein topology is
still visible, and human artistic acceptance remains pending.

### Evaluation and compatibility

Cream regions use independently seeded, anisotropic fields for their extent
and internal streaks. They share the veins' larger deformation, rather than
being restricted to the narrow main/secondary masks. An independent fine
scale breaks up their edges. A weighted union retains selected warm seams,
while allowing veins through pale and cream areas. Analytic derivatives
include the common deformation and anisotropic scaling. Each spatial scale
and the region threshold use the conservative pixel footprint.

The revised controlled spatial test requires both warm and pale samples away
from either narrow vein family. Warm and gray-only core samples still occur.
The former requirement that every inter-vein sample remain exactly pale has
been removed. Existing bounded, finite, cream-only color, filtering and
unchanged roughness/gradient checks remain. Five focused CPU suites pass.

All 40 native foundation render/validation runs pass. Maximum observed
CPU/GPU color error is 0.0000432134, within the unchanged 0.0002 bound;
geometry/gradient/normal bounds remain 0.00002. Neutral/plain regressions,
deterministic seeds, shortcut/full equality, export/reload, mixed ownership,
short cut histories and one-build appearance edits pass. All four named
detail choices pass at maximum 0.25 color amount with another seed.
Tint-on/off roughness and shading-normal PNGs match exactly. Limestone,
the 80-byte pattern record, palette endpoints, detail controls, scene fields,
geometry/history and material-only caching retain their contracts.

There are 18 current guardian stills and 12 selected current swatch stills.
All ten zero-amount guardian/swatch PNGs match their previous counterparts
exactly. Eight structural probes and all 720 current orbit frames pass;
old films retain their original receipts. Guardian geometry remains
`d5fc89f0abe076f0`: 1,011 primitives, 2,021 instructions and 65 operations.
Full guardian history and unchanged desktop controls were not rerun for this
color-only edit. There are no image maps, new volumes or lighting changes.

### Cost, identity and delivery

Fresh RTX 5070 Ti / driver 610.57.04 profiles use 1920×1080, supersample 1,
hard shadows, 180 submitted frames / 30 warmup and 148 completed GPU samples
per case. Shading-plus-post medians for variation 0 / 0.08 / 0.16 are
1.781056 / 1.799376 / 1.794608 ms; p95 values are
1.837152 / 1.851520 / 1.844128 ms. Cold field builds are
916.685 / 917.307 / 916.864 ms. No competing owned GPU job ran. These compare
amounts within the revised build, not old/new shader performance. Field
construction, readback and encoding are excluded from GPU frame measurements.

The final native binary SHA-256 is
`ba2ae1e8fb00b99e914943a73c62942343b16f3284e0497b272fdf01e0b1e037`.
Foundation, selected swatch, guardian comparison and profile receipts agree
on that identity and all shader hashes. Foundation qualification preceded
the implementation commit; its artifact hashes match the committed source
snapshots and final captures. The report includes exact implementation and
capture-driver snapshots.

Artifacts under `outputs/sdf/` include `marble-regions-swatch-before/`, three
retained swatch trials (selected: `marble-regions-swatch-trial-c/`),
`marble-regions-comparison/`, `marble-regions-foundation/`,
`marble-regions-profile/`, `marble-regions-motion-review/` and the
364-file / 57,913,975-byte `marble-regions-report/`.
The extended `projects/sdf/tools/marble_mineral_report.py` verifies native
capture hashes, matching old/new scenes/settings and exact neutral images
before packaging the swatches ahead of the guardian comparison.

All 32 static local targets, 60 dynamic image targets and JavaScript syntax
checks pass, recorded in `marble-regions-static-review.json`. The attributed
reference close-up is loaded from Antolini's server, rather than copied into
the published bundle; a fresh download matched the previously inspected
photograph. Browser discovery found no connected browser, so browser
interaction review remains unverified. All 364 served local files match
their source bytes/hashes, recorded in `marble-regions-served-review.json`.
Five documentation/layout checks and all 18 scoped SDF CLI recipes pass.
Plan, implementation and presentation are local commit milestones; no push
is included.
