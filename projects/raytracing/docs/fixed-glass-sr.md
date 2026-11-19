# Experimental fixed glass reconstruction

`--experimental-glass dragon|sphere` enables the comparison on the RT `demo` transport. The dragon uses the certified optical shell and its marble support. The generated fixture places the workshop’s closed clear-glass sphere in front of marble slats with visible sky between them. Both use the canonical demo glass recipe, atmosphere, smooth optical normals, scene reflections and transmission, disabled shadows, and the existing branch, event, ray and radiance caps.

The regular `dragon_demo.py` launcher keeps the certified glass dragon on its
native path by default. Pass `--upscaler dlss-quality` to select the experimental
dragon K route; it is accepted only for the launcher's fixed RT mode and expands
to `--experimental-glass dragon --upscaler dlss-quality`. The launcher does not
offer the generated sphere as a showcase selection. Use its `--model` option to
point at a certified optical model prepared in another checkout.

`--upscaler native` traces the full output resolution. `spatial-quality` traces at two-thirds dimensions, then linearly samples the HDR color for presentation. `dlss-quality` queries NGX for its Quality extent and evaluates Super Resolution preset K before the same display transform. All three use the same two or four fixed primary samples per render pixel (`--demo-samples`, default four). Each frame starts fresh in the RT integrator.

For K, each fixed primary lane receives the same temporal jitter. Lane zero writes the actual front surface's Vulkan depth in R32F and previous-minus-current motion in render pixels in RG16F. A sky miss writes far depth and rotation-only motion. The raygen projects the certified mesh's translated trace coordinates through the current and previous unjittered matrices. The beauty alpha remains 1. The historical diagnostic HDR alpha still contains the transport status bits. K receives scene color, depth, motion and jitter. Reflected and transmitted features can move differently from the glass surface used for reconstruction.

From the repo root, use a Release build configured with the developer-provided NGX SDK and the [prepared `optical-v1` model](../README.md#build-and-run). Set `MODEL` to the absolute path if you prepared it in another checkout. This example writes matched 1080p, 96-frame, four-sample clips of the dragon.

```sh
BIN=build/dlss/projects/raytracing/raytracing
MODEL=cache/raytracing/stanford-dragon/optical-v1/dragon-optical.glb
mkdir -p outputs/glass-study
for mode in native spatial-quality dlss-quality; do
  "$BIN" --config projects/raytracing/presets/optics/dragon-demo.json --model "$MODEL" \
    --experimental-glass dragon --upscaler "$mode" --demo-samples 4 --anti-aliasing off \
    --glass-sequence --headless --capture video --frames 96 --fps 30 \
    --width 1920 --height 1080 --require-validation \
    --output "outputs/glass-study/dragon-$mode.mp4" --metrics "outputs/glass-study/dragon-$mode.json"
done
```

For the sphere, replace `--config … --model … --experimental-glass dragon` with `--scene dragon-optics --transport-mode demo --no-shadows --experimental-glass sphere --yaw 0 --pitch .08 --distance 1.12`. It uses generated geometry and requires no external model. For render-only timings use `--headless-render-only --profile-output <prefix>` in place of `--capture video --output <file>`. For a single still omit `--glass-sequence`, `--frames`, `--fps` and `--capture video`, and supply a PNG output path. A one-frame K still shows freshly reset history.

The 96-frame camera holds for frames 0–11, orbits by +0.24 radians on frames 12–35, stops for 36–47, returns along the same arc on 48–71, and holds the starting pose on 72–83. Frame 84 cuts to −0.4 radians and resets K history. Frames 85–95 hold the new view. Other lengths divisible by eight and at least 16 frames use the same proportions. The metrics record each submitted frame's phase, render/output dimensions, ray counts, history age, reset reason and jitter in `glass_accepted_frames`.
