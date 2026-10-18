# October foundation qualification

This record separates the runtime foundation checkpoints from the documentation
consolidation that followed. The [machine-readable qualification](qualification.json)
pins source identities, commands, scopes and protected file hashes. The Git
commit containing this record identifies the closing documentation; tooling
qualification is pinned to `87f5a932914099c6b0ea684fa3bfc61e96b940a7`.

## Runtime checkpoints

| Source | Established scope | Boundary |
| --- | --- | --- |
| `d713512b4b7dcc3ac99addd550043b7d9fdaea4f`, closed by `c057139212cd96a5667f00f8820792d534352809` | 252 dev groups, six alternative Fluid domains, seven configure variants, focused sanitizers, 19 virtual-X11 windowed groups, six synchronization groups and exact retained capture comparisons. | Full runtime qualification before the later Ocean correction. |
| `86885a4d11acb40c5b19937ef5606aaa0e4e1671` | Ocean GPU-owner ordering correction: 13 focused groups, 19 virtual-windowed groups, six synchronization groups, owner ASan/UBSan and TSan checks, three ordinary and three sanitizer Ocean capture comparisons. | Focused follow-up; the full 252-group suite was not repeated. |

The original local receipts remain at
[the capture/Fluid closure](../../../outputs/foundation-loop/20261016-capture-fluid/closure.json)
and [the Ocean follow-up](../../../outputs/repo-review/20261017-final/review.json).
These are optional ignored review artifacts, not fresh-checkout dependencies.
Their original timestamps and capture-time publication state remain unchanged.
The [checkpoint archive](../../archive/foundation-checkpoints.md) retains the
phase narrative and historical measurements.

## Documentation pass

- Corrected current product/source ownership and the five-highlight scope.
- Separated the current roadmap from completed foundation phases and replaced
  the duplicated DESIGN source tree with an ownership map.
- Curated operating guides, decision checkpoints, historical research, archives
  and supported tool indexes. Tracked owned Markdown has no missing local links
  or unindexed pages, excluding the sealed snapshots' deliberate historical links.
- Shared navigation and media primitives between repository policy and the
  Fluid/glTF study tests. Domain assertions and immutable history remain local.
- Added the `dev-docs` lane: nine groups passed, including 99 documented native
  recipes and 11 primitive/negative fixture cases. The recipe lane runs with
  unusable Vulkan ICD paths and empty display variables and exits through
  configuration-template generation; it does not render or load scene assets.
- The eight-group `dev-foundation` lane passed, including standalone headers,
  configured dependency guards, core tests and CMake/shader fixtures. The CMake
  application fixture covers active, study-only and empty registries.
- Seven configurations were checked: projects-only, examples-only, cloud-only,
  Planet-only, terrain-studies-only, core-only and hostless. Static policy/docs
  gates passed on the six testable variants; hostless is configure-only because
  the repository requires the host for `BUILD_TESTING`.
- All 27 protected showcase/provenance, sealed narrative and relocation files
  retained their original hashes. No C++, public header, shader, numerical
  oracle or external-worker dependency changed.

Run the current gates after the normal dev configure/build:

```sh
ctest --preset dev-docs
ctest --preset dev-foundation
```

The [tools guide](../../../tools/README.md#documentation-gate) describes the
mechanical checks and their limits. Parser-only CLI checks, media integrity,
static build policy, rendered output and human visual acceptance establish
different things. No new full runtime suite, GPU/sanitizer qualification,
external CUDA worker admission, native GUI review or creative media approval
was performed by this documentation pass.

## Bootstrap and Terrain publication follow-up

The 2026-10-18 review found an incomplete bootstrap contract and Terrain
generation paths that deleted the previous bundle before installation succeeded.
The [follow-up qualification](bootstrap-publication.json) pins these fixes to
`b663564a` (toolchain/setup) and `1f4ad859` (Terrain publication).

- CMake 3.25+ matches the preset schema; all CTest Python helpers use one
  required Python 3.11+ interpreter. The normal presets use Clang 16+ with an
  early structured-binding capture capability check. Setup covers OpenSSL,
  HTTPS certificates and GLFW's Linux backend dependencies.
- A clean Debian 12 container, using only the documented package list plus
  the documented `clang-16` override, built all dev targets with CMake 3.25.1,
  Clang 16.0.6 and Python 3.11.2. All 15 distinct docs/foundation groups passed,
  including standalone public headers and native CLI recipe parsing. Optional
  libav capture was disabled; no Vulkan rendering or desktop run was performed.
  Clang 14 was rejected before dependency fetching. Separately, an explicitly
  missing test interpreter failed early, and hostless configuration with
  tests disabled succeeded without Python.
- All seven Terrain Diffusion generation modes use one recoverable publication
  helper. Fourteen focused tests cover installation, rollback, interruption,
  contention, process-kill recovery and a default-generator fixture. They passed
  on Python 3.11 and the host interpreter. The 37 existing Terrain Diffusion
  unit tests and two source-cache tests passed. Artifact-writing/validation
  bodies are structurally identical to the pre-refactor source.
- Local documentation and foundation lanes passed 9/9 and 8/8. All 27 protected
  media/provenance and sealed-study files retained their hashes. No C++, public
  header, shader, numerical oracle or external-worker dependency changed.

Full local logs remain in the optional ignored
[follow-up receipt](../../../outputs/foundation-loop/20261018-bootstrap-publication/closure.json).
The [Terrain tools guide](../../../projects/terrain/tools/README.md#generated-bundle-replacement)
documents recovery, retained lock files and the short reader gap during
replacement. This pass did not regenerate production terrain or qualify CUDA
producers, external workers, GPU runtime behavior or human visual acceptance.
