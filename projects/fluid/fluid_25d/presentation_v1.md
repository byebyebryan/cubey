# Fluid 2.5D presentation promotion V1

The active demo is now accompanied by an eight-second cinematic highlight and
a 36-second project film. They use the accepted Mountain 1024²/16m scene,
unchanged generated elevation, native recorded SynxFlow depth/momentum and the
current Scenic/B-spline rendering. This is a presentation/documentation pass,
not another solver, terrain, material, asset, backdrop or hydrology study.

## Selected treatment

The initial wide runoff framing exposed the finite patch edge against the sky.
An interior 1.8 km stream-network viewpoint keeps the water readable. The
highlight uses a 0.35-radian (approximately 20°) eased arc at fixed distance;
the main simulation cameras and legacy study sweep/zoom remain unchanged.

Four matched auditions compared fixed/moving cameras and water-only/dots.
Water-only is the agent-selected showcase treatment because rain, growing
channels and whitewater already carry the motion without diagnostic clutter.
Dots/trails remain available and retain their live-demo default. The browser
audition supports synchronized playback and matched native contact sheets.

The 36-second film uses deliberate cuts rather than a continuous fly-through:

| Screen interval | Physical interval, approximate | Camera / story |
| --- | --- | --- |
| 0–6 s | 0–0.5 h | Interior macro terrain; rain supplies the patch. |
| 6–16 s | 0.5–1.83 h | Eased stream-network view; downhill runoff develops. |
| 16–24 s | 1.83–2.5 h | Fixed collection view spanning acknowledged rain shutoff. |
| 24–36 s | 2.5–6 h | Fixed runoff view; streams retreat after rain stops. |

Cuts never reverse physical time. Actual endpoints are snapped to the existing
native saved timestamps and recorded alongside intended cue times. Rain turns
off at the study's actual applied boundary, approximately 7,203.878 seconds;
remaining collections can still grow briefly as upstream water arrives.
This is not a certified calm/permanent lake or a realistic 512 mm/h storm.

## Capture contract and provenance

The selected finals were rendered from clean source revision
`f3a0e95c7bba4a09540edb00bef04d12d76ec659`, with Release renderer SHA-256
`eca1d3f8248c0af7e95a649f8be01a8e9d6872406376e15530da7d4d548ff332`.
The recipe records all 61 shader hashes, recording/case/source identities,
exact CLI arguments, intended/actual shot intervals, native state counts,
postprocess commands, decoded media probes and owned-process cleanup.

The highlight has 480 native rendered frames and 65 distinct saved hydraulic
states. The four project-film segments have 28/74/39/195 saved states across
360/600/480/720 rendered frames. States are held between their original saves;
none are physically interpolated. Camera and rain/whitewater animation do not
constitute additional hydraulic states or native water parcels. Encoded 60 FPS
does not claim 60 Hz hydraulics, real-time CUDA throughput or display scanout FPS.

Both publications are silent H.264 High / yuv420p, 1280×720, exact 60 FPS CFR,
libx264 medium CRF 24, with fast-start metadata. Every master output frame is
rendered natively. The shared native encoder's last packet has no frame duration;
the explicit publication encode gives the existing frame sequence normal CFR
packet timing. It neither interpolates nor manufactures additional frames.
Raw higher-quality masters and native field archives remain ignored inputs.

Committed files and byte hashes are in the
[showcase manifest](../../../docs/media/showcase/manifest.json) and
[full recipe](../../../docs/media/showcase/fluid-25d-recipe.json).
No existing showcase MP4, poster, attachment URL or entry identity was changed.
No GitHub attachment upload or push was performed. Owner creative approval
and native GUI interaction/layout acceptance remain distinct pending gates.

## Reproduce with existing qualified inputs

See [setup](demo_setup_v1.md) before attempting a fresh checkout. These commands
do not install dependencies, regenerate terrain or run the solver. Each output
must be a fresh leaf directly under `outputs/showcase`.

