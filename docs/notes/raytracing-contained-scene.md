# Ray tracing contained scene

Date: 2026-11-05. Runtime `01dd5396`, following the qualified optics source
`dd432598` and delivery checkpoint `92aef33c`.

The later [reflective-dragon follow-up](raytracing-reflective-dragon.md) supersedes
this mixed dragon/prop presentation: primitives stay in optical proof scenes,
then the dragon itself becomes reflective in a separate showcase. This page
retains the dated base/layout experiment and its measurements.

## Result

The dragon presentation no longer uses the broad ground plane. It sits in a
contained arrangement on a closed, matte, bevelled base with visible sides and
sky around its outline. Smaller colored props and the mirror/prism fit within
that footprint. The overview framing lowers the camera and leaves the full base
in view. This follows the user's preference for other objects or a constrained
base while keeping the ground from defining the scene's overall feeling.

The base measures 2.48×1.34 metres for reflection or 2.48×1.66 metres for glass,
and is 0.16 metres thick. The source dragon is unchanged. Material, instance and
AS ownership, shared lighting, optical shaders, transport budgets and host paths
retain their existing contracts. No new terrain or environment renderer is added.

Only `reflection-dragon`, `refraction-dragon` and the three optical presets change.
The generated mirror/slab/prism fixtures keep their original floor and off-screen
marker for independent numerical checks; the legacy opaque scenes keep their
original geometry. The original [optics results](raytracing-optics-results.md)
and report remain dated evidence rather than being relabeled as this composition.

## Native review and qualification

Native captures compare the previous floor arrangement with the revised scene
at identical camera, sun and exposure. Base material, prop layout and framing
are deliberate presentation changes; these comparisons do not isolate one
physical parameter. Scene/environment transport comparisons retain identical
geometry and material settings within this revised source.

The complete capture, convergence and serial timing packet is retained under
ignored `outputs/raytracing/contained-scene/`. Its source/binary/SPIR-V/preset/model
identities distinguish it from the earlier floor-based packet. The focused RT
sync checks pass 10/10. The generated reflection/refraction/slab fixtures and legacy opaque dragon are
PNG-byte-identical before/after. Owned Xvfb controls/save/resize pass with seven
AS builds and three target generations. The four-second native orbit retains
seven AS builds. No full dev-suite rerun is claimed for this scene-only change;
279/279 remains the earlier optics checkpoint. Physical desktop, browser layout
and human artistic acceptance remain separate.

## Updated costs and transport limits

Three serial optimized 1080p runs per view/route retain 150 completed samples
after 30 warmup frames. No other owned GPU work overlaps timing. Validation and
raw diagnostics are disabled for timing; native comparison stills enable sync
validation. Source, binary, shader, preset and model identities are retained.

| View | Environment total-frame p95, ms | Scene total-frame p95, ms |
| --- | ---: | ---: |
| reflection | 0.583–0.584 | 0.588–0.588 |
| curved-mirror | 0.634–0.635 | 0.652–0.654 |
| refraction | 0.657–0.658 | 0.711–0.712 |

Moving-sun total-frame p95 is 1.433–1.446 ms, including probe updates.
These are costs of the new composition; camera coverage, materials and prop
placement changed, so they are not an isolated renderer optimization result.
Shared-host boundary observations do not prove continuous exclusivity.

The two mirror views match the deeper control exactly across all 2,073,600 HDR
pixels and contain no termination flags in these captures. The prism still uses
bounded transport: normalized mean error is 0.002412%, pixel p99 0.006658% and
maximum error 0.071565. The default prism capture has 3,400 event-limited pixels,
101,545 cutoff pixels and seven medium-state rejections. All fixed aggregate
convergence limits pass; a uniform per-pixel correctness claim is not made.

[Evidence and reproduction receipts](../evidence/raytracing-contained-scene-2026-11/README.md)
retain the exact scope and original comparison identities.

## Visual review

[Before/after comparison and native orbit](http://starship:8787/reports/2b982da05790b498e6954d21fb437f636c227c45085aee0d035f8dac128aba5a/).
The revised base is the initial view; the previous floor is available at the same
camera/light settings. All 27 published bundle files return HTTP 200 and match
the authored SHA-256 bytes; 12 UI state checks pass. Browser layout and human
artistic acceptance remain separate. The existing report server was reused.
