# Organic guardian study

Date: 2026-10-22

Status: the organic sculpt and capacity work are implemented; exhaustive native
history and presentation qualification is running. This is a separate candidate
for visual review, not user acceptance. The clean 106-leaf guardian, original
sculptures and retired driftwood experiment remain intact.

## Content and count

The user asks for an organic jaw/chin and skull, more detail and a construction
closer to 1,000 edits. `author_guardian.py` emits separate form and finished
variants. The underlying form uses 197 leaves; the detailed version uses
1,011 leaves / 2,021 instructions. Its authoring metadata records 113 coherent
strokes and primitive placements. Leaf reveals include the sampled segments of
those strokes; they are not 1,011 independent artistic decisions.

The box-derived jaw becomes a curved tapered mandible and rounded chin. An
elongated cranial vault, forehead mass, occiput and nape replace the single
spherical skull. Facial relief follows brow compression, malar and muzzle forms;
neck/nuchal folds and partial horn growth creases continue around the head.
The stone plinth, plain material, empty sockets, atmosphere and hard self-shadows
remain. There are no new chips, modeled glints, UVs or image textures.

Native auditions are retained as `outputs/sdf/guardian-organic-{form,detail}*`.
The first detail audition was too deeply engraved. A subsequent depth-sign error
opened unintended cheek holes and was rejected. The final candidate uses shallow
cuts placed along the actual surface normal, with separate retained-lip and
chin checks. Do not use those rejected audition images as the finished model.

## Capacity and authoring foundation

`a81f0619` raises authored limits to 1,024 leaves / 2,048 instructions. Tree depth
24, stack 32, 16 materials, field spacing and brick capacity remain unchanged.
Each program buffer grows by 114,688 bytes; the axis-64 field allocation becomes
912,997,552 bytes, within the existing 2 GiB ceiling.

History loading replaces repeated prefix recompilation with recursive ordering
dependencies at subtraction nodes. All 720 permutations of a six-leaf fixture
with nested tools agree with the exhaustive prefix-compiler oracle. Maximum
capacity, overflow, packing and history seek/reset tests pass in Debug and
Release. A native 1,024-leaf fixture includes a distinct last-packed primitive;
4,349 rays and 7,897 direct CPU/GPU points pass with zero mismatches, exhaustion
or part disagreement. The legacy guardian's geometry/appearance fingerprints
and PNG remain exact after the changes, and its generator remains byte-identical.
Debug/Release shader bytes match. Receipt: `outputs/sdf/organic-foundation.json`.

Normal-aware projection is opt-in. Positive depth insets a brush center into the
body; negative depth places a shallow cutter outside it. The new mode uses the
normalized finite-difference SDF gradient instead of the projection axis. An
analytic sphere test covers twelve front/rear/side and inward/outward cases,
including retained material four millimeters beneath an oblique shallow cut.
Legacy axis-offset output remains unchanged when the mode is unselected.

`f28360a1` commits the 1,011-leaf candidate and selective capture support.
Both organic stages and all four presets reproduce byte-identically:
`outputs/sdf/guardian-organic-reproduction.json`. Eight independent design probes
pass, including the lip/chin, hollow sockets, horn roots and nape connections.
The final close-up native check passes 4,297 field rays and 7,806 CPU/GPU points
with zero reported mismatches, exhaustion, ambiguities or part disagreement.
Native export/reload bytes and the normal/reloaded/axis-80 PNGs are identical;
the receipt is `outputs/sdf/guardian-organic-roundtrip/manifest.json`. The separate
eight-second native orbit decodes all 240 frames without exhaustion, retained
under `outputs/sdf/guardian-organic-orbit/`.

## Qualification scope

`qualify_history.py` retains exhaustive native field and CPU/GPU checks for every
prefix, backward/reset revisits and unchanged-state cache reuse. New optional
`--capture-step` / `--preview-node` arguments reduce only exported still/preview
selection; existing defaults retain the full image matrix. A separate three-leaf
native fixture proves that omitted stills do not omit prefix validation, and
that final static/replay pixels remain exact.

This larger study selects fourteen committed stills and the two socket cutter
on/off pairs. Other cutters appear in the native film but are not individually
qualified for preview visibility. Do not claim an exhaustive still-image or
all-cutter visibility matrix. The film has a 120-second build and 30-second
orbit, allowing every leaf to be visited without skipping; ordered visits,
geometry-build count and decoded frames are checked independently.

Matched captures compare the previous guardian, organic form and detailed
candidate across hero/front/opposite/profile/rear/detail/alternate-light profile
and clay views. Camera, lighting and render settings match within each triplet.
The alternate profile light exposes the skull beneath the horn shadows seen
in the default profile. The source, logs and receipts are retained in
`outputs/sdf/guardian-organic-comparison/` and `guardian-organic-history/`.

Remaining visual limits: this is still a stylized scripted sculpture. Fine
relief is constrained by the current field sampling; sampled curve transitions
and some deliberately carved fold patterns remain visible. The additional leaf
count alone is not a quality verdict.

## Measured costs

RTX 5070 Ti / driver 610.57.04, Release binary, 1920×1080 at 1×, hard shadows,
180 frames / 30 warmup / 148 completed GPU samples. Steady costs sum tracing,
material shading and shared post. The owned prefix-validation process was
temporarily suspended for all three profiles and resumed in a `finally` block;
geometry validation and profiling did not compete for the GPU.

| Version | Leaves | Cold field build | GPU median / p95 | Active bricks |
| --- | ---: | ---: | ---: | ---: |
| Previous guardian | 106 | 95.515 ms | 2.333 / 2.450 ms | 107,395 |
| Organic form | 197 | 176.859 ms | 2.848 / 2.956 ms | 106,175 |
| Organic detail | 1,011 | 912.261 ms | 11.023 / 11.337 ms | 106,643 |

These are bounded content measurements, not a normalized per-primitive cost.
Geometry changes still rebuild the complete authored field. The offline film
visits every leaf, while live playback can skip reveals when rebuild time
exceeds their allotted presentation interval. Manual step/seek remains available.
Rear work is mostly hidden during the fixed front build; use the rear still and
final orbit to inspect it. This is a scripted rendering study, not a qualified
real-time thousand-stroke brush editor.

Receipts: `outputs/sdf/guardian-organic-performance.json` and the raw profiles,
commands and logs under `outputs/sdf/guardian-organic-profile/`.

## Reproduction

```sh
cmake --preset release
cmake --build build/release --target cubey_project_sdf cubey_project_sdf_authored_tests -j8
python3 projects/sdf/tools/author_guardian.py --stage form
python3 projects/sdf/tools/author_guardian.py --stage final
build/release/projects/sdf/sdf --config projects/sdf/presets/sculpture-guardian-organic-build.json
```

Use `capture_guardian.py --output ...` for matched images. The history qualifier's
receipt records the exact selected-still/preview invocation and native binary.
