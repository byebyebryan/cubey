# Current documentation and source reconciliation

Date: 2026-10-26

Baseline: `ee94bd895d8cd7bf868336fde7330cd105d27acc`.

This is the bounded source-accuracy follow-up to the
[documentation/media reconciliation](cubey-docs-media-reconciliation.md).
The review found an operational profile-reader mismatch, obsolete startup
examples and older guidance that could send new work toward the wrong owner.
It did not select a renderer, solver, media or site redesign.

## Loading-profile contract

The native glTF viewer emits 48 `gltf_loading` metrics. The runner still
required exactly 46, so it rejected a current profile at its first observation.
An isolated reproduction using the emitter's metric names confirmed that
failure without a renderer, GPU or asset download.

The maintained [runner](../../projects/gltf_viewer/tools/profile_gltf_loading.sh)
now makes the inventory explicit in the observation manifest:

| Manifest | Inventory | Use |
| --- | --- | --- |
| `gltf_loading_profile_v2` | 48 metrics, including logical/physical default-texture counts | New live runs and replay of their saved profiles. |
| `gltf_loading_profile_v1` | The original 46 metrics | Replay of retained V1 recordings with the same report columns. |

The existing pinned metadata-only fallback retains the 46-metric inventory.
Earlier recorded inventories remain source-pinned evidence requiring their
original tools or saved summaries; they are not automatically upgraded.

Manifest rows must agree on their schema. Each profile must contain exactly
the named inventory from one emission frame. Duplicate names, missing metrics,
unknown replacements, unsupported schemas, mixed versions and mixed frames
fail. A V2 profile missing both new counters cannot silently become V1.
No absent values are invented. Failed replay leaves existing CSV reports
in place; metadata, raw profiles and manifests remain input evidence.

The [portable fixture suite](../../projects/gltf_viewer/tools/profile_gltf_loading_tests.sh)
reads current fixture names from the native emitter rather than copying the
runner's inventory. It covers current manifest writing, a stand-in live
observation, current/legacy replay, metadata-only legacy replay, report values,
schema errors and preservation of existing reports on failure. Its stand-in
process produces fixture data, not native rendering or performance evidence.

Actual retained compatibility was checked using a copy of
`outputs/gltf/loading-profile-20260910-upload-policy-final-r2`: all 30
46-metric observations replay successfully with no executable or Sample Assets
checkout available to the runner. The rebuilt `runs.csv` and `summary.csv`
are byte-identical to their original reports. All 34 selected source/report
files retain their hashes, and the source directory is untouched. This is
historical replay qualification, not a fresh loading benchmark.

The [operating guide](gltf-loading-profile.md#repeatable-workflow),
[performance guide](performance-profiling.md#gltf-staged-loading-profile) and
[tool index](../../projects/gltf_viewer/tools/README.md) now describe both
schemas. The recorded environment and measurement tables remain dated; the
current 48-metric inventory is distinguished from older 46-metric results.

## Current design and ownership

- [Design](../DESIGN.md) uses the implemented typed parse/run startup instead
  of a nonexistent `cubey::run<MyProject>()` API or virtual `App` interface.
  Its headless example uses a real executable. Current host/resource framing
  reflects the implemented foundation, and reference guidance permits the
  established licensed-port workflow with notices and provenance.
- [Smoke 2D](../../projects/fluid/smoke_2d/README.md) documents its existing
  coarse graph integration and runtime panel. APIC liquids and dense 3D pyro
  link to their implemented project owners; additional solver experiments
  remain separate choices. The [fluid map](../architecture/fluid-simulation.md)
  distinguishes those baselines from unselected particle/hybrid methods.
- [Ocean's adjacent-system map](../architecture/ocean-adjacent-systems.md)
  distinguishes builtin Fluid from external Terrain Water. Dynamic coastal
  coupling remains deferred. [Ocean rendering](../architecture/ocean-rendering.md)
  no longer implies that bathymetry is part of the current Terrain V1 backdrop.
- [Procedural generation](../architecture/procedural-generation.md) separates
  implemented primitives/cache/parity from completed migration history and
  unselected extensions. It links current Terrain V1 and shared surface
  evaluators, rather than treating the archived generator reboot as upcoming.
- All thirteen native owners represented by the current site now have direct
  showcase links in their project READMEs. The six completed front doors are
  Atmosphere, Terrain, Smoke 2D, Water 2D, builtin Fluid 2.5D and Explosion 3D.

The prior media-retention decisions stand. Site inputs, native runtime source,
the approved README hero, publication assets/receipts and sealed study records
are preserved. No new native capture or external publication asset is selected.

## Validation boundary

Final integration checks pass:

- Eight selected `dev-docs` groups, excluding `documentation_cli_recipes`,
  cover layout/rules, navigation across all 353 tracked Markdown files,
  retained media integrity and Fluid/glTF presentation records.
- `gltf_viewer_loading_profile_script_tests` passes through CTest, including
  the current emitter-derived fixtures and schema/failure boundaries above.
- `bash -n`, ShellCheck and `git diff --check` pass for the changed files.
- All prior anchors in changed Markdown remain available. All ten fenced
  recipe blocks in the six changed project READMEs are byte-for-byte unchanged.
- All 249 protected site/media/manifest/sealed-study files retain their hashes.
  The only non-Markdown changes are the profile runner and its fixture suite;
  native C++/headers/shaders/configuration and site deployment inputs are unchanged.

The integration inventory, CTest XML, preservation checks and copied historical
replay receipt are under ignored
`outputs/docs-source-reconciliation-2026-10-26/`.

The full native recipe lane still requires a current complete development build;
the checkout's missing/stale native executables do not establish that gate.
No renderer/solver benchmark or human visual acceptance is renewed by this pass.

The public site remains the existing Pages deployment from source
`48fa1ed1c7c7d02d0d6892466fecc2dec0afeb42`, workflow `40244161355`.
None of this pass's selected paths are Pages build/deploy inputs.
