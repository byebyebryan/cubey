# Offline cell-scale shoreline boundary study

This is a rendering-reconstruction study, not a solver, new terrain product,
production VTK dependency, or accepted bank fix. Numerical fields, native
geometry and defaults are unchanged. Pixel antialiasing is outside its scope.

The reference operations are scikit-image marching squares plus VTK constrained
point smoothing, compared with VTK Surface Nets 2D label-map extraction followed
by the same bounded polyline smoother. This is not an assertion of complete
VTK built-in Surface Nets smoothing parity: shared/branching graph vertices are
explicitly rejected, not silently reconnected into simple polylines.

Optional dependencies run outside Cubey's normal Python/build environment:

```sh
rtk proxy python3 -m venv /tmp/cubey-boundary-study-venv
rtk proxy /tmp/cubey-boundary-study-venv/bin/python -m pip install \
  numpy scikit-image vtk shapely matplotlib
rtk proxy /tmp/cubey-boundary-study-venv/bin/python -m unittest discover \
  -s projects/fluid/fluid_25d -p test_shoreline_boundary_study_v1.py -v
```

For exact replay, install from the handoff's pinned
`checkpoint/requirements.txt`. The ordinary environment skips these optional
tests rather than requiring heavy libraries. They are not added to the regular
CTest suite. The normal 159-test development gate was also rerun successfully.

Canonical evidence:

- `outputs/fluid/shoreline-boundary-study-20261006-eVRk0x/review/summary.html`
- `outputs/fluid/shoreline-boundary-study-20261006-eVRk0x/RESULTS.md`
- `outputs/fluid/shoreline-boundary-study-20261006-eVRk0x/protocol.json`

Create a fresh study leaf with `mktemp -d` under `outputs/fluid`, copy the frozen
protocol there, and run `run_shoreline_boundary_study_v1.py --out <leaf>`, followed
by `review_shoreline_boundary_study_v1.py --out <leaf>` in the study environment.
Each phase refuses to replace prior evidence. The scripts expect the existing,
hash-pinned native recording inputs used by the V4 presentation pass.

The 13 synthetic fixtures and four depth bands exercise streams, junctions,
positive-film/dry gaps, ambiguous diagonals, island holes, domain intersections,
wet lakes, and recession. Smoothing caps are total per-point displacements,
not per-iteration allowances. Original face labels transfer to moved faces by
stable vertex identity; moved centroids are not reclassified against raw h.

All uniform smoothed variants fail the frozen preservation gates. Light
marching-squares smoothing is the strongest research lead, not an accepted
candidate. The corrected recorded-field checks retain topology but give only
small reductions of the turning metric in the actual 30 m fields. No candidate
was imported into Cubey, no rendering/runtime performance claim is made, and
human visual acceptance remains separate.

The first `shoreline-boundary-study-20261006-148Z6U` run is retained as a
superseded face-label measurement preflight. Do not use it for acceptance.
