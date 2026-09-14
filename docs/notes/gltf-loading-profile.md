# glTF Staged-Loading Profile

Date: 2026-09-10

Status: incremental-upload profile captured and the windowed responsiveness
checkpoint is closed; the result is not a release benchmark or cold-cache claim.

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
metadata or the manifest.

The runner rejects a missing or malformed `.metrics.csv` unless it contains
exactly one complete 46-metric `gltf_loading` generation set. It writes a
machine-readable `runs.csv` row per observation and a wide `summary.csv` with
the first observation plus warm median/min/max values. `metadata.txt` records
the Cubey revision and dirty state, Sample Assets pin and revision, app hash,
host/OS/GPU information, and each asset's relative path, size, and SHA-256.

If a lane fails, the runner stops and reports the original log. It never
silently substitutes another model. A Sponza failure is explicitly returned as
an architecture decision because it would invalidate the large-scene lane.

## Current environment

The current headless corpus command was:

```sh
cmake --preset release
cmake --build --preset release --target cubey_project_gltf_viewer
projects/gltf_viewer/profile_gltf_loading.sh \
  outputs/gltf/loading-profile-20260910-incremental-upload
```

The runner selected output directory
`/home/bryan/code/cubey/outputs/gltf/loading-profile-20260910-incremental-upload`.

It used Cubey `70c024c441886e3cd24c1aeb85872baa1d8f6855` with the incremental-upload
implementation present in the dirty worktree, release app SHA-256
`b6d182fbd0030dd46cfe57fb34df45cb8b8607131b01ded116747dbc6bf78f25`, and
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

`asset_load_ms` remains the inclusive loader wall-clock span. Its six exclusive
CPU children are recorded by the loader itself:

| Metric | Measured work |
| --- | --- |
| `document_parse_ms` | Root document read and parse in `cgltf_parse_file`. |
| `buffer_load_ms` | `cgltf_load_buffers`. |
| `asset_validate_ms` | `cgltf_validate` and Cubey's unsupported-feature rejection. |
| `image_payload_ms` | Image buffer-view extraction/copy, data-URI decode, or external image-file read. |
| `image_decode_ms` | PNG/JPEG decode and RGBA materialization; BasisU KTX2 remains encoded and reports zero here. |
| `asset_assembly_ms` | Remaining CPU asset construction, including KTX2 header validation. |

The exclusive values are local to one `load_gltf_asset()` call and use a
monotonic clock. On a failed load the optional output is reset at entry and may
contain only attempted work before the exception; no failed or superseded
request reaches the viewer's successfully activated-generation metric emission.

The exact 48-metric generation inventory is: `generation_id`,
`source_file_bytes`, `metadata_probe_ms`, `asset_load_ms`,
`document_parse_ms`, `buffer_load_ms`, `asset_validate_ms`,
`image_payload_ms`, `image_decode_ms`, `asset_assembly_ms`,
`scene_prepare_ms`, `staged_worker_prepare_ms`, `gltf_residency_ms`,
`staged_gpu_install_ms`, `activation_ms`, `triangle_count`, `node_count`,
`material_count`, `texture_count`, `prepared_texture_count`,
`basisu_encoded_image_bytes`, `decoded_rgba_bytes`,
`prepared_texture_upload_bytes`, `mesh_upload_bytes`,
`mesh_upload_transfer_submission_count`, `gpu_upload_bytes`,
`gpu_upload_copy_count`, `gpu_upload_owner_advance_count`,
`gpu_upload_step_count`, `gpu_upload_submission_count`,
`gpu_upload_owner_submit_ms`, `gpu_upload_owner_max_step_ms`,
`gpu_upload_owner_target_ms`, `gpu_upload_step_byte_cap`,
`gpu_upload_copy_byte_target`, `gpu_upload_owner_over_target_step_count`,
`gpu_upload_completion_latency_ms`,
`gpu_upload_pool_initial_capacity_bytes`,
`gpu_upload_pool_final_capacity_bytes`, `gpu_upload_pool_peak_capacity_bytes`,
`gpu_upload_pool_reserved_at_final_submission_bytes`,
`gpu_upload_pool_growth_count`, `gpu_upload_backpressure_count`,
`gpu_upload_first_step_to_final_completion_ms`,
`gpu_upload_submission_frame`, `gpu_upload_completion_frame`,
`default_texture_logical_binding_count`, and
`default_texture_physical_upload_count`.

