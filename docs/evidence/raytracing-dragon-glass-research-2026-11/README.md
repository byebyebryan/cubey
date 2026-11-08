# Dragon glass research

Date: 2026-11-07, following local opaque-material commit
`c02a938a625a8b418986c243ed71cddfd4289d04`.
[Proposed glass plan](../../notes/raytracing-dragon-glass-plan.md).

- [Boundary/topology and feature audit](boundary-audit.json)
- [CGAL predicates and positive/negative controls](cgal-audit.json)
- [Intersection pairs and seven affected regions](intersection-regions.json)
- [Research provenance and preservation checks](research-identity.json)

These are read-only measurements of the exact pinned cached GLB. Exact
coincident positions were identified and 108 zero-area faces excluded in memory
for an analysis-only OFF boundary; no production model was prepared or modified.
The resulting closed manifold surface has 201 actual self-intersection pairs
across 190 faces in seven regions. There are no degenerate self-pairs among
those 201. Closed topology and positive algebraic volume do not certify an
embedded optical solid.

CGAL ran from the pinned `cgal==5.6.post202402071826` Python 3.11 wheel, with
separate crossing/clean tetrahedron controls. No system package installation or
renderer dependency was added. Python 3.13 had no compatible wheel; its source
build failed before use, so all reported predicates use the successful 3.11
environment. The numerical boundary audit uses NumPy 2.5.4 independently.

The original scripts and analysis-only mesh remain in ignored
`outputs/raytracing/dragon-glass-research-20261107/`; reproduction commands are
in the plan. The original cached asset and committed runtime are preserved.
Mesh repair, glass implementation and visual acceptance remain future work.
