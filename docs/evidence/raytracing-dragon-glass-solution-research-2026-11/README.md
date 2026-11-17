# Established glass-noise solutions: research evidence

2026-11-16. Supports the
[solution comparison and proposed pass](../../notes/raytracing-dragon-glass-noise-solutions.md).
This is a research-only continuation of the measured speckle report.

- `sources.json` records primary documentation/papers, checked dates and source
  blob identities where available. The RTXPT shader is pinned to commit
  `303b408ab81790a5051abd7573830b7a4112eb8e`; its design document is labeled
  v1.9.0 and was last changed 2026-10-23. The checked NRD README is v4.18.0.
- `classify_paths.py` uses the existing private diagnostic journal. It does not
  implement a candidate noise treatment. `path-classification.json` records
  two bright samples, their Fresnel probabilities/TIR flags and parity with
  the preceding sample-series measurements.
- Raw private build/journals are under
  `outputs/raytracing/dragon-glass-solution-research-20261116/`.

Both replays encounter only the certified glass body, with no opaque bounce.
Their internal reflections have TIR flags and probability one. Sample RGB is
identical to the earlier unstable-sample log, and differs by one float32 ULP
in one channel for the persistent-highlight sample. Flags are zero and
Vulkan synchronization validation is clean. These are illustrative samples,
not an exhaustive path census or measured candidate improvement.

Research recommendations are explicitly separated from facts measured in Cubey.
The EARS assessment uses the author/institution abstract and evaluation page;
the full PDF exceeded the web tool's size limit. Blender's manual search results
were found, but direct page fetches were unavailable; the clamp recommendation
is instead supported by accessible NVIDIA documentation and actual shader code.

The ordinary smooth-default source/build, assets and existing research are
preserved. No standard denoiser or candidate noise-control algorithm is installed
or integrated in this pass. The research remains uncommitted.
