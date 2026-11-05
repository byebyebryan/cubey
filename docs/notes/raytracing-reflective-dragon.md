# Primitive optical proofs, then a reflective dragon

Date: 2026-11-05. Runtime `421708f9`, following `ef88fe73`.

## Intended progression and implementation

The user expected a simple ball/box proof followed by a reflective dragon.
The mixed clay-dragon / mirror-sphere / colored-block arrangement conflated
those two stages. The current front door separates them:

1. `proof-reflection.json` and `proof-refraction.json` expose reflection and
   closed-glass transport using generated primitives, without downloaded data.
2. `reflective-dragon.json` uses `reflection-dragon-solo`: the dragon itself
   reflects, with only a small matte support base in the scene. No sphere,
   colored block or glass prism accompanies it.

The new scene has two meshes/instances and three AS builds including TLAS.
The prepared dragon and optical shader identities are unchanged. The base is
scaled to this single subject. Existing proof fixtures and the earlier mixed
presentation remain available for dated evidence/reproduction.

`--subject-material clay|mirror` and the native Dragon surface selector update
the completed slot's material buffer. Both modes use the same opaque AS mask;
changing them does not rebuild geometry. Mirror RGB is reflectance; clay RGB is
base color. Roughness is disabled in mirror mode because the existing scene
transport is sharp. This is an ideal reflective surface, without a new rough
conductor BRDF or stochastic transport implementation. The dragon remains opaque;
glass stays in the closed primitive study.

## Qualification and evidence scope

The native mirror/clay/scene-environment comparisons share geometry, camera,
light and exposure. Owned Xvfb checks exercise both subject modes, clay roughness,
reflection-route changes, save/reload and two resize transitions, retaining
three AS builds and three target generations. Focused RT sync tests pass 10/10.

Final captures, completed GPU timings, default/control HDR comparisons, secondary
hit observations and native video are retained under ignored
`outputs/raytracing/reflective-dragon/`. Their current receipts are linked at
closeout below. This is a single-sample sharp-reflection study; fine-edge
aliasing and deep-path limits retain their explicit scope. Physical desktop,
browser rendering and human artistic acceptance remain separate.

## Completed observations

At 128×96, the diagnostic sample contains 1,525 valid primary dragon pixels,
408 first reflected hits back onto the dragon and 156 first reflected hits onto
the base. Misses are excluded using primary t≥0 before instance/material scoring.
The two primitive proofs and previous mixed reflection preset are PNG-byte-
identical before/after this change. Saved native views reload within one RGB8
level outside the UI. Seven documentation checks pass.

The three serial optimized 1080p runs retain 150 completed samples after 30
warmup frames per process. Total-frame GPU p95 is 0.621–0.636 ms for scene-mirror
transport, 0.576–0.585 ms for environment-only mirror transport and
0.583–0.584 ms for clay. No other owned GPU work overlaps timing.

The default/control comparison scores all 2,073,600 HDR pixels with no edge
exclusions. Mean normalized error is 0.000939%, pixel p99 is zero and maximum
normalized error is 0.726264. Default transport flags 82 event-limited pixels;
the deeper control flags six. Fixed aggregate limits pass, with localized deep-
reflection errors retained. This is not a uniform per-pixel accuracy claim.
The control uses 12 events / 64 radiance calls / 256 total calls and no cutoff;
defaults remain 8 / 32 / 128 with cutoff 1e-4.

[Compact evidence and commands](../evidence/raytracing-reflective-dragon-2026-11/README.md)
retain source, asset, binary/shader, lifecycle and timing identities. The full
normal dev suite is not rerun for this bounded change; the earlier 279/279
receipt retains its original source scope.

## Visual review

[Primitive proof, reflective dragon](http://starship:8787/reports/eda21c11324c7f412342cfca9aafd9673a36d7af655a00cb10e8f22c72ec4d71/).
The report presents primitive proofs first, followed by the reflective dragon,
clay/environment comparisons, diagnostics and native orbit. All 23 bundle files
return HTTP 200 and match their authored SHA-256 bytes; five DOM states pass.
The existing shared report server was reused and left running. Browser rendering
and human artistic acceptance remain separate.
