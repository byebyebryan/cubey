# Repository tools

Run these stdlib Python tools from the repository root:

- `python3 -B tools/check_project_layout.py` checks maintained project layout,
  current documentation links, relocation targets, and retained-study boundaries.
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
