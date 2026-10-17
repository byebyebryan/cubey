# Repository tools

Run these stdlib Python tools from the repository root:

- `python3 -B tools/check_project_layout.py` checks project layout, discovered
  documentation front doors, relocation targets and protected study boundaries.
- `python3 -B tools/check_documentation.py` checks all tracked, repository-owned
  Markdown links/anchors and navigation from the root README. Directory links
  follow their README; vendored docs and ignored private builds are excluded.
- `python3 -B tools/check_documentation.py --check media` verifies current gallery
  files, sizes, SHA-256, PNG dimensions, MP4 fast-start layout and player order.
  Historical recipes retain their original hashes; this check reads the current
  manifest rather than treating superseded file paths as current assets.
- `python3 -B tools/report_server.py` serves local reports under `outputs/`.
  See the [report browser guide](../docs/getting-started.md#browse-local-result-reports)
  for configuration and operation.

Regression tests live in `tests/tools/` and run through `ctest --preset dev`.
The build-policy fixtures configure tiny CMake projects without building Vulkan;
the report-server tests use an ephemeral loopback HTTP server. Neither suite
requires a GPU or external network. To run the focused suites directly:

```sh
python3 -B tests/tools/test_project_layout.py
python3 -B tests/tools/test_cmake_app_policy.py
python3 -B tests/tools/test_report_server.py
```

Shader incremental-build fixtures: `python3 -B tests/tools/test_shader_build.py`.

Foundation include boundaries: `python3 -B tools/check_foundation_layers.py`.
Negative fixtures: `python3 -B tests/tools/test_foundation_layers.py`.
Compile configured public headers independently with
`cmake --build --preset dev --target cubey_header_checks`, or run the
`foundation_public_headers` CTest group. The header target is excluded from the
ordinary build and uses each owning library's public usage requirements.

The `foundation_layers` CTest group also checks the configured CMake library
link graph. CMake evaluates direct and interface link lists for every configured
build variant. The checker resolves target aliases and follows transparent
interface/object wrappers, stopping at each independently checked library
boundary and compiled foreign libraries. Missing or unevaluated evidence fails.
Directory-local imported targets used on a checked path must be exposed globally
so they can be inspected.
To run both checks directly after configuration:
`python3 -B tools/check_foundation_layers.py --target-graph build/dev/foundation-targets.json`.
Keep the adjacent generated `.json.links/` directory with that manifest.

## Documentation gate

After configuring and building the normal dev tree, run `ctest --preset dev-docs`.
It selects navigation, layout, media integrity, domain presentation checks and
native CLI recipes. It needs Git and stdlib Python; it does not need a GPU,
desktop, CUDA worker, terrain cache, media decoder or network access.

`repo_docs.py` supplies the shared Markdown and media primitives used here and
by the retained Fluid/glTF presentation tests. Fenced/inline code is excluded
from link checks; inline links, reference definitions, HTML media links, encoded
paths, explicit anchors and repeated heading anchors are checked. The two sealed
Fluid narratives retain their old links as intentional history; layout checks
independently protect their bytes. New tracked pages need a link from an owned
index or guide. Navigation cannot decide whether prose is current: status,
ownership, evidence scope and reopen decisions still require review.

CMake generates `build/dev/documented-apps.json` from registered application
targets. The recipe check discovers literal shell commands in the root/setup
and configured application READMEs, substitutes the configured binary path,
and appends `--write-config-template` to use the shared startup early-exit.
It validates option parsing, not asset availability or rendered results. Test
binaries, disabled applications and preparation commands are outside this check.
The setup guide's caller-owned `ocean.json` example uses a fresh generated
template; `--help` is checked directly where documented. Nonliteral recipes,
missing binaries, parser errors, missing templates and empty selection fail.
The CTest recipe lane supplies unusable Vulkan ICD paths and empty display
variables; no rendering is allowed as a side effect of a recipe check.

The primitive/negative fixtures run directly with
`python3 -B tests/tools/test_documentation.py`. For parser-only readback from a
configured build, use:

```sh
python3 -B tools/check_documentation.py --check recipes \
  --app-manifest build/dev/documented-apps.json --json
```
