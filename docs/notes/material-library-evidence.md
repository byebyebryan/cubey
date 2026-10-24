# Material library expansion evidence

Date: 2026-10-23

Status: implemented and natively qualified under the
[library plan](material-library-expansion-plan.md). New appearances remain open
to human review. The [prior workshop evidence](material-workshops-evidence.md)
remains the compatibility and performance baseline.

[Open the material-library review report](http://starship:8787/reports/97f536f0e315dd8cb3fa726846f782fbb832ea85ddefa3b7a35b956b33e9d63f/):
57 native stills, three eight-second films, matched mesh/SDF specimens, optical
roughness strips, material components, editable-recipe snapshots and measured
costs. Publication contains 221 files / 20,231,386 bytes. Every file's HTTP bytes
match the prepared bundle from both Starship and Carbon. The shared report
server is reused; no source push or public-showcase deployment is included.

## Content checkpoint

`b3e9a371` adds recipe V2, the bounded shared catalog and resolved authored SDF
scene V3 materials. Recipe and workshop tests pass, including strict fields,
catalog identity/path failures, all named parameter groups, domain persistence,
standalone snapshot survival and exact legacy seven-material SDF fingerprints.
No new appearance was claimed by that data checkpoint.

## Rough-glass feasibility

Eight native 640 by 640 captures compare roughness 0.05, 0.25, 0.45 and 0.65 in
mesh and SDF spheres against the same generated sharp-bar HDR environment.
Required Vulkan validation passes on the RTX 5070 Ti, driver 610.57.04.
Local captures, native logs and hashed source/binary/input receipt live under
`outputs/material-library/feasibility`. The final report uses newly captured,
matched views at roughness 0.05, 0.32 (default) and 0.55.

Inspection shows progressively softened reflection and transmitted bars with
fixed geometry, neutral attenuation and fixed IOR. At the higher settings SDF
approaches a smooth bright average of this environment. This establishes that
the bounded filtering route is viable; it is not an acceptance claim for the
final preset or a comparison to measured glass.

Mesh reuses forward rough-transmission filtering. SDF accumulates an angular
spread estimate at interfaces and samples existing environment mip levels;
its central path and measured in-medium absorption distance remain intact.
Zero spread takes the legacy sharp environment path. SDF does not transmit
arbitrary opaque scene geometry or integrate a full multiple-ray rough BTDF.
The two renderers are intentionally not required to produce identical images.

The diagnostic generator is
[`library_environment.py`](../../projects/pbr_materials/tools/library_environment.py).
Mesh accepts that HDR with `--environment`; SDF material-study lighting accepts
it with `--material-environment`. Both use ordinary existing environment products.
The accepted clear/cloud atmosphere routes are unchanged.

## First surface auditions

The shared catalog now contains twelve recipes. Initial native auditions cover
all five new families in both consumers: a bored solid block for wood, oriented
thin swatches for rust/paper/cloth and a sphere for frosting. Ten fixture
captures pass required Vulkan validation. SDF also passes the geometry ray oracle
and CPU/GPU comparisons of color, roughness, relief gradient, metalness, sheen
and optics for all five fixtures.

The original gradient/normal tolerance remains 2e-5 for legacy materials. Fine
V2 periodic relief uses a separate 1e-4 bound for ordinary CPU/GPU trigonometric
rounding; geometry and optical-interface bounds are unchanged. Independent
finite differences verify the scalar relief gradient and nonuniform affine
chain rule for each new opaque family. Minified samples lose resolved bump
while retaining stable mean color/coverage and their rough response.

Initial visual review finds the wood/cut relationship legible and the rust's
metal/oxide response distinct. Cloth relief and paper flecks need a restrained
correction before final presentation. The local audition files are retained;
these images precede final preset selection and performance qualification.

The bounded correction reduces wood/rust relief, quiets paper flecks while
bringing fibers into the visible frequency range, and softens cloth relief.
Cloth over/under selection is centered on alternating yarn crossings. The
corrected ten-fixture audition passes native validation and SDF CPU/GPU checks;
paper remains a stylized directional-fiber study rather than a complete board
construction or a fuzzy paper simulation.

Both consumers now expose the shared catalog and family selection. Mesh uses a
four-column gallery with bounded table capacity. Reloading recipes validates a
complete candidate before publication; a mesh roster/order/optical-route change
requires restart so existing permanent handles retain their contract. Editing
the selected new family is covered by the bounded-resource stress path.

Final qualification is driven by
[`qualify_library.py`](../../projects/pbr_materials/tools/qualify_library.py),
which discovers the catalog, records per-run identities and uses adapter-specific
pitch signs to compare the same view.

## Final qualification

The rendered implementation is `cc4a248d`. Two optical default views were
recaptured using the matched-camera tooling at `3622d91e`, with unchanged
binary/shader identities. `bfc19fc0` completes report checks and provenance;
subsequent wrap-up documentation adds no renderer change. Per-run commands,
binary/shader/recipe hashes, resolved recipes/scenes and raw profile CSV/trace
files are retained in `outputs/material-library/qualification` and the report.
Its generator identity is recorded separately in `report.json`.

- All 57 final images and three films pass required Vulkan validation. The five
  new sphere cases and the rust CSG ownership cases also pass the SDF field and
  CPU/GPU material oracles. New family/roughness edits hit the geometry cache
  without rebuilding bricks; the existing approximately 871 MiB field allocation
  is unchanged.
- The selected rust, cloth and frosted-glass stress cases each update both used
  frame slots 1,000 times. Each slot retains twelve descriptors and 13,056 uniform
  bytes with zero growth. Routing-changing edits remain rejected.
- All twelve resolved recipes survive SDF scene serialization and export with
  matching content fingerprints. An embedded V3 snapshot also remains usable
  after its original recipe file is removed.
- Seven retained PNG controls remain byte-identical: clay/glass spheres,
  clay/glass guardians, furnace, IOR furnace and glTF. The original seven recipe
  values, v2 scene hashes and geometry/history contracts remain unchanged.
- The scoped core/header/foundation/layout/documentation/SDF gate passes 17
  CTest entries; both workshops pass all 33 documented CLI recipes. The initial
  broad CLI recipe gate encounters unrelated unbuilt executables in this release
  tree, so no repository-wide CLI pass is claimed. A stale unit-test expectation
  for the enlarged uniform packet was corrected to its new aligned size; the
  legacy prefix ends at byte 896 and the full packet is 1040 bytes.
- Headless Chrome 156 checks eight report interactions on desktop and mobile,
  decodes all 57 images, loads all three eight-second H.264 films and reports no
  script errors or horizontal mobile overflow. The same checks pass over the
  published HTTP URL. This is browser QA, not human artistic acceptance.

## Steady GPU costs

RTX 5070 Ti, driver 610.57.04, 1920 by 1080, 1x, required validation, sixteen
warmup frames and 94 recorded samples per case. Costs exclude field construction,
IBL generation and encoding. Scene complexity differs, so these are budget
checks rather than a direct renderer speed comparison.

| Consumer / specimen | Environment | Median ms | P95 ms |
| --- | --- | --- | --- |
| Mesh twelve-material gallery | Studio | 0.452 | 0.455 |
| Mesh twelve-material gallery | Clouds | 2.089 | 2.100 |
| Mesh cloth swatch | Studio | 0.160 | 0.162 |
| Mesh frosted sphere | Studio | 0.216 | 0.218 |
| Mesh frosted sphere | Clouds | 1.849 | 1.869 |
| SDF wood cut block | Studio | 1.591 | 1.643 |
| SDF rust CSG | Studio | 2.795 | 2.901 |
| SDF cloth swatch | Studio | 0.804 | 0.821 |
| SDF cloth swatch | Clouds | 2.404 | 2.436 |
| SDF frosted sphere | Studio | 2.707 | 2.836 |
| SDF frosted sphere | Clouds | 4.204 | 4.333 |

## Settled boundary

The twelve-entry shared library, its two workshops, bounded controls and
persistence are implemented. Wood's solid structure and rust's changing response
are the clearest new technical demonstrations. Cloth is a coarse weave with
restrained sheen; kraft is deliberately a stylized fiber surface. Frosting is
usefully visible in both consumers, with the stated SDF transport approximation.
Further realism, arbitrary cloth charts, corrugated/fuzzy geometry and a full
rough-refraction solver remain deferred. The guardian/robot modeling and their
accepted presets are preserved; the separate authored-content showcase is not
reopened by this milestone.
