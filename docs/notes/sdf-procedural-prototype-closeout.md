# SDF procedural prototype closeout

Date: 2026-10-21

Status: settled after the final camera pass. The user chose to stop polishing
the procedural test object and retain it as a tech demo/prototype. Stronger
visual results would need a separate content-authoring effort.

[Final 24-second video, opening comparison and 2k/4k stills](http://starship:8787/reports/1db7103651a39de3b013aabf8424bbcb06e0a236627b8af3c32dcb9306dc6aa1/).

## Settled presentation

Use `projects/sdf/presets/negative-stick-ball-dense.json`: seed 42, sphere
radius 0.30, width 0.024, 4,096 cuts, and the `intro-bores` treatment. Four
long, inward-shifted bores introduce the operation; the remaining cuts retain
the shorter stick recipe. `--count 2048` selects the broader surviving form.
The two-second hold, four slow cuts, accelerated construction and final
half-orbit remain unchanged. Hard self-shadows, shared atmosphere, no ground.

The previous camera was deliberately aligned down the first bore to show its
exit. That produced an oddly flat sky-colored diamond. After auditioning six
nearby offsets, the final pass changes only preset yaw from
0.6662341301943412 to 0.6062341301943412 radians, about 3.4 degrees to one side.
An illuminated inner wall is visible in the opening. The through-hole geometry
is unchanged even though this view no longer looks directly through its exit.

## Verification

Camera commit `10573c7a` is the clean source snapshot for
`outputs/sdf/oblique-closeout/manifest.json`. The executable and all nine
compiled shader hashes match the previous dense-bore qualification. All six
native runs require Vulkan validation and exit successfully: intact, one cut,
four cuts, 2k, 4k, and the 4k carving video. Exported recipes match the earlier
qualified recipes byte for byte, including the final video recipe.

FFprobe confirms 720 H.264 frames at 1280x720, 30 FPS, 24 seconds. Native
opening/final captures and decoded video stages were inspected. This is a
camera-only follow-up; the previous [geometry qualification](sdf-dense-bore-evidence.md)
is retained without rerunning the CPU oracle or making a new performance claim.

The report passes isolated Chrome checks over local no-Range HTTP and the
published URL: seven decoded chapter seeks, playback, both count controls,
native image decoding, local links, a 390px layout and no JavaScript errors.
Receipts: `outputs/sdf/oblique-closeout-browser-review.json` and
`outputs/sdf/oblique-closeout-delivery.json`. The publisher separately verifies
HTTP 200 and entrypoint bytes. No user-browser acceptance is implied.

## What remains useful

The retained prototype demonstrates the ProjectR-to-Cubey port, ordered
additive/subtractive recipes, cached distance bricks, construction playback,
mapless self-shadows and atmosphere integration. Its source review, commit
history and evidence document the learning journey.

The random carved ball is a test object. It does not establish an impressive
authored sculpture, an exclusive SDF effect or an advantage over mesh Boolean
systems. Dense updates remain synchronous and detached remnants remain in
place. Better content, authoring tools or learned generation are future
experiments; this closeout adds no work to those directions.
