# Fixed-quality RT dragon and forward comparison evidence

2026-11-16. See the [results and limits](../../notes/raytracing-dragon-demo-results.md)
and [browser still/video comparison](http://starship:8787/reports/a9552bb10b43a53f3fb280516ebd1791f964c895352447c9d19c858e1846a619/).

- `identity.json`: final/capture executable identities, shader/source hashes,
  preserved asset/preset/recipe/scene-builder checks. A post-capture metrics-only
  correction retains the sampled split-depth field's numeric type. Final and
  captured demo HDR is bitwise equal; the shader is unchanged.
- `demo-controls.json`: final-executable real-GPU seed/sample-target,
  finite-output, fixed-frame and non-RT-baseline controls; sampled raw-reference
  comparison with explicit float tolerance and pixel-identical PNG.
- `sampled-glass-controls.json`: sixteen original asset-bound controls and
  their intentional shallow diagnostic truncation cases.
- `fullhd-validation.json`: whole/close HDR checks, independent of the smaller
  mechanical controls. The close-up has one pending-branch-cap pixel; there
  are no invalid-medium, query, event or visibility-budget failures.
- `motion-summary.json`: native 1080p recordings, moving-input GPU timestamps,
  fixed sample counts, validation and separately recorded Xvfb cadence.
- `browser-checks.json`: desktop browser image/comparison controls and playback,
  pause, chapter-seek and decode checks for all three clips.
- `rt-tests.log`, `documentation.log`: twenty RT and nine documentation tests.
- `validation.json`: bounded acceptance and preservation summary.

Ignored raw captures, logs and reproduction scripts are under
`outputs/raytracing/dragon-demo-20261116/`. The published report bundles only
the intended images, MP4s, timing summary and HTML. Native recordings use
owned Xvfb windows; no user desktop input was injected. The 30 fps MP4s repeat
displayed frames and do not claim native desktop FPS.

The first four-sample candidate's P95 GPU cost exceeds the provisional 33 ms
target at some angles. The one planned two-sample fallback was tested; no
further branch search, denoiser, footprint solver or reconstruction work was
added. Fine glass shimmer and the one-pixel capacity loss are retained limits
for visual review. No default promotion, commit, push or public-site update.
