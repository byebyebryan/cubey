# Rounded tapered stroke segment

Date: 2026-10-22

The [sculpture auditions](sdf-sculpture-study-evidence.md) expose blunt horn tips
and swollen branch joints when a varying-radius curve is approximated by
constant-radius capsules. Add one `round-cone` primitive, then construct curves
at authoring time from a short sequence of these segments. This is a polyline
with tapered rounded segments, not an analytic spline or a deformation.

## Geometry contract

The solid is the convex hull of two spheres centered at local Y =
`-half_length` and `+half_length`, with `bottom_radius` and `top_radius`.
Use the existing rigid transform and positive uniform scale contract.
Half-length is positive. Radii are nonnegative, with at least one positive.
Equal radii give a capsule; a zero radius gives a pointed endpoint. If one
sphere contains the other, reduce exactly to the larger sphere. This includes
the boundary where radius difference equals center separation.

The implementation derives the distance in a meridian plane. Let center
separation be H, radius slope be `(top - bottom) / H`, and let A be
`sqrt(1 - slope²)`. Outside the containment case, the common tangent has unit
direction `(slope, A)` and outward unit normal `(A, -slope)`. Project the sample
onto that tangent: below/above its two contact points, use the corresponding
sphere distance; between them, use the signed distance to the tangent line.
Revolving this construction about Y gives the solid distance.

This is an exact signed distance to the convex solid and is 1-Lipschitz.
Its cap/side transitions share value and normal. Therefore it fits the existing
cell-center distance/radius classification without a weaker pruning bound.
Rigid transforms preserve that bound; uniform scaling rescales both position
and distance. Existing min/max and polynomial smooth operations retain the
same conservative bound. The implementation is derived from this geometry;
no external primitive source code or dependency is imported.

The preview box conservatively uses the larger endpoint radius in X/Z and
`half_length + max_radius` in Y. The full geometry participates in normal
material selection, export/reload, fingerprints and saved leaf-reveal history.
A tapered segment is one leaf and one history step. A curved authoring stroke
can contain several such segments with smooth joins. No new scene version,
program/atlas capacity or interactive history operation is introduced.

## Qualification

CPU tests compare against an independent one-dimensional numerical minimizer:
the union of spheres along the segment with linearly interpolated radius.
They include increasing/decreasing radii, a zero-radius endpoint, containment,
equal-radius capsule equivalence and finite-difference gradient bounds.
Native fixtures cover transformed taper, contained spheres, a pointed endpoint
and smooth subtraction; the existing ray/direct CPU-GPU evaluator and material
checks run on each. Sculpture history qualification additionally exercises the
new primitive as both an addition and a previewed cutter.
