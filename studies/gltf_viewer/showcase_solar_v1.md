# Front-facing solar showcase revision

Status: owner-approved and promoted as the current glTF gallery highlight.
The fixed-morning cut is retained as historical media/receipt provenance.

[Compare the revised film with the published cut](http://starship:8787/reports/973972e1e0e1bd8d47f90eab26a386296d0d167cbeff300b993d7d579bb0e8b4/)
in the indexed Cubey report catalog. Native Cubey GUI review is separate.
The report buffers the two small comparison MP4s locally in the browser so
frame seeking works even when the report host does not serve HTTP byte ranges.

## Selected revision

- Start at yaw **0°**, verified face-on against 0°/45°/90°/135° stills.
- Ease through **45°** over eight seconds, keeping pitch −6.8755°, distance
  scale 0.70, the same scene bounds and the fitted foreground/background depth.
- Advance the existing shared solar clock from **14:00 to 19:30**, at
  0.6875 simulated hours per film second. This keeps daylight and golden hour
  for most of the film, with dusk/early night as the closing beat.
- Reuse existing automatic exposure: omit the explicit `--exposure 0` override.
- Use `--star-intensity 0` in this capture only. Visible point stars sampled
  through the 64px environment cubemap became large blurry blocks at night.
  This avoids that artifact; it does not fix or globally disable shared stars.
- Retain original-mountain A, its held 12h/512mm/h storm state, clear sky,
  water optics, surface cues and corrected depth. No C++ or shaders changed.

The front-facing opening makes the helmet much more readable than the old
84.5° start. The lake is less prominent initially and opens up on the left as
the orbit progresses. Twilight colors are intentionally strong; the last beat
is darker, not a new permanently bright night material.

## Rejected controls

Both 30° and 45° face-on arcs were rendered with the old 14:00→21:00 schedule
and explicit exposure 0. The longer arc revealed more of the lake and helmet,
but that clock spent too much of the clip dark. The first night check exposed
the point-star/cubemap artifact. Static 18:30/19:00/21:00 controls, a +0.5
fixed-exposure control and automatic-exposure controls isolated the remedies.

The old morning clip is a presentation comparison, not a matched shader A/B:
its camera headings and solar state deliberately differ. Shared/global defaults
and the normal viewer path are unchanged.

## Evidence and handoff

Authoring root: `outputs/showcase/gltf-front-solar-20261015-YcdZBy/`.

- Runtime and immutable input identities frozen at `e38886c3`; GPU bed, depth
  and decoded velocity audited exact after every film; zero hydraulic dispatches.
- Required Vulkan validation passed on the RTX 5070 Ti without a display server.
  No solver, CUDA worker, terrain generation or native GUI was launched.
- The repeated selected render matches **all 480 decoded frames exactly**:
  `31923b308915e532fc3008be4ddb8e242acf50ac03cf77c9f9e8fac7211fcccd`.
- 22 scoped CTest groups, 15 glTF helper/media tests, and 15 Fluid presentation
  tests pass. These are regression/capture checks, not live performance claims.
- `package/` contains a native 1080p60 CRF20 master, Lanczos 720p60 CRF24
  derivative, four-second poster and exact recipe. Both MP4s are silent,
  fast-start H.264 High/yuv420p, exactly eight seconds and 480 CFR frames.
- Approved gallery: 1,854,610 bytes;
  SHA256 `e8d2e16887ae8b28a4e12d711f511e72e189902297684e26ae9e2c0a2eecf909`.
- The exact approved package replaces the current glTF media without a recapture
  or re-encode. Its own attachment and byte verification are recorded in the
  [publication receipt](../../docs/media/showcase/gltf-terrain-publication.json).
  The morning recipe/receipt and both earlier attachments remain historical;
  the other four highlights are unchanged.
- Damaged Helmet media remains CC-BY-NC-4.0 with ctxwing/theblueturtle_
  attribution; Cubey source remains MIT.

## Reproduction

With the existing prepared terrain/water cache and Damaged Helmet available,
use a fresh, nonexistent output directory. Freeze records the actual worktree
status instead of assuming it is clean. Capture/package phases refuse existing
artifacts and verify the frozen runtime/inputs before and after work.

```sh
rtk proxy python3 -B studies/gltf_viewer/review_showcase_solar_v1.py outputs/showcase/gltf-solar-repeat --phase freeze
rtk proxy python3 -B studies/gltf_viewer/review_showcase_solar_v1.py outputs/showcase/gltf-solar-repeat --phase capture-refined
rtk proxy python3 -B studies/gltf_viewer/review_showcase_solar_v1.py outputs/showcase/gltf-solar-repeat --phase package
```

`--phase capture` renders the original 30°/45° controls. The report builder
uses the retained control/repeat receipts as well as those films and the
selected package; it does not generate missing evidence or modify the gallery.
