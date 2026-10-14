# Fluid 2.5D: simulation patch only

The demo's target is the detailed water experiment, not a wide terrain-backdrop
stress view. Fluid now displays only the complete selected simulation grid,
using the configured B-spline subdivision. Surrounding terrain and outer LOD
belong to the shared Terrain foundation, whose source, renderer and runtime
are unchanged by this pass. The six-site default collection stays the default;
prepared-world selection remains explicit.

## Implementation boundary

- Remove Fluid's exterior CPU mesh construction and scene-owned context GPU
  resources/draw calls, rather than merely hide them. Camera clipping and shadow
  fitting derive from the patch only. Generic shared renderer pipelines remain.
- Retain source/site path, hash, placement, margins and bed admission, and shared
  whole-source climate/material masks. This is not a zero-source-read claim.
- Remove the Wider terrain checkbox; show Simulation patch, Fit simulation patch
  and Full patch. Existing `overview`, `runoff` and `collection` IDs remain valid.
- Pin launcher context `off`; explicit legacy `off` works. `on` fails clearly
  before GPU startup and directs users to Terrain. No silent backdrop fallback.
- Preserve all five grid/area choices, site selection + restart, rain/pace/reset,
  B-spline banks, live/replay, diagnostics and builtin solver alternatives.
- No shader, water/material recipe, numerical solver, terrain generation or
  captured-field changes. No new camera angle/zoom or shared Terrain promotion.

## Headless evidence

Fresh root: `outputs/fluid/patch-only-20261013-EgH3uo`.
All 45 focused CPU/GPU/contract regression groups pass without skips.
Review [stills and motion in the browser report](http://starship:8787/reports/3b1edf62bd0def56df30a7d9d49af487ff291fc9527eab38d4961150b58e89e1/),
or return to the [Cubey report index](http://starship:8787/reports/#project=cubey).
The HTML-report workflow publishes a dedicated immutable export, separate from
the existing port-8801 report hub; it does not change that server or firewall.
Starship HTTP delivery and a private headless Chrome check pass: three view
selections, decoded playing clips, return-to-still/pause, evidence links,
390px layout and no-JavaScript fallback. Browser receipts/screenshots stay in
the fresh output root; they are not native Cubey GUI or Snap-side acceptance.
The frozen replay driver hashes runtime and immutable source/case/recording
inputs before/after each phase. Previous baseline driver/runtime receipts are
retained separately; final captures use the same current frozen runtime.

Four ordinary controls (1k full patch/runoff/collection and 2k runoff) remain
pixel-exact. Four prepared-world views record tiny raster differences after
removing the exterior camera clip range: mean absolute channel differences
0.010–0.031/255. They are not pixel-exact. Native bed/depth/decoded-velocity GPU
uploads remain exact. The world fixture preserves site identity and exact
shared material masks while reporting zero exterior vertices/triangles.

The managed live trial acknowledges rain/pace/reset, all five grid/area presets
and a different site. Nine scenes install dry/paused, nine imported-field audits
pass, and owned workers/viewer shut down without escalation or leftovers.
Switching-trial timings are mixed and are not steady-state throughput evidence.

Two independent default 1k/16m live trials, 1920×1080 hardware headless Vulkan on
RTX 5070 Ti, use a running external CUDA worker, 60Hz render cap, 60× requested
pace and 20s running warmup excluded from a 60s dry-start run. Measured pace is
59.83–60.09×, GPU scene p95 5.324–5.330ms, CPU service p95 2.684–2.770ms,
and advancing displayed field age p95 approximately 0.142s. Both meet the
8ms GPU / 4ms CPU / 1s freshness / 54× pace gates. This is not a developed-state
soak, native-display FPS qualification or a measured exterior-removal speedup.

Two reversed-order renderer-only repeats at 1080p, with 120 warmup frames,
no solver, upload audit/readback or encoding in the timed loop, measure GPU p95
3.80–4.12ms for 1k and 9.64–9.73ms for 2k/8m. The finer grid remains available
but exceeds the 8ms default scene budget on this captured view. Renderer-only
2k measurements do not requalify live 2k throughput.

Six-second full-patch/runoff/collection clips hold one native state and advance
only rain, foam and dots/trails. They show presentation motion, not advancing
hydrology; the live trials above establish actual streaming separately.

## Verdict

Patch-only composition is the appropriate Fluid contract. Local runoff is the
clearest review view; the water treatment needs no forced new shader change.
The obvious remaining presentation issue is the Full patch framing: substantial
empty background makes streams small. A separate, bounded initial-camera/fit UX
pass is more useful than returning to wide-backdrop quality tuning.

Native GUI/layout review remains deferred. Reports are headless/browser evidence,
not user-side Snap reachability or native GUI acceptance. No push is authorized.

Reproduce final controls, timing and motion with the frozen driver and report
helper using their `--phase` options and a fresh evidence root. Inputs point to
existing ignored prepared studies; they are not fetched or substituted. The
baseline phase requires its historical binary and driver receipt, not today's
final runtime. Treat runtime/input drift as a new study, never overwrite receipts.
