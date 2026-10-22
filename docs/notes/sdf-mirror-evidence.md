# Mirrored stroke authoring and guardian review

Implemented and natively qualified on 2026-10-22.

[Published review](http://starship:8787/reports/28b97723efe9c303766c0ad148b500cfc49758af39d488a292e616d0ce512216/)
contains the 80-second construction/orbit film, all 66 committed stills, paired
red cut previews, eight matched inspection views, desktop receipts and costs.
The original organic guardian and its report remain available.

## Result and scope

The new `sculpture-guardian-mirrored[-build].json` presets use 65 atomic actions:
48 mirrored pairs and 17 single strokes or primitive placements. The source
has 113 coherent stroke placements, presented as 1,011 individual segment
reveals. Pairing saves 48 placements (42.5%); whole-stroke grouping separately
reduces replay from 1,012 states/builds to 66. The final solid still contains
1,011 primitives and 2,021 instructions.

This is an authoring helper and native history extension. There is no live
brush editor or new interactive mirror checkbox: `MirrorAuthor.mirror_x` is the
scripted authoring mode. Existing geometry, capacity limits, field resolution,
materials, hard self-shadows and shared atmosphere/sky remain in use.

`author_guardian_mirrored.py` migrates the saved organic scene deterministically.
It groups the original generator's explicit stroke continuations and verifies
bilateral name correspondence. For each paired feature, the saved left-side
stroke supplies the reflected right-side geometry. Central masses, mouth and
cross-face folds retain their authored asymmetry. The formerly higher right
horn now mirrors the left horn; this is a separate variant, not a pixel-identical
replacement for the original. Generated scene and both presets reproduce byte
for byte (`outputs/sdf/guardian-mirrored-reproduction/manifest.json`).

## Contracts

- History accepts either legacy `node` or nonempty `nodes` per item, never both.
  Every primitive must occur exactly once across the sequence. Each operation
  has one polarity; mixed additive/subtractive leaves are rejected. Every
  subtraction still requires its retained body to participate before its tool.
- Grouped history commits, seeks and undoes the complete operation. Scene
  export preserves explicit geometry and grouped history. Dependency validation
  remains linear apart from map lookup, rather than compiling every prefix at
  load time.
- `reflect_x` reflects local translations and quaternion frames recursively.
  All seven primitive distance functions are locally X symmetric; conjugating
  rotations as `(x,-y,-z,w)` retains handedness without negative scale. Nested
  placement, taper direction, Boolean structure and materials are preserved.
- `MirrorAuthor.edit` emits the original and its reflected copy and records one
  `nodes` operation. `centerline=True` emits once; mirroring can be disabled per
  edit or for subsequent edits. The `mirror_axis` tag is descriptive metadata.
- Native previews collect every negative primitive in an operation. Each
  cutter is ray tested against the current solid depth, and visible tools use
  the existing red highlight. Preview storage is bounded by 1,024 primitives,
  with a separate 64 KiB buffer per frame slot to avoid overwriting an in-flight
  frame. Neither previews nor history grouping change the committed solid.

## Qualification

Release native binary SHA-256:
`596a87e2d0f796036f6c87a1b1a47a4ad687356cd7a573331f90d17a215744b3`.
Evidence manifests retain all shader hashes, commands and scene identities.

`ctest --test-dir build/release -R
'^sdf_(authored|recipe|normal_projection|mirror_author)_tests$'
--output-on-failure` passes all four suites. The authored suite retains its
720-order nested-subtraction oracle and adds transformed grouped cuts,
atomic undo/seek, paired previews, export/reload and malformed group rejection.
The reflection suite checks all seven primitives plus a nested rotated/scaled
CSG tree against independently evaluated reflected points (1,920 samples),
and checks grouping, centerline emission, disabled mirror mode and unique names.

The five documentation/layout policy checks pass. The repository-wide CLI
recipe test is unavailable as a complete gate in this SDF-only build: other
demo executables are missing. Selecting the built SDF application passes all
of its documented native recipes without a Vulkan ICD or display. This is a
scoped recipe check, not a claim that the repository-wide lane passed.

The guardian history process checks 66 distinct states / 69 checkpoints, with
148,031 sampled-field rays and 279,378 direct CPU/GPU points across the process.
Reported mismatches, exhausted rays, finite-grid ambiguities, material errors
and part disagreements are zero. It revisits midpoint, empty and final states,
and checks unchanged-state cache reuse.

The 2,400-frame film visits every operation in order with exactly 66 field
builds. Every decoded frame passes screening. Final replay and static PNGs are
byte-identical. Two selected guardian cut operations have on/off preview
stills; other cutter previews are present in the film, without a claim of
individual visibility qualification for every segment.

`guardian-mirrored-controls/windowed-interaction.json` passes native desktop
resize, orbit, zoom, play/pause/reset, next/previous, numeric seek and paused
cache checks. It also seeks, undoes and redoes operation 12 (both horns, 72
segments) and operation 21 (both sockets). All logged committed fingerprints
agree with the independently qualified prefix identities. The test used 22
builds; incidental playback visits are recorded in its receipt.

The small foundation fixture independently qualifies a two-sided 12-segment
groove. Both image halves contain 22,666 changed/redder preview pixels. Looking
from behind the retained solid makes preview-on/off PNGs byte-identical,
proving solid occlusion. Its full history and film also pass native checks.

Export/reload/re-export files and native PNGs agree exactly. The original
guardian hero PNG remains byte-identical to the previous qualified image.
A 16-box legacy renderer smoke also passes required Vulkan validation and its
field oracle (274 rays, no mismatches/exhaustion).

## Measured costs

Controlled Release profiles use RTX 5070 Ti / driver 610.57.04, 1920×1080,
supersample 1, hard shadows, 180 frames / 30 warmup and 148 completed GPU
samples per version. No other owned native GPU qualification job ran during
these profiles. Both cases use the same binary and shaders. Frame times sum
the SDF shading and shared post passes; cold field construction is separate.

| Scene | GPU median | GPU p95 | Cold field build | Active bricks |
| --- | ---: | ---: | ---: | ---: |
| Original organic | 10.7254 ms | 11.1080 ms | 913.945 ms | 106,643 |
| Mirrored strokes | 10.6659 ms | 11.1948 ms | 918.122 ms | 107,286 |

Treat steady costs as effectively unchanged. Field allocation is 912,997,552
bytes in both cases; this excludes renderer resources such as preview buffers.
The small brick-count difference follows the changed mirrored geometry. This
is not a normalized primitive-cost benchmark or evidence of a rendering speedup.

Every committed action still rebuilds the field synchronously. Late edits take
roughly 0.92 seconds; live playback may skip actions if it falls behind logical
time. Offline capture visits all operations. A coordinate-folding SDF operator,
incremental field rebuild and material evaluation optimization remain separate
future work.

## Evidence and delivery

Artifacts are under `outputs/sdf/`:

- `mirror-foundation-native/`, `mirror-foundation-regression/`: paired preview,
  occlusion, legacy authored image and native small-fixture evidence.
- `guardian-mirrored-history/`: every state, all stills, selected previews,
  export, native film, captioned film and decode receipt.
- `guardian-mirrored-comparison/`: sixteen matched captures, saved inputs,
  authoring source snapshots and eight passing structural intent probes.
- `guardian-mirrored-controls/`, `guardian-mirrored-profile/`,
  `guardian-mirrored-roundtrip/`, `guardian-mirrored-reproduction/`: corresponding
  native controls, controlled costs and persistence/reproduction checks.
- `guardian-mirrored-report-final/`: 375-file, 44,216,321-byte publication bundle.
  Static and dynamically selected assets and JavaScript syntax are checked.
- `guardian-mirrored-served-review.json`: HTTP bytes and hashes for every
  published file. The existing shared report server was reused.

Native images, desktop undo/redo captures and captioned film frames were
visually inspected. Browser interaction review was unavailable: the previous
browser connection was disconnected and discovery returned no browsers.
HTTP delivery and native qualification do not imply browser interaction or
human artistic acceptance.

Reproduce from the repository root:

```sh
python3 projects/sdf/tools/author_guardian_mirrored.py
build/release/projects/sdf/sdf \
  --config projects/sdf/presets/sculpture-guardian-mirrored-build.json
python3 projects/sdf/tools/qualify_history.py \
  --binary build/release/projects/sdf/sdf \
  --config projects/sdf/presets/sculpture-guardian-mirrored-build.json \
  --export-name guardian-mirrored.json --preview-node left-socket \
  --preview-node left-muzzle-crease-0 --video \
  --output outputs/sdf/guardian-mirrored-history-new
```