The upload values measure one logical generation session covering glTF
textures, static mesh buffers, and deformation buffers. Owner advances also
include allocation-only work; physical steps and submissions contain copy work.
`gltf_residency_ms` measures the CPU cost of constructing that upload session;
it no longer represents the whole asynchronous residency interval.
`staged_gpu_install_ms` spans the viewer's complete `AwaitingGpu` stage through
final-ticket observation and resident-scene adoption. Historical tables below
that label the old synchronous value as residency retain their original
meaning; current whole-session comparisons use the staged install envelope or
the session's first-step-to-final-completion metric.
The historical `gpu_upload_owner_submit_ms` name is retained for profile
compatibility but now contains total owner-advance CPU time. The max, target,
and over-target values expose indivisible work that exceeds the soft 2 ms
target. The step cap bounds the aggregate staged bytes in one physical upload
step; the copy target bounds ordinary buffer chunks and block-row-aligned
texture chunks except when one indivisible row is larger. Pool capacity is
physical mapped capacity, while reserved-at-final is a
snapshot before the final ticket retires its leases. Final completion latency
starts at the last queue submission; first-step-to-final spans the whole upload
session. Both include polling cadence rather than claiming GPU timestamps.

The two frame values are correlation markers: submission is the first frame
that observed owner enqueue or `AwaitingGpu`, and completion is the activation
frame. Deterministic headless startup writes zero for both because it drains the
same session synchronously before frame zero.

## Windowed upload-jitter comparison

[`projects/gltf_viewer/profile_gltf_windowed_upload_jitter.sh`](../../projects/gltf_viewer/profile_gltf_windowed_upload_jitter.sh)
is the controlled windowed companion. It runs 1280x720 Sponza for 1,200 frames,
starts the import at frame 120 with `--profile-import-delay-frames`, and records
raw frames, host update/GPU-drain/draw spans, all loading metrics, the exact
command, and binary/asset hashes. It requires an explicit Wayland session and
does not read user input. A valid before/after comparison uses the same script,
asset, dimensions, frame count, and delayed-start trigger; the legacy baseline
gets only that profiling trigger in a disposable worktree, never a retained
legacy upload mode.

The 2026-09-10 controlled observations used the same Sponza file, compositor,
1280x720 configured extent, 120-frame warmup, and 1,200-frame bound. The legacy
binary was `HEAD` plus only the delayed-start trigger
(`b3f5b66266ec7fd6004202f8788d753bb570fcb0d9b27eafeaf81e663eaa2390`) and
therefore emits its older 19 loading metrics. The batch binary was
`5c9e74cc5124c66bfb49504f0b60c1159a907c55722de9766f077fcc5be6aecd` and emits
the former 32-metric inventory. The incremental binary was
`b6d182fbd0030dd46cfe57fb34df45cb8b8607131b01ded116747dbc6bf78f25`
and emits the then-current 44-metric inventory. The raw evidence, commands, and hashes are retained
under `outputs/gltf/windowed-upload-jitter-legacy-20260910-replay` and
`outputs/gltf/windowed-upload-jitter-batch-20260910-final`, with the matched
incremental result under
`outputs/gltf/windowed-upload-jitter-incremental-sponza-20260910-matched`.
The inventory difference does not affect the common frame/pass CSV comparison.

| Observation | Frame p99 / max (ms) | `host.gpu_drain` p99 / max (ms) | Activation frame |
| --- | ---: | ---: | ---: |
| Legacy immediate | 1.789 / 141.876 | 0.000 / 140.602 | 771 |
| One batch | 1.874 / 112.386 | 0.001 / 100.516 | 798 |
| Incremental session | 2.529 / 3.751 | 1.043 / 2.222 | 861 |

The incremental session uploaded the same 302,219,712 bytes as 358 bounded copy
regions over 45 owner advances and 43 same-graphics-queue submissions. Import
became visible to the upload lane at frame 769 and the complete generation
activated atomically at frame 861. Total owner work was 43.616 ms, but its
largest advance was 2.221 ms and the final fence was observed after 1.782 ms.
The 32 MiB startup pool neither grew nor backpressured in this paced windowed
lane. The acceptance targets are therefore met: the worst observed frame was
3.751 ms, below 16.7 ms, and the worst `host.gpu_drain` span was 2.222 ms,
below 10 ms.

