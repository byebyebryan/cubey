# glTF Staged-Loading Profile

Date: 2026-09-09

Status: baseline captured; the result is a performance decision checkpoint, not
a release benchmark or a cold-cache claim.

## Evidence boundary

The glTF validation work has three separate evidence classes:

- Compatibility evidence asks whether a selected asset loads and produces a
  valid headless PNG. The opt-in Khronos sample CTest lane owns this smoke
  coverage.
- Conformance evidence asks whether loader and renderer semantics satisfy
  focused analytic, furnace, and Khronos `SpecularTest` checks. A valid PNG
  alone is not conformance evidence.
- Performance evidence measures the staged loading boundaries for a fixed asset
  corpus. It does not claim visual fidelity, steady-state frame throughput, or
  cold-storage behavior.

## Repeatable workflow

The maintained runner is
[`projects/gltf_viewer/profile_gltf_loading.sh`](../../projects/gltf_viewer/profile_gltf_loading.sh).
It uses the release viewer and the existing pinned Sample Assets checkout; it
does not fetch or copy assets. The default command is:

```sh
cmake --preset release
cmake --build --preset release --target cubey_project_gltf_viewer
projects/gltf_viewer/profile_gltf_loading.sh
```

The release app must exist at
`build/release/projects/gltf_viewer/gltf_viewer`. The default asset root is
`build/dev-gltf-conformance/_deps/gltf_sample_assets-src`; `APP=...` and
`ASSET_ROOT=...` are useful overrides, but a live run accepts the asset root
only when it is the exact, clean Git checkout at Khronos Sample Assets commit
`2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf`. It rejects missing Git identity,
a different `HEAD`, and dirty tracked or untracked state before profiling. The
runner also requires `Models/model-index.json` and all five lane paths before
it starts. The recorded root-asset SHA-256 values are supplemental evidence;
the clean pinned checkout is the corpus identity.

Each lane runs one explicitly named `first` observation followed by five
`warm` observations (`REPEATS` changes the warm count). Every observation is a
new one-frame, 320x180 headless PNG run with `--profile-warmup-frames 0`, static
generated PBR IBL, clouds disabled, and no terrain/ocean/video. The PNG is
validated and removed; profile CSV, trace, summary, and log files remain under
an ignored `outputs/gltf/loading-profile-<timestamp>` directory. A live run
accepts a positional output directory only when it is new or empty; any
non-empty directory is rejected before metadata, profiles, or reports can be
changed. Failed fresh runs intentionally retain their partial evidence for
diagnosis.

Each live run also writes `profile-manifest.csv`: a deterministic,
machine-readable record of the exact lanes, first/warm observations, repeat
count, asset paths, and profile prefixes. `SUMMARIZE_ONLY=1` rebuilds
`runs.csv` and `summary.csv` solely from this retained manifest and its raw
profile CSVs. It does not require a viewer binary or Sample Assets checkout,
does not read the caller's `REPEATS` or current lane list, and does not rewrite
metadata or the manifest. The 2026-09-09 baseline predates the manifest; it is
replayed through a bounded compatibility path only because its retained
metadata proves the exact clean pin, fixed lane inventory, and repeat count.
That compatibility path does not create a manifest; new live runs always do.

The runner rejects a missing or malformed `.metrics.csv` unless it contains
exactly one complete 19-metric `gltf_loading` generation set. It writes a
machine-readable `runs.csv` row per observation and a wide `summary.csv` with
the first observation plus warm median/min/max values. `metadata.txt` records
the Cubey revision and dirty state, Sample Assets pin and revision, app hash,
host/OS/GPU information, and each asset's relative path, size, and SHA-256.

If a lane fails, the runner stops and reports the original log. It never
silently substitutes another model. A Sponza failure is explicitly returned as
an architecture decision because it would invalidate the large-scene lane.

## Baseline environment

The exact baseline command was:

```sh
cmake --preset release
cmake --build --preset release --target cubey_project_gltf_viewer
projects/gltf_viewer/profile_gltf_loading.sh
```

The runner selected output directory
`/home/bryan/code/cubey/outputs/gltf/loading-profile-20260909-120758`.

