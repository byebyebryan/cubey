# SynxFlow dependency handoff

This optional, separate-process dependency powers the preferred live
[Terrain Water demo](../README.md). Nothing here is installed, compiled or launched by normal
Cubey configure/build/test/startup. Cubey's Vulkan renderer does not link CUDA.
Return to [demo setup](../demo_setup_v1.md) for terrain/case preparation and launch.

## Upstream credit and exact identities

- Project: [SynxFlow/SynxFlow](https://github.com/SynxFlow/SynxFlow).
- Reference source: [v1.0.1 commit 8a978150](https://github.com/SynxFlow/SynxFlow/tree/8a9781504204a7b41217b483db4190c1f6f340bc).
- [Upstream getting started](https://synxflow.readthedocs.io/en/v1.0.1/quickstart.html)
  and [PyPI 1.0.1 release/files](https://pypi.org/project/synxflow/1.0.1/).
  The documentation banner still says 0.0.5.0; our artifact pins, not the banner
  or a moving `master`, identify the tested implementation.
- Xilin Xia and Xiaodong Ming (2025),
  [SynxFlow: A GPU-accelerated Python package for multi-hazard simulations](https://doi.org/10.21105/joss.07586),
  JOSS 10(111), 7586. Upstream credits its HiPIMS-CUDA/Pypims lineage.

Cubey reuses the solver; it does not claim those numerical methods as its own.
[runtime_profile.json](runtime_profile.json) records URLs, source/wheel hashes,
toolchain versions and the existing qualified service hash. It is documentation
and validation metadata, **not** an override accepted by the launcher.

| Piece | Why it exists | Identity / boundary |
| --- | --- | --- |
| PyPI wheel | Python IO/case tooling and stock native API | `synxflow==1.0.1`, CPython 3.11 Linux x86_64 wheel, SHA-256 `31c4f0a94291f929b24e5c6811a0359c4dd94a6da012dcf6be8895f4bdc402e4`. |
| GitHub source archive | Source for the separately built controlled service | Commit `8a9781504204a7b41217b483db4190c1f6f340bc`, archive SHA-256 `281e84ac6945b7a10c04e11a226dd1bc56673f2fe4deb03f9095671f3ac5f297`. |
| Hooked `flood` extension | Pause/reset/rain/pace and bounded field publication | Qualified artifact SHA-256 `251cea1bd0658acdfc5b5129038d93970bf1769d7cac00bbf1edff99a886315f`. |

The stock wheel alone does **not** provide `run_service`. The build helper adds
host/application hooks; it checks that solver-library files and the stock
`run()` text remain unchanged. The PyPI source distribution and GitHub tag are
not interchangeable just because both say 1.0.1: the earlier source audit found
numerical differences. See [retained source/parity evidence](../../../studies/fluid_25d/harness/README_HISTORY.md#optional-source-build-audit).

The upstream release metadata declares GPLv3+; our worker/hook/build slice is
marked [GPL-3.0-only](LICENSE), with [complete license text](COPYING). The private
source build retains upstream `LICENSE.txt`. Cubey's renderer/client is separate
MIT code. This is the repository's declared boundary, not a blanket legal opinion
about arbitrary redistribution; preserve source, notices and build provenance
when distributing external-worker artifacts. No external binary is committed here.

## 1. Create the isolated Python environment

Supported lock scope: **Linux x86_64, glibc >= 2.28, CPython 3.11**; tested patch
3.11.17. This does not promise Windows, ARM, another Python ABI or an arbitrary
CUDA/toolchain combination. Provide Python with venv/pip support explicitly.

From the repository root, choose a new output leaf:

```sh
rtk proxy mkdir outputs/fluid/synxflow-setup-FRESH
rtk proxy /path/to/python3.11 -m venv outputs/fluid/synxflow-setup-FRESH/env
rtk proxy outputs/fluid/synxflow-setup-FRESH/env/bin/python -m pip --isolated install \
  --index-url https://pypi.org/simple --only-binary=:all: --require-hashes --no-cache-dir \
  -r projects/terrain_water/external_synxflow/requirements-linux-cp311.lock
rtk proxy outputs/fluid/synxflow-setup-FRESH/env/bin/python -m pip --isolated check
```

This is an explicit network/package-install step, confined to that venv; never
use sudo or system pip. The [29-package wheel lock](requirements-linux-cp311.lock)
includes pip/setuptools and transitive IO dependencies. Each version and the
specific platform wheel hash is fixed. A missing wheel, platform mismatch or
hash failure is a stop condition, not permission to drop `--require-hashes`.

The legacy IO code needs NumPy 1.26.4 (`np.trapz`), pandas 2.2.3 (pre-3.x
assignment behavior), and setuptools 80.9.0 (`pkg_resources`). Deprecation
warnings can remain; they do not justify silently resolving newer versions.
CUDA compiler/toolkit and the controlled service are **not** installed by pip.

For offline restoration, explicitly download the same lock with
`pip download --only-binary=:all: --require-hashes -r LOCK --dest WHEELS`, then
install with `--no-index --find-links WHEELS --require-hashes -r LOCK` inside
the venv. Keep wheel hashes with the handoff; copied site-packages are not a lock.

## 2. Restore a qualified service, or build a candidate

If you already possess the qualified service artifact, supply its path explicitly.
The old dated maintainer path is not a deployment requirement. Keep the venv
interpreter path for runtime; resolving its symlink to base Python loses packages.
Check native loader dependencies as well: the qualified artifact has an absolute
RPATH to the retained GCC runtime directory and needs compatible `libstdc++`,
`libgcc`, glibc and the NVIDIA driver. A matching file hash alone is not a portable
runtime bundle. Use the import check below to expose missing libraries; do not
patch the binary/RPATH and assume its old hash qualification still applies.

For a source build, provide existing CUDA 11.7.99, GCC/G++ 11.4.0, CPython 3.11,
CMake and Ninja. Those are the retained qualified toolchain versions, not a
claim that every newer combination works. Follow the
[official NVIDIA toolkit archive](https://developer.nvidia.com/cuda-11-7-1-download-archive)
for toolkit acquisition; no global driver/toolkit/compiler installation is
automated here. The tested host used an RTX 5070 Ti; current driver/GPU operation
must be checked separately. A source compiler and a runtime driver are distinct.

Download the immutable source, then use the explicit build helper:

```sh
rtk proxy curl --fail --location --max-time 120 \
  https://codeload.github.com/SynxFlow/SynxFlow/tar.gz/8a9781504204a7b41217b483db4190c1f6f340bc \
  --output outputs/fluid/synxflow-setup-FRESH/synxflow-source.tar.gz
rtk proxy python3 -B projects/terrain_water/external_synxflow/build_synxflow_service_v1.py \
  --source-archive outputs/fluid/synxflow-setup-FRESH/synxflow-source.tar.gz \
  --cuda-root /path/to/cuda-11.7 \
  --python outputs/fluid/synxflow-setup-FRESH/env/bin/python \
  --cc /path/to/gcc-11.4 --cxx /path/to/g++-11.4 \
  --out projects/terrain_water/external_synxflow/private-builds/setup-FRESH --jobs 6
```

The archive checksum is verified before extraction/toolchain/build work. The
output must be a fresh leaf **inside `external_synxflow/`**; private builds are
ignored. Retain `build-result.json`, configure/build logs, compiler identities,
changed-source hashes and the resulting extension hash. The helper does not
run a solver or install its output into Python/Cubey. Its resolved Python path
is used for build headers/ABI; the runtime launcher still needs the venv path.

**Build success is not admission.** A fresh build can produce another binary
hash even with the same source/toolchain. Do not change `EXTENSION_SHA` merely
to make setup pass. Path overrides retain the exact existing hash guard.

## 3. Verify imports and the admission boundary

Run the checker using the new venv, with library caches confined to your output:

```sh
rtk proxy env MPLBACKEND=Agg \
  MPLCONFIGDIR="$PWD/outputs/fluid/synxflow-setup-FRESH/matplotlib" \
  XDG_CACHE_HOME="$PWD/outputs/fluid/synxflow-setup-FRESH/cache" \
  outputs/fluid/synxflow-setup-FRESH/env/bin/python -B \
  projects/terrain_water/external_synxflow/check_runtime.py \
  --extension /path/to/qualified/flood.cpython-311-x86_64-linux-gnu.so
```

It checks host/ABI, every installed locked version, extension SHA, IO import and
the service API. It imports native libraries but **does not dispatch a solver**.
It rejects an unqualified hash before optional imports and has no hash-bypass
flag. Success is not a CUDA-execution, numerical-parity or lifecycle certificate.

For a new candidate hash, the maintainer qualification path remains explicit:

1. Retain the unchanged source/build audit and frozen comparison protocol.
2. Run `check_synxflow_source_correspondence_v1.py` separately with `--entry-point
   stock` and `--entry-point service-noop`, supplying every qualified baseline via
   repeated `--baseline-case`, plus `--extension`, `--protocol` and fresh `--out`.
   The retained gate has nine analytic controls and both mountain rain/recession
   cases. A passing subset or package import is not the whole gate.
3. Exercise real worker controls with `run_external_service_controls_v1.py`,
   supplying the candidate, case, MIT client probe, frozen controls protocol and
   fresh output. Check pause/reset identity, rain/pace, acknowledgements and cleanup.
4. Requalify lifecycle/failure behavior and bounded concurrent headless rendering
   before reviewing an intentional launcher pin update. Do not relax comparison
   tolerances or reuse another binary's receipts.

These tools live [one directory up](../../../studies/fluid_25d/harness/README_HISTORY.md#optional-gpl-external-service-worker).
Their historical case/protocol archives are optional ignored data, not checkout
assets. There is no self-service arbitrary-build admission switch today. If the
baselines or qualified artifact are unavailable, use builtin/replay rather than
calling an unqualified build supported.

## 4. Prepare a case and launch explicitly

Follow [terrain/case setup](../../../studies/fluid_25d/notes/demo_setup_v1.md#terrain-prerequisite), then pass
the venv and qualified extension to `run_terrain_rain_demo.py --print-command`.
After successful preflight, a bounded `--headless --run-seconds 20` run validates
actual worker startup/renderer attachment. Without a switch script it stays
dry/paused; proving moving water also needs acknowledged start/pause actions.
For that check, the tracked [bounded controls script](../tests/fixtures/headless-controls.json)
can be passed with `--switch-script` and `--run-seconds 30`. It starts Manual,
changes actual rain, pauses and resets dry; this is a lifecycle smoke, not a
performance or long-horizon numerical qualification.
Keep launch, worker/viewer logs, command acknowledgements and cleanup receipts.

Normal live worker mode is **continuous**. Prepared files specify 20 hours for
finite diagnostics; that is not a hard stop in continuous mode. Worker `--finite`
selects the input horizon explicitly. The guide ends its explanation at six
hours without stopping computation. Regression-script timestamps are currently
restricted to 0–72,000 s by the script parser, a separate test-interface bound.
Continuous does not promise unlimited float-clock precision or indefinite health.

## Without SynxFlow

Our builtin Vulkan solvers are retained and do not need CUDA, Python, generated
terrain or an external process. They are explicit alternatives, not automatic
fallbacks or equivalents of the featured mountain-rain results:

```sh
rtk proxy build/release/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-backend builtin --fluid25d-solver virtual-pipes
rtk proxy build/release/projects/fluid/fluid_25d/fluid_25d \
  --fluid25d-backend builtin --fluid25d-solver finite-volume
```

Recorded native fields can also use the featured renderer without CUDA or a
running solver, provided the matching recording/terrain assets exist. See
[backend capabilities](../../../studies/fluid_25d/notes/current_reference.md#solver-contract-and-ownership).

## Handoff validation — 2026-10-15

Local evidence: `outputs/fluid/synxflow-handoff-20261015-lhuBfJ/` (optional,
ignored data, not a fresh-checkout dependency). A new CPython 3.11.17 venv
installed all 29 hash-locked wheels and passed `pip check`, IO/service imports
and the retained Mountain preflight. A 30-second Vulkan-headless run with the
qualified extension acknowledged Manual start, rain 256 mm/h, pause and dry
reset. Native time reached about 252 s before reset; the new session returned
to zero. Both worker completion receipts report stopped, no failure and unchanged
working/reference inputs. All owned processes were reaped without escalation;
launcher cleanup records expected SIGTERM for worker wrappers, not all-zero exits.
No GUI or performance qualification is inferred.

A fresh download reproduced the pinned source-archive hash. A separate private
build with the retained toolchain passed source auditing and compilation, but
produced candidate SHA-256
`10fbd54d718bf1748371627c1462eed31a3c326f14eb11ea48fba7cf0f1b1084`.
Both the checker and unchanged launcher reject that candidate. No pin, numerical
code or tolerance was changed; new-build numerical/lifecycle qualification was
not performed or claimed. The original qualified service remains the live default.
