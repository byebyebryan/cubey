# Forward environment transmission results

Completed 2026-11-07. Forward glass now resolves the shared atmosphere probe
along its estimated exit ray in the opt-in comparison presets. Existing forward
and glTF consumers retain projected-screen transmission. Canonical recipes,
shared lighting and the public showcase are preserved. No scene floor was added.

[Interactive comparison](http://starship:8787/reports/be78531d34c70c602e0febcd7defc684183d90d51f5c10fa363e81c2acbcf10e/)
shows screen forward, environment forward, SDF and hardware RT side by side,
with all twelve materials and twelve native films. The old report remains intact.

## Implemented contract

`ForwardPbrTransmissionSource::{Screen,Environment}` is a frame setting in the
shared renderer. It uses a spare scene-uniform lane; material layout and recipes
are unchanged. Ordinary and dispersed volume lookups use the common selector.
Thin surfaces retain their straight-through fallback direction. Environment
mode reuses the existing current/previous probes, transition, rotation and gain.
It does not trace neighboring bodies.

The workshop adds `--transmission-source screen|environment`, a native selector
and view persistence. View export creates parent directories; failed view/HDR
writes release GPU resources before returning the original error. Recipes remain
separate exports.

Optional scene readback and component views expose the production shader's
outgoing ray, Beer length, exit and mip-zero source radiance. Diagnostic capture
uses RGBA32F; ordinary rendering remains RGBA16F. Both modes retain the existing
refraction pyramid and resource ownership.

Readback exposed a shared render-graph omission: different read-only texture
usages require layout transitions. Sample → transfer → storage → sample now
emits the required barriers; same-usage consecutive reads still omit work.

## Qualification

| Check | Result |
| --- | --- |
| Directional-gradient source control | Mean RGB-vector error .234945 → .003005, about 78x lower |
| IOR-1 null control | Source difference p99 5.31e-5 |
| Constant radiance | Exact agreement |
| Four-IOR arithmetic | Observed-normal direction p99 at most .000288°; .1°/.5%-diameter arithmetic limits pass |
| Full integration suite | 291/291, zero skips, at `66e75372` |
| Final-source focused suite | 6/6 at `90b6caaa` |
| Current-session retention | Five pre-change 1080p screen-mode images remain byte-identical |
| glTF screen compatibility | Eight pinned assets; five calibrated attenuation, roughness, thin-wall, order and dispersion checks |
| Persistence and failures | Both source choices replay exactly at matched dimensions; native saved values and failed-output cleanup verified |
| Native lifecycle | Source/lighting switches, resize, orbit, zoom, reset and Save View pass synchronization validation |
| Presentation | 58 stills; nine three-renderer films plus three screen controls, each 240 frames, 30 fps, 1080p, eight seconds |
| RT film accounting | 720 frozen output states, each completing 256 primary iterations |
| Delivery | 88 published files verified against local bytes; material/gallery controls, playback and seeking reviewed in Chrome |

The gradient metric isolates source routing at mip zero. It does not establish
complete glass-energy or rough-BTDF accuracy. Geometry controls exclude a
two-pixel silhouette band, 1.238% of ideal sphere coverage, and use independent
Snell/Beer calculations with the observed production shading normal.

Frozen 1080p gallery costs, three serial repetitions per source with 30 warmups
and 150 completed GPU samples per repetition:

| Forward source | Worst repetition whole-scene GPU p95 |
| --- | ---: |
| Screen | 1.67189 ms |
| Environment | 1.66904 ms |

The difference is within measurement noise. The mode switch adds no images or
descriptor bindings and still builds the pyramid. These measurements do not
establish physical peak memory. Diagnostic export resources are separate.

## Qualified limits

The RGBA8 neutral normal texture tilts the shading normal by .3178°. At IOR 1.5,
p99 ideal-sphere direction error is .514°, so the original ideal-normal target
is explicitly **not passed**. The observed-normal control separates that retained
approximation from the lookup change. Normals and materials were not tuned to
match another renderer's image.

The historical 640×480 bronze image has a one-code difference in one pixel.
Restoring its exact historical fragment SPIR-V gives the same present-day image
as the new shader. The broader runtime cause is unestablished; the historical
byte gate remains failed. Today's five pre-change 1080p controls pass exactly.
Older captures are preserved.

Forward/SDF rough glass still filters radiance; RT samples bounded rough
dielectric paths. Geometry faceting, Fresnel/internal paths, filtering and solar
treatment can differ. This pass establishes neither whole-image equality nor
an unbiased reference. Local scene refraction, new rough-transmission filtering,
GI, caustics and RT clouds remain separate decisions.

Visible sky uses `SkyOnlyNoGroundOcclusion`, while the shared lighting probe
retains its `Ground` hemisphere. The dark refracted hemisphere is environment
radiance, not a scene ground plane.

## Source, preservation and recovery

- `7255cbc3`: pre-renderer research recovery point.
- `66e75372`: source setting, diagnostics, graph fix and workshop integration.
- `30bfad78`: qualified rendering source for matched media.
- `7fc6c96a`: export-directory and failure-cleanup qualification source.
- `90b6caaa`: final native layout and focused acceptance source.

F0's qualified fragment SPIR-V matches the final runtime. Later I/O/UI changes
are covered by focused, persistence and native checks. Select `screen` to retain
the previous transmission choice. All eleven protected SDF/horse files retain
their recorded hashes. There was no push or public-site promotion.

The report loads films into browser object URLs so seeking works on the existing
HTTP publisher without range support. Browser checks cover all three sequences;
observed four-way playback skew was .003 seconds, with exact paused seek targets
and no console errors. No shared server changes were needed.

[F0 receipt](../evidence/forward-transmission-2026-11/f0.json),
[F2 receipt](../evidence/forward-transmission-2026-11/f2.json),
[execution record](forward-transmission-progress.md) and
[operation guide](raytracing-material-workshop-guide.md) retain detailed scope.
Raw captures and commands remain under `outputs/raytracing/forward-transmission/`.

## Review closeout

The 2026-11-07 review covered `7255cbc3..a58549c1`: source selection, shared
uniforms and shader routing, readback synchronization and cleanup, view export,
comparison presets, qualification tools and report delivery. It found one
reproducible defect: `--view-output` lost the final headless `--orbit-degrees`
camera offset because the authored yaw was restored before exporting the view.
Capture motion now has explicit camera state, which Save View includes.

The new `pbr_materials_view_replay` regression failed before the fix and passes
after it. Four pairs cover screen/environment sources in the ordinary workshop
and a frozen library presentation; every replay is byte-identical to its final
capture. Seven focused tests pass, and all five 1080p screen retention controls
remain byte-identical. The qualified fragment SPIR-V, nine recorded F2 artifacts
and eleven protected files in the original worktree remain unchanged.

The [review receipt](../evidence/forward-transmission-2026-11/review.json) records
the finding, source identities and checks. No further code defects were found
within that scope; the optical and historical retention limits above remain
open. The proposed general workshop startup-default change is separate; this
closeout retains the existing defaults and published comparison snapshot.
