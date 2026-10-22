# Guardian: organic forms and a larger sculpt

Date: 2026-10-22

Status: capacity, authoring and the separate 1,011-leaf sculpt are implemented;
native history/presentation qualification is running. See the
[execution evidence](sdf-guardian-organic-evidence.md).

The user selects the guardian, asks for a more organic jaw/chin and skull,
more detail, and a construction closer to 1,000 edits than 100. Driftwood stays
deferred. Preserve the clean 106-leaf guardian as a comparison; author a separate
organic variant. Retain stone material, empty sockets, stern expression, shared
atmosphere and hard self-shadows. No arbitrary edge chips or modeled eye glints.

## Shape and detail

First replace the box-derived jaw with a curved, tapered mandible and a rounded
chin. Build an irregular cranium, temples and occiput that flow into the nape;
the rear should have anatomy rather than read as one sphere. Connect these into
the cheeks, muzzle and shoulders. Review front, profile, opposite and rear views
before adding fine detail.

Use secondary forms and relief that follow that anatomy: socket overhangs,
cheek/muzzle transitions, lips, brow compression, neck tendons and horn roots.
Then add grouped folds and smaller horn/skin relief, leaving quiet areas between
clusters. Avoid decorating the old blockout with hundreds of unrelated grooves.

Target roughly 1,000 explicit primitive leaves within a 1,024-leaf ceiling.
Record coherent strokes separately from their sampled segments: a twelve-segment
curve is twelve saved leaf reveals, not twelve independent sculpting decisions.
Denser sampling must resolve curved forms or detail, rather than pad the count.
More geometry does not establish a better artwork; native images remain the
artistic review gate.

## Necessary capacity work

The previous authored limit is 128 leaves / 256 instructions. Raise that bounded
budget to 1,024 / 2,048; retain tree depth 24, stack 32, 16 materials, field
resolution, brick capacity and 2 GiB field budget. The existing dynamic postfix
shader program supports this without a new primitive or shader stack size.
Program storage grows by 114,688 bytes per allocated program buffer.

History loading currently recompiles every prefix just to reject a tool that
appears before its body. Replace that quadratic load-time work with a tree
dependency check. At each subtraction, the earliest body leaf must precede
the earliest tool leaf. Recursive enforcement includes nested negative tools.
Compare this rule against exhaustive prefix compilation for all 720 permutations
of a six-leaf nested fixture; retain native prefix evaluation as an independent
runtime check.

## Evidence and checkpoints

1. Capacity boundary, overflow, GPU packing and large-history seek/reset tests;
   native 1,024-leaf last-instruction fixture and legacy guardian image parity.
2. Native organic form audition, then anatomically grouped detail. Preserve the
   earlier guardian inputs and images.
3. Final solid/hollow design probes, native ray/material/part checks, every-prefix
   field validation, export/reload, static/replay equivalence and matched views.
4. A construction presentation long enough to visit each leaf without skipping.
   Large-study capture selection must be explicit: do not describe selected
   stills/previews as an exhaustive image or cutter-visibility matrix. Native
   prefix checks and the film's visit/build checks remain exhaustive.
5. Publish a focused guardian comparison, record measured costs and remaining
   visual limits, and commit the reviewable checkpoints locally.
