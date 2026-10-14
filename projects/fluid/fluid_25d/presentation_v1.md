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