A separate 600-frame DamagedHelmet check uploaded 85,119,596 bytes in 57 copy
regions over 12 advances/submissions. It moved from upload marker frame 249 to
atomic activation at 275, with 2.120 ms maximum owner work, 2.119 ms maximum
`host.gpu_drain`, and 3.425 ms maximum frame time. Its pool also remained at
32 MiB without growth or backpressure. The raw evidence is
`outputs/gltf/windowed-upload-jitter-incremental-damaged-helmet-20260910-final`.

## Upload-policy tuning

The follow-up policy sweep retained the 2 ms owner target and 2 MiB logical-copy
target while testing 8, 16, and 32 MiB physical-step caps. It recorded three
valid repetitions for every Sponza/DamagedHelmet, unpaced/explicit-60-Hz cell:
36 runs under `outputs/gltf/upload-policy-tuning-20260910-r1`. Four earlier
observations that overlapped another viewer process are retained but explicitly
excluded. Each valid run records its command, app and asset hashes, policy
values, raw profiles, and summary.

The primary Sponza whole-session medians were:

| Pace | 8 MiB | 16 MiB | 32 MiB |
| --- | ---: | ---: | ---: |
| Unpaced | 85.386 ms | 79.009 ms | 82.099 ms |
| 60 Hz | 801.364 ms | 500.531 ms | 432.726 ms |

The 32 MiB cap is 3.9% slower than the fastest unpaced value and fastest in the
controlled 60 Hz lane, so it is the only candidate within 5% of the fastest
primary result in both modes. The DamagedHelmet control also favors 32 MiB when
paced; its small unpaced session remains about 20 ms for all three candidates.
The selected defaults are therefore a 32 MiB step cap, 2 MiB copy target, and
soft 2 ms owner target.

All upload-attributable host work remained inside one 16.7 ms frame budget and
every `host.gpu_drain` maximum stayed below 10 ms. With the selected cap,
Sponza's worst owner advance was 6.781 ms, worst unpaced frame was 11.611 ms,
and worst paced `host.update` was 10.698 ms. Paced raw frame deltas reach about
17.1 ms because they include the deliberate 16.7 ms period plus sleep
overshoot; the pacing sleep is outside the profiled host spans.

Follow-up current-tree evidence found the completion edge hidden by that broad
span. One Sponza run reached 17.513 ms frame-to-frame with 14.758 ms in
`host.update`. Nested spans showed session adoption at 0.001 ms, old-scene
retirement at 0.053 ms, and actual activation at 0.109 ms. The unmeasured tail
was destruction of the completed session plus roughly 285 MiB of prepared CPU
texture payload. The viewer now hands that spent CPU-only shell to its asset
worker after Engine/Vulkan adoption. Three explicit-60-Hz Sponza repetitions on
the corrected path keep `host.update` at or below 0.229 ms, `host.gpu_drain` at
or below 7.006 ms, activation at or below 0.159 ms, and the disposal enqueue at
or below 0.005 ms. Their upload-session durations are 416.103--433.366 ms with
no staging backpressure. Three matched DamagedHelmet GLB controls keep
`host.update` at or below 0.317 ms and `host.gpu_drain` at or below 7.173 ms.
Raw paced frame deltas still reach 17.073 ms because the deliberate 16.7 ms
period includes sleep overshoot, while all measured host work remains within
the frame-work gate. The retained evidence is under
`outputs/gltf/windowed-upload-jitter-default32-{sponza,helmet}-disposal-r[1-3]-20260910`.

The final default was also replayed across the complete five-asset corpus with
one first observation plus five warm observations per lane. All 30 observations
contain one complete 46-metric generation set under
`outputs/gltf/loading-profile-20260910-upload-policy-final-r2`. The recorded
release viewer SHA-256 is
`20a37c8eef857167e72d46a1a1614946e98439fb859c8f1cf619f35266b50ccf`, and the
Sample Assets checkout remains the required clean
`2bac6f8c57bf471df0d2a1e8a8ec023c7801dddf` pin.

Warm CPU phase medians from this capture were:

| Lane | Parse | Buffers | Validate | Image payload | Image decode | Assembly |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| DamagedHelmet | 0.829 | 0.000 | 0.052 | 0.496 | 91.689 | 0.357 |
| AnisotropyBarnLamp BasisU | 0.068 | 0.130 | 0.017 | 1.648 | 0.000 | 0.405 |
| CesiumMan skinning | 0.322 | 0.000 | 0.014 | 0.048 | 10.985 | 0.208 |
| AnimatedMorphCube | 0.049 | 0.000 | 0.001 | 0.000 | 0.000 | 0.006 |
| Sponza | 0.674 | 1.656 | 0.556 | 3.427 | 462.152 | 6.233 |