It used Cubey `870aa8e` plus the uncommitted staged-loading metrics work,
release app SHA-256
`292842bc1f183560cde87ccd5b909ae0f764dba939d0f4ee6fd686f283e5e673`, and
Khronos glTF Sample Assets commit
`2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf`. The host was `starship` running
Linux `7.2.3-arch1-3` on an NVIDIA GeForce RTX 5070 Ti with driver `610.57.04`.
The sample checkout occupied 3.1 GB in this checkout. That is an observed
opt-in storage cost, not a stable upstream size guarantee.

The first observation is not called cold: OS and filesystem caches were not
cleared, and the five warm repetitions are separate process launches. The
first/warm labels describe the runner order only.

## Results

Timings are CPU wall-clock milliseconds. `worker` is the staged worker envelope;
`residency` is the glTF GPU-owner residency phase; `activation` is frame-boundary
scene activation. The warm value is the median of five repetitions.

| Lane | First probe / load / prepare | Warm probe / load / prepare | First worker / residency / activation | Warm worker / residency / activation |
| --- | ---: | ---: | ---: | ---: |
| DamagedHelmet | 0.067 / 92.130 / 7.610 | 0.063 / 91.046 / 7.477 | 99.800 / 33.880 / 1.167 | 98.499 / 33.949 / 1.183 |
| AnisotropyBarnLamp BasisU | 0.079 / 2.078 / 136.091 | 0.071 / 2.290 / 133.732 | 138.215 / 14.570 / 1.143 | 135.878 / 14.779 / 1.146 |
| CesiumMan skinning | 0.208 / 11.694 / 0.425 | 0.202 / 11.358 / 0.425 | 12.151 / 10.421 / 1.175 | 11.849 / 10.422 / 1.172 |
| AnimatedMorphCube | 0.057 / 0.067 / 0.007 | 0.066 / 0.057 / 0.006 | 0.112 / 7.664 / 1.120 | 0.098 / 7.690 / 1.138 |
| Sponza | 0.616 / 448.395 / 57.979 | 0.622 / 465.059 / 59.817 | 506.438 / 132.917 / 1.347 | 524.939 / 138.028 / 1.379 |

The stable workload/count evidence is:

| Lane | Source bytes | Triangles | Nodes / materials / textures | BasisU encoded bytes | Decoded RGBA bytes | Prepared texture bytes | Mesh upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DamagedHelmet | 3,773,916 | 15,452 | 1 / 2 / 5 | 0 | 83,886,080 | 83,886,080 | 1,233,456 |
| AnisotropyBarnLamp BasisU | 6,979 | 10,203 | 3 / 4 / 4 | 4,809,167 | 0 | 16,777,216 | 677,700 |
| CesiumMan skinning | 438,044 | 4,672 | 22 / 2 / 1 | 0 | 4,194,304 | 4,194,304 | 291,720 |
| AnimatedMorphCube | 6,752 | 12 | 1 / 2 / 0 | 0 | 0 | 0 | 1,872 |
| Sponza | 167,176 | 262,267 | 1 / 26 / 69 | 0 | 285,212,736 | 285,212,736 | 17,006,916 |

All five lanes completed and emitted one complete metric set per observation.
There was no correctness failure and no lane required substitution.

## Decision gate

The probe is not the current bottleneck: it stayed below 0.7 ms. The measured
dominant phase differs by asset:

- DamagedHelmet and Sponza are dominated by `asset_load_ms`.
- AnisotropyBarnLamp is dominated by `scene_prepare_ms`, consistent with the
  BasisU-heavy preparation path.
- CesiumMan has comparable asset-load and residency costs at this size.
- AnimatedMorphCube is dominated by GPU residency despite negligible CPU work.

The next work should therefore target the measured load/prepare/residency
phase for a representative asset, with before/after runs from this same
workflow. The evidence does not justify general streaming, partial residency,
transfer queues, or upload-budget infrastructure yet: those are separate
runtime designs and this profile only measures startup of complete scenes. If a
future profile shows no meaningful bottleneck, stop there rather than inventing
streaming. If a profile reveals a correctness failure, repair correctness
first; otherwise let the dominant measured phase choose the next bounded
optimization.
