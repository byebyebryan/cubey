# Material library expansion evidence

Date: 2026-10-23

Status: implementation in progress under the
[library plan](material-library-expansion-plan.md). New appearances are pending
human review. The [prior workshop evidence](material-workshops-evidence.md)
remains the compatibility and performance baseline.

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
`outputs/material-library/feasibility`; the final report will select a small
comparison from this set.

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
pitch signs to compare the same view. Current implementation work is still
awaiting its final compatibility, timing and published presentation receipts.
