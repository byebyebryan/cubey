# Terrain Cached Radial Integration

Date: 2026-07-16

Status: continuous-center cached integration rejected; radial v2 remains the
accepted macro-composition baseline; production hard cut remains unchanged.

## Decision

Do not promote either tested `cached-radial` candidate. Reducing the accepted
radial study to stride 2 or stride 3 preserves its far-field image, but neither
candidate passes the `terrain surface < 1.0 ms` p95 gate at 2560 x 1440.

This closes the index-only integration attempt. It does not reject the radial
composition or justify another source-shape iteration. The next bounded test is
an ownership correction: retain radial outer terrain and stride 3, but stop
rendering the continuous diagnostic center that the foreground consumer does
not need.

## Evaluated Contract

The opt-in lane preserves the accepted radial-v2 configuration exactly:

- `mountains-hierarchy-v2` source with seeds `0`, `9012`, and `12345`;
- `32.768 km` outer radius;
- `6 km` low-relief floor footprint;
- broad restoration over `1-24 km` and detail restoration over `5-30 km`;
- continuous center and full baked positions, normals, and material channels;
- `500 m` focus height and `100-1000 m` orbit;
- unrestricted yaw and `0-30` degree elevation;
- render stride 1 control against cached stride 2 and stride 3 candidates.

The product test confirms exact equality of baked center/sector vertices and
source topology across all three strides. Only `render_indices` and the derived
render-triangle count change. Runtime profiles now publish submitted sectors,
submitted triangles, product render capacity, and source sample count alongside
the GPU spans.

Run the maintained pack with:

```sh
projects/terrain/capture_cached_radial_backdrop.sh
```

It writes `outputs/terrain/cached-radial-v1/`. `REVIEW.md` defines the review
order and `review-metadata.json` contains the measured contract.

## Visual Result

Stride 2 and stride 3 preserve the full radial silhouette at the intended
far-field scale. Across three seeds and six headings there are no visible sector
holes, boundary cracks, or transition rings. Clay, normals, projected edges,
material weights, and stage ownership remain continuous.

Stride 3 provides no material visual regression against stride 2 in this pack.
As a supplemental pixel check, its normalized mean difference from stride 1 is
`0.00089-0.00180` across the six seed-9012 headings. That small difference is
not a substitute for review, but it agrees with the visual read.

The pack does not improve the source. Several headings still expose broad,
smooth mountain faces and limited secondary silhouette detail. Those are known
`mountains-hierarchy-v2` limitations, not reduced-index artifacts. At the
`1000 m / 30 degree` camera extreme, the orbit looks down onto the quiet center
and the distant relief leaves the frame. A real consumer must own that
foreground view; adding terrain detail to the diagnostic floor is not the
backdrop's responsibility.

## Performance Result

The RTX 5070 Ti profile uses 2560 x 1440, 30 warmup frames, and 146 measured
terrain-surface samples:

| Lane | Render capacity | Average submitted | Median ms | p95 ms | Gate |
| --- | ---: | ---: | ---: | ---: | --- |
| Full radial, stride 1 | 5,305,344 | 1,670,116 | 1.848 | 4.303 | Fail |
| Cached radial, stride 2 | 1,328,640 | 419,833 | 1.407 | 1.524 | Fail |
| Cached radial, stride 3 | 607,232 | 190,695 | 1.243 | 1.338 | Fail |

Both candidates submit about 11 of 48 sectors on average. All radial lanes bake
2,657,280 source samples. Stride 2 setup/first-frame measured `17,182 ms` and
`373,648 KiB` peak RSS; stride 3 measured `16,933 ms` and `363,788 KiB`. That is
comparable to the accepted workbench cache build, so it is retained persistence
debt rather than the reason for rejection.

The current production control recorded a `0.724 ms` median but a `2.981 ms`
p95 in this run because a small group of samples spiked around the warmup
boundary and at isolated headings. Its accepted dedicated pack remains the
production checkpoint. The radial candidates do not share that pattern: their
medians and p95 values are consistently above one millisecond, so their failure
does not depend on the noisy control.

## Next Bounded Test

Keep the source, radial gates, outer domain, stage, materials, sector partition,
and stride 3 fixed. Change only foreground ownership:

1. Do not submit the continuous center mesh in the production-shaped candidate.
   The radial height source may remain continuous for diagnostics and outer
   sampling.
2. Preserve the consumer-owned foreground contract and test whether the outer
   sectors meet the stage without a visible ring in representative scene views.
3. Re-run seed 9012 over six headings, the camera-envelope endpoints, ownership
   diagnostics, and the 1440p profile before paying for the full multi-seed pack.
4. Require `<1.0 ms` p95 with at least 120 samples. A visual pass alone still
   cannot promote the lane.

At stride 3 the center contributes exactly 66,560 render triangles and is always
visible. Removing it would reduce the measured average submission from about
190,695 to 124,135 triangles and avoid shading the full-screen diagnostic floor.
That is the highest-leverage untested correction consistent with the accepted
ownership model.

If that correction still misses the GPU gate, move to projected-size sector LOD
or separately decimated center/outer index levels. Do not resume radial-gate
tuning, change the terrain source, or add an unreviewed global stride 4 in the
same experiment.

## Stop Condition

The current `cached-radial` lane remains study-only. Production defaults do not
change. Reopen this decision only with evidence that a consumer-owned center or
screen-space index policy preserves the radial-v2 visual baseline and satisfies
the runtime gate.
