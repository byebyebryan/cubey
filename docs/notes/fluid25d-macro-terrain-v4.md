# Fluid 2.5D bare macro-terrain and rainfall V4

Review: `outputs/fluid/macro-terrain-v4-20261007-study/index.html` and
`RESULTS.md`. This builds on the retained terrain V3 study. Human visual
acceptance of the terrain studies remains deferred; they are opt-in.

## Target and decision

The target is a bare, kilometre-scale mountain landscape. Vegetation, clutter,
close-up soil detail and photorealism are outside this pass. Readable remains
a first-class visualization and the launcher default. Scenic still defaults
to refined; the new `macro` terrain material is explicitly opt-in.

The selected change replaces V3's five-direction ambient softening with a
cosine-integrated diffuse environment, for terrain only. It is a modest
improvement to the overly bright ambient cliff bands, not a complete cure for
the full rainy scene's glossy/rubbery appearance.

Component controls separate terrain albedo, detail normals, direct lighting,
ambient lighting, specular and shadow contributions with water/dots hidden.
Removing terrain specular barely changes these views. Removing shadows erases
hard dark bands but loses grounding; reduced shadow strength and slope-color
contrast were auditioned and not selected. Detail-normal removal is subtle
at macro cameras. Only the diffuse integration is selected by `macro`.

Bright streaks persist in the full water/terrain composite but disappear with
water hidden. Together with the retained water shader, this points to thin-film
water reflections as another contributor to the coated-ground appearance.
The next rendering study should isolate that film presentation, rather than
continue raising ground roughness or inventing terrain detail. No water-film,
optics, coverage or shoreline changes were made here.

## Implementation boundaries

`generate_generated_diffuse_irradiance` reuses Cubey's established Hammersley,
cosine-hemisphere and tangent-frame sampling. It convolves the retained
generated environment's ambient response, including its legacy gain/floor;
it is not a newly calibrated radiance model. The stored result is `E/pi`, with
no additional pi factor. A 32-pixel, six-face RGBA32F cube uses 96 KiB and 256
samples per texel, generated lazily on first opt-in use rather than per frame.
An independent uniform-sphere quadrature checks the normalization and faces.

Water keeps its original environment bindings. The four appended terrain
uniforms are isolated from water/sky shader declarations, preserving the
original uniform prefix and compiled water/geometry/numerical shaders.

The retained v2 tuning schema adds four bounded values (all 0..1):

- `terrain_diffuse_convolution`: legacy 0; macro 1.
- `terrain_specular_scale`: default 1.
- `terrain_shadow_scale`: default 1.
- `terrain_slope_color_scale`: default 1.

Macro inherits every other terrain V3 setting, except
`terrain_ambient_softening=0` because the integrated cube replaces that
approximation. JSON omission retains old defaults. Terrain component views
append `no-specular`, `no-shadows` and `specular-only` without renumbering
existing controls. The GUI exposes these controls and the opt-in preset.

No solver/service mathematics, immutable recordings, terrain geometry/scale,
water optics, bank reconstruction/coverage, dot/trail policy or texture assets
were changed. B-spline remains an experimental option, not a promoted default.

## Rainfall UI

Live rainfall is now near the top of the panel: active uniform supply in mm/h,
an editable intensity, explicit Apply and pending/acknowledged status. Zero
stops new rainfall; existing water can continue flowing and draining. Rain
intensity is depth supplied per physical hour, not water depth or time scale.
Solver pacing is labeled separately.

Typing is local and remains available across paused heartbeat updates. Apply
still requires healthy, nonterminal, nonbusy solver authority and a finite,
nonnegative edited value; the backend health policy and command protocol are
unchanged. During an in-flight command, text entry is briefly locked so its
acknowledgement cannot overwrite a newer draft. Replay panels show recorded
rate/phase and scheduled total near the
top, explicitly read-only. Live does not invent a cumulative-rain total.

The private Xvfb test uses actual mouse/keyboard entry for 0, 120, 240 and 0
mm/h, checks wire commands and native applied acknowledgements, and advances
at least 180 physical seconds per stage. It also tests pause/resume, Readable
comparison, resize, detach and owned worker cleanup. This is bounded automated
GUI compatibility, not human approval or indefinite live-solver acceptance.

## Reproduce and review

```sh
rtk proxy python3 projects/fluid/fluid_25d/run_mountain_rain_demo.py replay --style scenic --material macro --camera runoff --start 6000
rtk proxy python3 projects/fluid/fluid_25d/run_mountain_rain_demo.py live --style scenic --material macro
```

Replay inspects retained Terrain Diffusion recordings; Live starts a fresh dry,
paused external SynxFlow run. Neither command imports surveyed terrain.

The compact review contains two eight-second paired clips, four paired stills,
independent lighting controls, an alternate heading, a camera-only held-state
sweep and actual rain-control screenshots. Final numerical parity, full tests,
matched GPU timings and retained development failures are in `RESULTS.md` and
`acceptance.json`; the review runner seals source/runtime/input/media hashes.
Timing excludes simultaneous CUDA and first-use generation. No defaults or
human visual acceptance are promoted by passing automated checks.

The matched RTX 5070 Ti runs give macro median/p95 of 0.638/1.679ms at
720p and 0.470/1.646ms at 1080p; worst 720p batch p95 is 1.681ms, under
the 4ms ceiling. V3 tails are lower in these batches. No negligible-overhead
or consistent median/tail improvement is claimed; keep this limitation with
the opt-in visual result.

The original study loop left these changes uncommitted. The later commit
review retains this historical evidence without promoting the terrain preset.

Final validation: all 170 dev tests pass without skips; 11 V3 reference stills
and 7 Readable/raw controls retain byte parity, 41 existing compiled shaders
and native inputs retain byte parity, and both real private GUI exercises pass.
