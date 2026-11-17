# Forward dragon scene visibility — 2026-11-16

[Interactive comparison and native motion](http://starship:8787/reports/b8c6f3111377c9a11eb1ac0b9a82dcd5d7dd4e68d8559f08025dc23f3a5aadfa/)
corrects the preceding environment-only baseline. See the
[implementation and limits](../../notes/raytracing-forward-transparency.md).

The pattern-only control changes zero of 43,823 interior glass pixels in
environment mode, 10,051 in screen mode and 10,039 in hybrid mode. This
establishes local scene visibility, not optical accuracy. Existing screen and
environment PNG controls remain pixel-identical to before the shared change.

- `visibility-controls.json`: final executable's seven capture commands and
  the measured scene response, with validation enabled and RT disabled.
- `compatibility.json`: both retained source modes and both slab patterns.
- `hdr-validation.json`: finite wide/detail 1080p HDR and synchronization validation.
- `native-hybrid.json`: successful fresh native orbit/stop/return recording.
- `shared-source-controls.json`: eighteen production forward source controls.
- `regression-tests.log`: 23 passing core, RT and shared material tests.
- `identity.json`: source, shader, executable and asset hashes; capture groups
  distinguish the initial build from the final error-guard cleanup. All six
  final source PNGs reproduce the initial build byte-for-byte.

Large captures, video and recovery copies remain under the ignored
`outputs/raytracing/forward-transparency-check-20261116/` directory. Its
`before-hybrid/` snapshot preserves the prior tracked patch and twelve touched
source files, including untracked forward adapter work. Published HTML/assets
are stored separately by the shared report service.

The RT images and clip are retained from the preceding pass; the RT transport
shader, scene and materials were not changed. The new native forward clip
records 464 frames with one raster sample per frame, validation enabled and
zero AS allocation. Xvfb cadence is not physical desktop FPS. No commit, push
or public-site deployment is included.

The 2026-11-17 review cleanup is recorded in `review-fixes.json` and
`review-fix-visibility.json`: the sky-backed control rejects screen-only
misrouting, the marble control rejects environment-only misrouting, and the
native selector label is fully visible. Saved CTest log copies have only
their extra blank lines at EOF removed; the normalization hashes and raw
recovery location are recorded with the review fixes.
