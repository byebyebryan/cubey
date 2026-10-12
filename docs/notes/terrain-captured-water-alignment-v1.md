# Terrain captured water alignment result

Date: 2026-10-11

The captured-water loop stopped at its first geometry gate. The source and
coordinate mapping are correct, but the shared Terrain mesh does not preserve
the ground surface against which Fluid's saved water is rendered. A direct
overlay would bury many shallow streams or leave gaps below their water surface.
No second-state bake or renderer extraction should precede an alignment fix.

The [HTML report](../../outputs/terrain/captured-water-alignment-20261011-TTM1hh/index.html)
contains matched maps and receipts. It is also served at
`http://starship:8801/terrain/captured-water-alignment-20261011-TTM1hh/`.

## Measurements

The probe uses the existing approved Mountain 1024-square recording at 16 m,
unchanged source focus, and exact triangles from the shared Terrain product
builder. The explicitly covered ground view has radius 8192 m and uses the
accepted continuous, seam-matched High topology. The normal 16384 m backdrop
disk would not fit this source focus; this is a matched patch test, not an
admitted full-size default Terrain placement.

The source sampler agrees with the saved sampled source bed to within
0.00048828125 m. All 823592 native grid positions inside the disk find a ground
triangle. Water reference heights use the existing positive B-spline basis
applied to both numerical bed and depth, sampled at vertices common to the
retained 2x water mesh. Coordinates, units, and vertical placement are not the
cause of the metre-scale mismatch.

| Ground control | Ground triangles | Absolute bed mismatch p95 | Buried water samples |
| --- | ---: | ---: | ---: |
| Runtime filtering and stride 3 | 742368 | 1.75 m | 35.2% |
| Runtime filtering and full stride 1 | 5305344 | 1.01 m | 11.9% |
| Unfiltered LOD0 and stride 3 | 742368 | 1.50 m | 32.8% |

The main group contains 215742 samples with reconstructed water depth at least
0.05 m. Burial means the ground triangle is more than 0.02 m above the saved
water surface. Fractions are spatial sample counts, not visible screen pixels
or a count of lost streams. In the 0.05 to 2 m depth subset, burial is 38.8%,
13.1%, and 36.2% respectively.

The declared early rejection limits were at most 1% buried samples and at most
5% with bed mismatch exceeding the larger of 0.1 m and one quarter of water
depth. All controls fail both limits. Full density increases triangle count
7.1x without solving alignment. These are conservative geometry rejection
checks; a future numerical pass would still need subcell and rendered review.

## Why the working Fluid view is different

Fluid draws ground and water from the same B-spline reconstruction and uniform
mesh. Their displayed separation stays equal to reconstructed depth. Terrain
instead samples a footprint-filtered source onto radial/sector triangles. The
same source identity therefore produces a different displayed ground surface.
This is a consumer reconstruction mismatch, not evidence of a failed solver or
corrupt captured fields.

Increasing rain, lifting all water, or replacing its reference bed with Terrain
samples would obscure the mismatch rather than preserve captured lake levels
and stream placement. Unfiltered sampling and increased density remain study
controls; neither is promoted as a runtime fix.

## Bounded next decision

Keep optional captured water as the direction. The next proposed pass should
prove one water-compatible ground patch using the same reconstruction and mesh
topology as the saved water, while retaining Terrain's existing backdrop beyond
that patch. Ground and water must share placement, material/lighting inputs,
and resource lifetime handling. Overlapping original ground must be replaced
or excluded rather than left to hide the compatible surface through depth
testing. Patch boundaries need a dry-margin/seam review.

That is a renderer integration decision, not terrain carving, source generation
or hydrology research. Do not implement a new backdrop architecture in this
loop. If a local compatible patch cannot remain bounded, retain Fluid as the
working consumer and defer Terrain composition. Rain-fed/post-rain capture,
the shared asset contract, and renderer extraction remain later gates of the
original plan, not completed features.

## Validation and retained behavior

Eleven selected CTests passed, including the new triangle/B-spline controls,
six report-helper tests, and existing recording immutability and surface-gradient
GPU checks. Flat-lake, sloped-bank, winding, shared-edge, coverage-hole, malformed
topology, and invalid-field controls exercise the private sampler. GPU checks
use genuine headless Vulkan; the worker-host test is a fixture, not live CUDA.

All three final trials use one frozen probe binary. Original bed, depth,
momentum, terrain source, and the approved Fluid render runtime remain unchanged.
The existing sealed Fluid presentation report revalidates. Pre-freeze probes
are retained separately and excluded from the final comparison.

The new executable is an explicit, excluded-from-default-build study tool.
It adds no solver launch, CUDA dependency, or public Terrain API. Diagnostic maps
are CPU geometry evidence, not new scenic renders or human visual acceptance.
No GUI or new hydraulic run was started, and no production rendering default
was changed in this loop.
