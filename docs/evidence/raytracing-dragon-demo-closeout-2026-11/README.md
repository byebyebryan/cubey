# Dragon demo closeout evidence — 2026-11-17

[Interactive report and prepared site snapshot](http://starship:8787/reports/db121962f7a3f6de71e151d48f9a868987ea457ca170d76ded224c58b8e7a03d/)
provide the visual review packet. Report delivery and browser playback are
verified; this does not deploy the public site.

The [closeout note](../../notes/raytracing-dragon-demo-closeout.md) defines the
three retained modes, launcher and accepted quality boundary. This packet
qualifies presentation and launch changes against base revision `05794fc2`
plus the uncommitted closeout patch. It does not certify optical perfection,
physical desktop cadence, or public deployment.

## Fresh captures and cost

Nine 1920×1080 stills cover whole, detail and front across forward hybrid,
four-sample fixed RT and stationary 256-sample RT. All use identical camera,
model, materials and clear-sky settings with shadows disabled. Forward beauty
stills use its ordinary RGBA16F route; its three separate HDR diagnostics use
RGBA32F and are not substituted as beauty media. All twelve captures pass
Vulkan validation, have finite nonnegative HDR output where requested, and
reproduce the preceding pre-CLI-alias-fix PNGs byte-for-byte.

Normal-speed native 1080p outward/stop/return captures pass synchronization
validation on an explicitly owned Xvfb display. The editor is hidden. Forward
records 905 rendered frames with RT disabled and zero AS allocation; fixed RT
records 634 with four samples per frame; sampled RT records 888 and reaches
256 samples at the final stop. Their 30 fps video streams may repeat displayed
frames. This is native application evidence, not physical desktop FPS.

Unpaced headless GPU timestamps on the RTX 5070 Ti, driver 620.70.06:

| Route | Median | p95 | Sampling during the measured moving sweep |
| --- | ---: | ---: | --- |
| Forward hybrid | 1.045 ms | 1.067 ms | One raster sample per frame |
| Fixed RT | 27.603 ms | 28.855 ms | Four spatial samples per frame |
| Fixed RT alternative | 14.120 ms | 14.770 ms | Two spatial samples per frame |
| Sampled RT | 7.005 ms | 7.188 ms | One new primary sample; history resets |

Each serial run measures 151 matching poses after 30 warmup frames at
1920×1080, with validation and recording disabled. These are project pass GPU
costs rather than total desktop frame times. They compare intentionally
different sampling policies; the sampled figure is not 256-SPP throughput.
The forward metrics do not name a device; the host's single-GPU identity is
recorded separately rather than synthesized into that renderer's metrics.

## Retained records and limits

- `identity.json`: base source, exact executable/model hashes, GPU/driver and
  renderer/preset source hashes.
- `capture-calls.json`: all twelve final commands, validation and image equality.
- `capture-configs.json`: full captured scene/view and GPU-sweep configurations,
  keyed by their original ignored paths, with exact input hashes. Restore these
  paths or point the recorded commands at extracted copies when reproducing.
- `hdr-validation.json`: all HDR extrema and RT transport-status counts.
- `native-motion.json`: three native commands, results, phase timestamps and
  original video/event/metric hashes.
- `gpu-sweep.json`: serial GPU commands, scope, sample counts and measured costs.
- `runtime-tests.log` and `repository-tests.log`: the 23 focused runtime and
  nine repository/documentation groups. Navigation over new Markdown is checked
  separately because the normal repository inventory selects tracked files.
- `validation.json`, `new-markdown-navigation.json` and `delivery.json`: final
  source/site/browser checks, the complete Markdown navigation check and the
  durable report delivery identity.

Fixed RT records policy pruning (status 128), and one pixel reaches the
pending-branch bound (64) in each of the detail and front stills. Whole has no
capacity flag. The sampled views retain 14/18/16 numeric-tail pixels (8) in
whole/detail/front. None of the views records total-call/event/path budget
exhaustion, invalid medium or cross-instance status. These counters are
reported limits; no additional optical repair belongs to this closeout.

Large original images, HDR buffers, native logs, metrics and videos remain in
ignored `outputs/raytracing/closeout-20261117/`. Initial captures and failed
capture/receipt attempts are retained there separately. Selected web derivatives
and their input/output hashes live in the site's media ledger and dedicated
dragon receipt. The model remains explicitly prepared in ignored cache and
carries Stanford's separate source/usage notice.