```sh
rtk proxy mkdir -p outputs/showcase
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v1.py audition \
  --study outputs/fluid/site-expansion-20261011-jtIA3s \
  --recording outputs/fluid/rain-response-v1-20261014-a2 \
  --out outputs/showcase/fluid25d-audition-FRESH
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v1.py hero \
  --study outputs/fluid/site-expansion-20261011-jtIA3s \
  --recording outputs/fluid/rain-response-v1-20261014-a2 \
  --out outputs/showcase/fluid25d-hero-FRESH
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v1.py story \
  --study outputs/fluid/site-expansion-20261011-jtIA3s \
  --recording outputs/fluid/rain-response-v1-20261014-a2 \
  --out outputs/showcase/fluid25d-story-FRESH
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v1.py assemble \
  --study outputs/fluid/site-expansion-20261011-jtIA3s \
  --recording outputs/fluid/rain-response-v1-20261014-a2 \
  --out outputs/showcase/fluid25d-story-FRESH
```

`package_presentation_v1.py --hero HERO_ROOT --story STORY_ROOT --out FRESH_ROOT`
rechecks media, clean capture revision and matching renderer/recording/scene
identities, then creates a focused static export. `--package-media` creates only
absent named repository artifacts; it refuses to replace the approved package.
Capturing the exact extension-hash/source-data profile on another machine is
not promised by the presence of a renderer-only checkout.

## Qualification

- Fifteen scoped CPU groups passed after adding current-entry docs/link tests;
  three hardware Vulkan GPU groups passed (recording immutability, startup and
  clear-sky daylight). With the committed-media gate, the final scoped run is
  sixteen passing CPU groups plus the same three passing GPU groups.
- The existing fixed-daylight reference capture stayed byte-exact:
  `ff00524503d877f18de6ccd7dba00fccef7d1e6a1210d729b143359640315464`.
- Every captured clip received bit-exact native bed/depth/decoded-velocity GPU
  import audits, monotone advancing saved-time receipts and clean owned shutdown.
  These are upload/identity checks, not a new native conservation certificate.
- A fresh one-case preparation reproduced the qualified native bed and input
  identity, then passed read-only preflight and a bounded 25-second headless live
  manual-start/pause check using explicit qualified worker paths. Missing other
  cases stayed disabled. No install or arbitrary-build qualification was inferred.
- The previous 2,427-line README body was preserved exactly in
  [the development record](README_HISTORY.md); old section anchors retain entry
  points. The primary README is now a current entry, not a chronological study log.
- Browser playback, decoded dimensions/durations, midpoint inspection and play
  after seeking passed. The unchanged simple report server does not supply HTTP
  range requests; the small review clips use a client-side Blob fallback for
  seekable inspection. No server, firewall or global hosting changes were made.

Local evidence: `outputs/showcase/fluid25d-audition-20261014-a4`,
`fluid25d-hero-20261014-a2`, `fluid25d-story-20261014-a2`, and
`fluid25d-presentation-20261014-v1`; explicit setup proof is in
`outputs/fluid/fluid25d-presentation-setup-20261014` and
`fluid25d-presentation-setup-smoke-20261014`. Failed/earlier auditions are
retained separately, not silently overwritten or claimed as final qualification.

