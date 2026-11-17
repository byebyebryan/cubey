# Forward dragon audit evidence

2026-11-16. See [findings and scope](../../notes/raytracing-forward-dragon-audit.md)
and the [corrected browser comparison](http://starship:8787/reports/d2e6bc85ea3e7e4ff14a220759d984a3637ee3fb423d8886b0b5984b849e456e/).

- `dragon-controls.json`: production material components, four-IOR arithmetic,
  32 independent GLB entry/normal spot checks, thickness scaling and sky-only
  IOR-1 null control. Uses observed shading normals for the volume-ray oracle;
  not an exact-mesh-exit or energy reference.
- `shared-forward-controls.json`: fresh execution of eighteen established
  forward sphere/source controls; identical fragment SPIR-V to the adapter.
- `identity.json`: executable/source/shader identities, preserved model/recipes,
  unchanged RT shader, isolated tangent/source image deltas and replay checks.
- `validation.json`: current source checks, native receipt, clean output-error
  path, browser checks, RT and documentation regression summaries.
- `rt-tests.log`, `documentation.log`: focused repository checks.

Raw data and reproduction artifacts remain ignored under
`outputs/raytracing/forward-dragon-audit-20261116/`. Its before-source snapshot,
original images and failed overly strict draft oracle runs are retained. The
final controls explicitly use practical raster-binding tolerances; actual
differences and excluded sky/local-object scope are reported rather than
silently interpreted as exact optics. The final report isolates one variable
at a time and preserves the earlier report as a dated comparison.
