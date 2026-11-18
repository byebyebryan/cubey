# Fixed opaque dragon temporal AA

`--transport-mode fixed-opaque` is an opt-in, per-frame RT path for the
existing `dragon-materials` route: the original bronze dragon over its opaque
marble support. The matched presets are
[`dragon-fixed-opaque-off.json`](../presets/optics/dragon-fixed-opaque-off.json)
and
[`dragon-fixed-opaque-taa.json`](../presets/optics/dragon-fixed-opaque-taa.json).
Both issue four primary lanes per accepted frame by default; set
`--fixed-opaque-samples 2` on both launches for the two-lane match. Progressive
`--samples` and glass `--demo-samples` do not select these lanes; non-default
values for either are rejected.

The mode rejects glass, non-final views, recipe/material edits, alternative
support or lighting paths, raw diagnostics and forward rendering. TAA is
admitted only with fixed-opaque transport. Shared `--anti-aliasing` policy
defaults to `auto`: an interactive fixed-opaque run requests TAA, while
headless Auto keeps the direct path. Other RT transport modes and the forward
renderer keep their existing path under Auto. The Off preset is the certified
direct-render comparison. Use explicit `taa` with fixed-opaque transport for
headless TAA captures. If the selected device lacks native TAA support, Auto
selects the fixed Off pipeline and reports the device bypass reason on its
first accepted frame. Explicit `taa` requires a TAA-capable device.

TAA adds one accepted-sequence jitter to each existing spatial lane. It does
not replace the first off-center lane with a center ray. The first actual
primary hit exports its world position; a miss exports its world ray direction
with `w=0`. Both project through the unjittered current/previous view matrices
into a native-size RG16F previous-minus-current motion image. No motion rays or
depth approximation are added. Sky radiance uses the same primary rays as the
opaque path.

The producer and the direct GPU regression share
[`fixed_opaque_motion.glsl`](../shaders/fixed_opaque_motion.glsl). The focused
`raytracing_fixed_opaque_motion_vectors` test dispatches that production GLSL
function on a compute queue and compares its hit/sky vectors with independent
expected pixels for stationary jittered hits, both geometry-motion signs, sky
translation and sky rotation. The fixture uses synthetic inputs. The
model-backed render exercises the raygen hit/miss lookup and the motion image
write in `material_workshop.glsl`.

## Deterministic headless sequence

`--fixed-opaque-sequence` drives camera yaw from the host frame index, so Off
and TAA video runs use identical hold, orbit, stop, cut and post-cut poses. The
short form is 14 frames of headless render-only output. The visual form is 96
frames at 30 Hz and writes a video. Both record accepted submission tickets,
primary dispatch counts and, for TAA, history age, reset reason and jitter in
the metrics JSON.

The original prepared model is outside this checkout. Use the protected cache
copy below; it is read-only and its expected SHA256 is
`3f9dc36d34ce148e92ac4d096339a5cc48a9c4a86126b9963b052676a8a2260e`.

```sh
MODEL=/home/bryan/code/cubey-raytracing-first-pass/cache/raytracing/stanford-dragon/dragon.glb
BIN=build/release/projects/raytracing/raytracing

"$BIN" --config projects/raytracing/presets/optics/dragon-fixed-opaque-off.json \
  --model "$MODEL" --headless --headless-render-only --frames 14 \
  --fixed-opaque-sequence --width 640 --height 360 --require-validation \
  --metrics outputs/fixed-opaque/off-short.json \
  --profile-output outputs/fixed-opaque/off-short-profile

"$BIN" --config projects/raytracing/presets/optics/dragon-fixed-opaque-taa.json \
  --model "$MODEL" --headless --headless-render-only --frames 14 \
  --fixed-opaque-sequence --width 640 --height 360 --require-validation \
  --metrics outputs/fixed-opaque/taa-short.json \
  --profile-output outputs/fixed-opaque/taa-short-profile
```

For reviewable motion clips, use the same flags with the long form and separate
output paths:

```sh
"$BIN" --config projects/raytracing/presets/optics/dragon-fixed-opaque-off.json \
  --model "$MODEL" --headless --capture video --frames 96 --fps 30 \
  --fixed-opaque-sequence --width 1280 --height 720 --require-validation \
  --output outputs/fixed-opaque/off.mp4 --metrics outputs/fixed-opaque/off.json \
  --profile-output outputs/fixed-opaque/off-profile

"$BIN" --config projects/raytracing/presets/optics/dragon-fixed-opaque-taa.json \
  --model "$MODEL" --headless --capture video --frames 96 --fps 30 \
  --fixed-opaque-sequence --width 1280 --height 720 --require-validation \
  --output outputs/fixed-opaque/taa.mp4 --metrics outputs/fixed-opaque/taa.json \
  --profile-output outputs/fixed-opaque/taa-profile
```

The fixed sequence phase boundaries are hold frames 0–11, orbit 12–47, stop
48–71, cut frame 72, then post-cut hold through frame 95. The cut moves from
the orbit endpoint to the same final pose in both runs and explicitly resets
TAA. The 14-frame fixture uses hold 0–2, orbit 3–7, stop 8–10, cut 11, and
post-cut hold 12–13.