[Review the selected films](http://starship:8787/reports/c3198c8ff67fa60e7997d396a5a6fee6e1aae6081f97f22ca928ea83e6b9b803/).

## Selected Joining valleys sunset cut (promotion pending)

The later owner-selected candidate is Joining valleys at a 15° downward view,
13.5 km distance and 0.60-radian eased orbit. The original Mountain showcase
files above are still unchanged. `patch_presentation_refinement()` in
`present_fluid25d_v1.py` describes the opt-in candidate; it does not replace
`hero()` or alter interactive demo defaults. The owner selected the sunset
variant below after reviewing the native 1080p comparison; creative selection
is complete, but the committed gallery still contains the earlier Mountain cut.

The eight-second refinement renders all 480 frames rather than padding a still:
24 opening frames, 336 advancing frames, and 120 closing frames. Only the
recorded physical-time requests are held at the endpoints. Camera, rain and
cosmetic surface clocks continue at presentation time, independently of the
accelerated approximately six-hour hydraulic timeline. The saved depth and
momentum planes are neither advanced nor interpolated during the finish.

The native capture-only options are `--fluid25d-recording-opening-frames` and
`--fluid25d-recording-closing-frames`; both default to zero and require an
explicit headless recorded video with at least two active frames. The recipe
rounds the float32 physical frame interval upward so the final request reaches
the last native save instead of landing just before it. This includes all 72
saved states of the selected archive; the earlier 15° cut displayed 71.

`--fluid25d-native-camera-vertical-shift 0.06` moves the composition upward by
six percent of frame height without changing the orbit, camera elevation,
terrain scale or water fields. A Scenic tuning JSON using schema
`cubey.fluid25d.scenic-material.v2` and `"rain_macro_softening": 1` enables
distance, top/perimeter and sky-background rain fades in the shared terrain
patch renderer. Zero preserves the accepted rain treatment. This changes
visible streak opacity only, not rainfall rate, coverage or water agitation.
The reusable refinement recipe includes the tracked
[soft-rain preset](fixtures/presentation-v1/soft-rain.json), byte-identical to
the tuning used in the approved capture; ignored output files are not required
for this setting.

The lighting audition uses the existing shared environment with manual sun
elevation 22° and azimuth −75°. It emphasizes ridge relief but creates broad
wet-ground glare, so daylight was retained as the reference for the subsequent
solar-clock audition rather than selecting this manual side-light setup.
The baseline cut remains byte-exact with all new controls disabled. This is
rendering/editorial evidence, not a new live solver or performance study.
Native inputs, shaders, binaries, capture receipts and audition variants are
retained in `outputs/showcase/fluid25d-presentation-refinement-20261014-a1`.

### Solar-clock addition

`patch_solar_refinement()` keeps the same 480 water-state requests, orbit,
framing, rain fades and 1080p output, and adds the existing shared solar clock.
It moves from 15:30 to 18:00 over the eight-second presentation (day 80,
latitude 45°, fixed azimuth offset −95° to suit the shot). This is an authored
lighting interval, independent of the approximately six-hour water recording,
not a claim about the generated terrain's actual location or storm time.

The clock continues through the animated endpoint hold. Sky, terrain/water
lighting and reflections follow the shared environment; no video color fade,
new shader or solver work is introduced. Existing twilight strength 0.18 and
horizon warmth 0.25 reduce the twilight emphasis. A first trial ending at 18:12
with default twilight was retained separately because it produced excessive
horizon color and reduced water readability. The remaining stylized sunset
horizon is a known visual tradeoff, not a new atmosphere task for promotion.
No runtime default is changed.

The [owner-selected sunset comparison](http://starship:8787/reports/c40797df0862afd1461f80ad9b11354c1e91b7464902b84d8c7710c465679cde/)
uses `outputs/showcase/fluid25d-solar-refinement-20261014-a2`: native 1920×1080,
480 frames / eight seconds / 60 FPS, H.264 High / yuv420p / CRF 20, with all
72 original hydraulic saves represented. Water requests and the orbit match
the daylight refinement exactly. Capture-time state/rain/camera receipts,
renderer/shader/input hashes and owned-process cleanup are retained there.

The October 15 review added the tracked tuning preset and rejection checks for
non-finite/future capture timestamps and non-finite media duration. All nine
focused CTest groups and 22 presentation/docs/media unit cases pass. A fresh
hardware-headless 1080p capture using the tracked preset reproduces the
owner-selected sunset MP4 byte-for-byte; review evidence is in
`outputs/showcase/fluid25d-review-20261015-c3egw8`. No packaged media was replaced.

### Promotion plan (not executed)

The supported selected-cut entrypoint is now `present_fluid25d_v2.py`; V1
commands above remain Mountain-only compatibility tools. The V2 command checks
the selected archive's protocol/recording/result identities, every retained
native frame, dry opening, original field audit and numeric/source beds. It
requires a fully clean Git worktree (including index and untracked files),
freezes runtime/shader/source identities, and refuses a final master that is
not byte-identical to the owner-selected sunset. No ignored audition script
is imported. Use existing optional artifacts explicitly:

```sh
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v2.py capture \
  --case outputs/fluid/site-expansion-20261011-jtIA3s/cases/joining-valleys-patch-1024-16m \
  --recording outputs/fluid/site-expansion-20261011-jtIA3s/runs/joining-valleys-patch-1024-16m/recording/recording.json \
  --audit outputs/fluid/site-expansion-20261011-jtIA3s/field-audit.json \
  --out outputs/showcase/fluid25d-sunset-production-FRESH
rtk proxy python3 -B projects/fluid/fluid_25d/present_fluid25d_v2.py package \
  --capture outputs/showcase/fluid25d-sunset-production-FRESH \
  --out outputs/showcase/fluid25d-sunset-package-FRESH
```

Both commands require a fresh output leaf. Packaging rechecks the capture,
clean-source receipt, full rain/camera/solar/hydraulic timelines and native
1080p probe, then creates a standalone browser export. It copies the master
without re-encoding, derives the 720p/60 CRF 24 gallery copy, and extracts its
formed-water poster at six seconds. It does not overwrite repository media,
upload, push, run the solver or install dependencies.

1. **Make the selected recipe a supported capture entrypoint.** Accept explicit
   prepared-case and recording paths instead of the current Mountain-only
   rain-response wrapper. Pin Joining valleys, the solar recipe and the tracked
   rain preset; retain strict source/bed/frame hashes and archive validation.
   Do not turn ignored, one-off review scripts into required dependencies.
   Keep the interactive Mountain demo and existing `hero()` compatibility path
   unchanged; showcase selection is not a solver/default-site promotion.
2. **Freeze a clean-source production capture.** Build from the committed
   renderer/recipe, recheck the existing native archive, then run the same
   hardware-headless eight-second capture. Require every rendered frame,
   monotone water requests, the exact final-state hold, moving presentation/
   solar clocks, bit-exact GPU imports and clean shutdown. Match the approved
   1080p cut byte-for-byte when using the same runtime/encoder; otherwise stop
   for a bounded comparison rather than silently accepting a different cut.
   No solver rerun, terrain generation or native GUI review is needed.
3. **Package one highlight, not a compulsory highlight/story pair.** Update
   the packaging interface to support a single qualified cut and its own
   provenance. Retain the native 1080p master for the project, and derive the
   eight-second 720p60 / CRF 24 gallery publication to preserve the existing
   global showcase contract. Label that downsample explicitly. Select a poster
   with the lake already formed, before the darkest sunset frames. Refresh
   only Fluid's media, byte hashes, dimensions, recipe and publication status;
   retain the four other demos' media/attachment pins exactly. The old 36-second
   Mountain rain-on/off film stays a legacy study/reference, not a required
   companion or a description of the new continuous-rain scene.
4. **Reconcile the presentation surfaces.** Update the root README, Fluid
   README, showcase README/manifest, recipe and this document together. Clearly
   distinguish recorded native water, accelerated physical time, independent
   art-directed solar time and cosmetic motion from live solver performance.
   Preserve source/model/solver provenance and license boundaries using the
   selected archive's identities, not the old Mountain recipe by assumption.
   Remove obsolete creative-review-pending copy for the selected cut while
   leaving public-attachment/push status truthful.
5. **Validate and publish in separate checkpoints.** Test single-highlight
   packaging, clean-source/hash failures, media probes, docs links, original
   showcase pins and default-render regressions; inspect playback/seeking and
   the gallery-size derivative in the browser. Commit the qualified local
   media/docs package first. GitHub attachment upload, wiring the real returned
   URL into the gallery, and a push require an explicit publication request;
   no public URL should be invented and no other highlight should be replaced.

The promotion pass is complete when the selected dry-to-streams-to-lake sunset
cut is the one documented/packaged Fluid highlight, with a legible poster,
reproducible provenance, preserved unrelated gallery media and passing scoped
checks. Further rendering, terrain, solver and hydrology changes are out of scope.
