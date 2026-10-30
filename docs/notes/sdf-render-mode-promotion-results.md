# Selectable SDF modes and Balanced default

Date: 2026-10-29. Plan `a9ab2976`, runtime implementation `fcd08e9c`,
default/root-far promotion `193a16ca`.
[Plan](sdf-render-mode-promotion-plan.md) ·
[progress](sdf-render-mode-promotion-progress.md) ·
[evidence](../evidence/sdf-render-mode-promotion-2026-10/README.md).

[Interactive HTML report](http://starship:8787/reports/b37a56f83a63404aba37029a521824414e0e5c5cecb50423d7b2a60f706fb184/)
includes native mode/sampling comparisons, actual UI captures and motion clips.
The [delivery receipt](../evidence/sdf-render-mode-promotion-report-2026-10.json)
verifies served asset bytes separately from unavailable browser-render and
physical-desktop acceptance.

## Result

Authored scenes and material workshops now default to **Balanced**: cached
primary-hit refinement followed by source CSG normals, retaining cached primary
visibility and secondary sampling. The native panel and config/CLI expose
Cached, Cached refined, Balanced, Analytical and Hybrid. `auto` chooses the
supported default; legacy box recipes retain Cached.

Supersampling is separate in both the panel and CLI. Existing sample defaults
and all 95 preset files are unchanged, including SS2 in 91 presets. There is
no automatic view-dependent quality adjustment, resolution reduction or
preview/settled switching. Explicit original tracing flags retain their old
routes and study admission; conflicting named/advanced settings reject.

The source audit's tentative A16 public analytical mapping proved unsuitable:
completed Guardian observations reached about 135–492 ms. That incomplete
batch is retained as investigation evidence and excluded from final acceptance.
The existing A64 root-far accelerator is now qualified for the public Analytical
atmosphere path, including the native window and authored playback. This was
an integration change to an existing accelerator, not another algorithm search.
Prepared-cone/adaptive studies retain their private scope. Material/optical
analytical rendering and Hybrid retain their existing ordinary A16 backend.

## Final frame costs

RTX 5070 Ti / NVIDIA 610.57.04, 1920×1080 output, frozen atmosphere, hard
shadows, no AO/ground/clouds/occluder. Three serial interleaved runs per
configuration, 180 frames, 30 warmup and 150 verified samples per GPU pass.
The final public-route matrix has 108 runs, after the 96-run sampling study.
Native binary SHA-256:
`a00dfa2527febd2e81984a56980b929be86b19abe4a5d21b9336f63dd28ba98e`.

| Subject / view | Cached SS1 | Balanced SS1 | Analytical SS1 | Cached SS2 | Balanced SS2 | Analytical SS2 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Guardian preset | 1.896–1.936 | 4.605–4.669 | 19.246–19.357 | 4.880–4.908 | 12.052–12.200 | 44.450–44.685 |
| Guardian side | 2.199–2.242 | 3.997–4.033 | 21.681–21.834 | 5.416–5.426 | 9.784–9.914 | 44.141–44.605 |
| Guardian close | 2.642–2.658 | 8.824–8.944 | 35.611–35.904 | 7.208–7.226 | 25.838–26.140 | 77.926–77.984 |
| Robot preset | 4.222–4.254 | 4.312–4.398 | 5.112–5.126 | 13.827–13.874 | 12.222–12.225 | 13.173–13.331 |
| Robot side | 3.615–3.653 | 3.789–3.813 | 3.436–3.460 | 12.080–12.097 | 10.710–10.719 | 10.427–10.458 |
| Robot close | 9.161–9.193 | 7.674–7.705 | 8.808–8.859 | 35.760–35.780 | 30.043–30.072 | 30.315–30.442 |

Numbers are GPU p95 milliseconds. SS1 Balanced passes the preferred 16.7 ms
screen in all six final views; SS2 close views fail the 20 ms screen. These are
explicit cost/quality choices, not a promise of 60 fps at every zoom. Robot
still has views where analytical rendering is competitive or faster. Public
Analytical here uses ordinary root-far with automatic eligible shadow
specialization; it does not inherit the earlier prepared A+B timing.

There is no owned build/test/capture/encoding overlap with final timing or
publication. Receipts retain GPU boundary observations; those do not prove
continuous host exclusivity. Headless GPU route timing excludes UI/present and
physical display latency. Native-window behavior is qualified separately.

## Runtime and compatibility evidence

- Twenty named/explicit-low-level pairs match native pixels at SS1/SS2 on both
  subjects. Four default captures match explicit Balanced, and bare recipe
  startup matches explicit Cached. Optional route overrides preserve legacy
  full templates while new null-override templates round-trip through `auto`.
- All twenty directed small-scene mode transitions pass, with settled image
  identity and five preparation/activation failure/retry stages. A failed
  request retains mode, scene, camera and published cache generations.
- Both full scenes pass cache→source→cache round trips. Cached-to-cached
  selections and sample-only changes reuse the field; Analytical releases it.
  Returning through Hybrid rebuilds once: about 1007 ms on Guardian and 142 ms
  on Robot in the observed switch. Entering Balanced from Cached adds its
  region cache, about 145–150 / 25–28 ms on the two scenes.
- The final root-far source route passes 155 frozen source checks. The 192
  original SPVs, canonical geometry/history/field/region/material sources and
  shared engine remain unchanged. Two new wrappers support C/C+N in material
  lighting. Existing controlled-cell/quality evidence remains applicable to the
  unchanged helpers; no geometry tolerance or golden was changed.
- Five nine-action history/recovery replays pass. Forty feature captures cover
  AO, soft shadows, ground/occluder, material/furnace/shared-atmosphere lighting,
  clear/frosted/clouded glass, lighting cycles, construction/presentation and
  debug views. Native RGB diagnostic-color screening passes, with its limited
  scope labeled separately from complete internal floating-point observation.
- Native Xvfb tests pass mode/sample selectors, resize/recreate, orbit/zoom,
  reset, normal-debug round trip, rejected/retried owned reload, material editing
  and history seek/undo/redo/playback. Root-far Analytical performs these with
  zero field builds. Workshop recipe changes reuse field/regions; specimen
  changes publish compatible new caches. A working material edit survives a
  mode round trip; CPU workshop tests preserve unsaved material/domain/library
  values through specimen changes.
- Ninety-six native arc frames compare Cached/SS2, Balanced/SS1 and Balanced/SS2
  without rebuilding geometry. These use explicit poses and review playback
  speed, not an interactive FPS measurement. Physical-desktop/human acceptance
  remains distinct from the completed virtual-window/machine checks.

## Costs that remain

The repeated geometry-action p95 (edit/undo/redo/retry, three runs) is:

| Subject | Cached | Balanced | Increase |
| --- | ---: | ---: | ---: |
| Guardian | 938.94 ms | 1076.62 ms | 14.66% / 137.68 ms |
| Robot | 98.74 ms | 120.94 ms | 22.48% / 22.20 ms |

Neither triggers the proposed stop rule of both 25% and 10 ms. Robot remains
close enough to that boundary to monitor on broader workloads. This does not
solve continuous editing: full field generation is still synchronous.

Balanced retains the 912,997,552-byte requested field payload and adds roughly
6.44 MB Guardian / 4.42 MB Robot region metadata. Source-only Analytical omits
the field. Mode selection shares compatible caches rather than keeping five
full renderers or several fields resident. The earlier per-generation metadata
and overlapping requested-geometry caps remain met. Requested bytes, CPU
scratch, staging and total Vulkan/device residency are different quantities.

A first observed material-workshop pipeline setup took **11.45 seconds**;
repeating the same configuration took **8.30 ms**. The study environment took
about 270 ms in each observation. These are observed process setups, without
deliberately clearing the driver cache. First shader selection can visibly
stall, and pipeline/startup work remains a separate optimization opportunity.

Balanced still cannot recover geometry omitted by the cache. Cached shadows,
thin-feature loss and wrong-first-surface cases from the quality study remain.
The detail-preserving profile still fails; retaining Analytical/Hybrid is
therefore a meaningful user choice rather than only a diagnostic convenience.

## Delivery boundary

This is local native implementation/default promotion, with a report snapshot.
No GitHub push, project-site deployment, canonical scene change, public media
replacement or image-golden update is part of the goal. Remaining investigations
include first shader/setup stalls, incremental geometry publication, selective
source visibility for lost detail, and acceleration of the other source lighting
or hybrid scopes. They do not block offering the qualified modes and default.
