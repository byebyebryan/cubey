# Sampled opaque dragon delivery

Local candidate, 2026-11-07, on `raytracing-first-pass-20261104`, based on
`3c1cda12313d69ff191212b01fd6080843f9c4a5` plus the preserved uncommitted slab work.

[Interactive comparison and native clip](http://starship:8787/reports/52b247a6c27b1b2d7b02aa0300ea1552612065f00d04e9611d5ef109f8e490ed/).
The [results note](../../notes/raytracing-dragon-sampled.md) describes the route,
reproduction commands, ownership boundaries and limits.
[delivery.json](delivery.json) retains compact source and acceptance identities.

Full ignored artifacts remain in `outputs/raytracing/dragon-sampled-20261107/`:
the goal-start source backup, final qualification, native exercise, 1080p
captures, three serial GPU pilots, source identity, browser screenshot/check and
all-file HTTP verification. The published bundle contains only its 19 intended
review assets, not the GLB or complete scene dumps. The shared report server was
reused and remains running.

The focused RT suite passed 18/18; documentation checks passed 9/9. Qualification
covers all ten opaque recipes and the actual pinned mesh, while retaining explicit
sample variance and path-budget limits. The native video targets 32 primary
samples after settling; 1080p bronze stills use 1,024. Iteration GPU timings are
separate from accumulated still cost and native presentation latency.

Original SDF/horse source, canonical material files and the cached GLB are
preserved. The new scene's actual geometry matches the accepted dragon and slab
exactly. The work is uncommitted and has not been pushed or promoted to the public
site. Glass remains gated on optical mesh qualification.

That delivery receipt predates the [commit review](review.md). Its recorded
candidate identities and measurements remain scoped to that delivery.