| Lane | First probe / load / prepare | Warm probe / load / prepare | First worker / residency / activation | Warm worker / residency / activation |
| --- | ---: | ---: | ---: | ---: |
| DamagedHelmet | 0.071 / 102.580 / 7.449 | 0.081 / 94.197 / 7.618 | 110.080 / 21.746 / 1.157 | 101.740 / 21.374 / 1.184 |
| AnisotropyBarnLamp BasisU | 0.080 / 2.269 / 134.195 | 0.075 / 2.348 / 134.461 | 136.500 / 5.797 / 1.163 | 136.678 / 5.692 / 1.144 |
| CesiumMan skinning | 0.202 / 11.538 / 0.425 | 0.203 / 11.587 / 0.426 | 12.009 / 4.243 / 1.103 | 12.046 / 4.651 / 1.151 |
| AnimatedMorphCube | 0.069 / 0.062 / 0.006 | 0.065 / 0.059 / 0.006 | 0.105 / 3.603 / 1.049 | 0.104 / 3.663 / 1.090 |
| Sponza | 0.951 / 479.776 / 66.573 | 0.882 / 474.929 / 63.167 | 546.390 / 63.770 / 1.263 | 539.771 / 62.951 / 1.243 |

Warm incremental-upload medians from the same headless corpus were:

| Lane | Upload bytes | Copies / advances / submissions | Owner total / max (ms) | Final fence / whole session (ms) | Final pool / growth / pressure |
| --- | ---: | ---: | ---: | ---: | ---: |
| DamagedHelmet | 85,119,596 | 57 / 7 / 7 | 21.107 / 6.095 | 0.120 / 19.105 | 96 MiB / 2 / 0 |
| AnisotropyBarnLamp BasisU | 17,454,976 | 29 / 3 / 3 | 5.478 / 2.116 | 0.100 / 3.555 | 32 MiB / 0 / 0 |
| CesiumMan skinning | 5,118,200 | 24 / 3 / 2 | 4.415 / 2.302 | 0.181 / 2.508 | 32 MiB / 0 / 0 |
| AnimatedMorphCube | 7,292 | 22 / 2 / 2 | 3.574 / 2.057 | 0.050 / 1.600 | 32 MiB / 0 / 0 |
| Sponza | 302,219,712 | 358 / 24 / 22 | 60.269 / 6.592 | 0.173 / 60.802 | 128 MiB / 3 / 2 |

Headless `finish()` deliberately drains successive owner advances without a
frame boundary. Some destination allocations are indivisible, so its soft 2 ms
target records maximum advances up to 6.938 ms in the first Sponza observation,
and Sponza reaches the 128 MiB cap with two backpressure retries. Those values
make the limitation explicit;
they are not evidence of a windowed frame stall. The paced windowed Sponza and
DamagedHelmet lanes above are the responsiveness gate, and both pass without
pool growth or backpressure.

The stable workload/count evidence is:

| Lane | Source bytes | Triangles | Nodes / materials / textures | BasisU encoded bytes | Decoded RGBA bytes | Prepared texture bytes | Mesh upload bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| DamagedHelmet | 3,773,916 | 15,452 | 1 / 2 / 5 | 0 | 83,886,080 | 83,886,080 | 1,233,456 |
| AnisotropyBarnLamp BasisU | 6,979 | 10,203 | 3 / 4 / 4 | 4,809,167 | 0 | 16,777,216 | 677,700 |
| CesiumMan skinning | 438,044 | 4,672 | 22 / 2 / 1 | 0 | 4,194,304 | 4,194,304 | 291,720 |
| AnimatedMorphCube | 6,752 | 12 | 1 / 2 / 0 | 0 | 0 | 0 | 1,872 |
| Sponza | 167,176 | 262,267 | 1 / 26 / 69 | 0 | 285,212,736 | 285,212,736 | 17,006,916 |

All five lanes completed and emitted one complete metric set per observation.
There was no correctness failure, no lane required substitution, and replay
with `SUMMARIZE_ONLY=1` regenerated the reports from the retained manifest and
raw profiles.
