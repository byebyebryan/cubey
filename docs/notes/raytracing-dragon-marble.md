# Flat marble base for the dragon

Date: 2026-11-07. Local candidate based on published `3c1cda12`.

The current revision uses an asymmetric **convex** footprint, keeping the flat
caps and 30 mm thickness. The earlier cutaway silhouette was rejected; its
geometry and captures remain dated references.

[Current convex-base native comparison and outline overlay](http://starship:8787/reports/14ecd419956f24dba153ffb7236ebd76ec9e833825dc4214274cd89c73174586/).
All 17 published files match their authored bytes over HTTP.

[Previous cutaway silhouette](http://starship:8787/reports/a7de6e03f530fdde6ee9b361305d3a28b5e8972cee6e7aab32bf9d2faa093421/).

The current direction is a flat stone slab with irregular cut edges, with the
shared Balanced marble recipe on the **base**. The dragon starts in clay and
can independently switch to the existing sharp mirror. The previous undulating
and subdivided rock studies did not read as rock; the marble-on-dragon variation
also put the requested material on the wrong object. Their immutable reports
remain dated visual references, rather than the current scene specification.

Run `projects/raytracing/presets/optics/dragon-marble.json`. The native **Base
material** selector and `--support-material matte|marble` affect the slab only.
**Dragon surface** and `--subject-material clay|mirror` affect the dragon only.
Marble pattern/relief toggles affect the base, independently of the dragon RGB
and roughness controls. Both selections round-trip through Save view.

## Geometry and rendering

The support fits asymmetric outer candidates around the foot contacts, then
computes their convex hull. Both the CPU test and the independent actual-mesh
check require every vertex to lie inside every outward face plane. Its scale
follows the lowest 15 mm band and preserves the lowest 5 mm foot contacts. It has
flat horizontal caps, vertical cut faces and uniform 30 mm thickness, with no
height displacement or subdivision. Its 44 triangles span approximately
0.835 by 0.527 m.
The scene contains exactly two objects under the shared clear atmosphere.
The prepared dragon's topology and placement remain unchanged.

The base consumes the unchanged canonical `assets/materials/marble.json` through
the shared recipe record and object-domain evaluator. Only material ID 1 is
eligible for that recipe; the dragon retains its existing clay/mirror path.
Object-space marble veins continue across the top and cut sides. The surface
uses existing environment PBR shading and traced sun shadows. This revision
adds no GI, progressive primary sampling or rough scene reflections.

The legacy `rock` support option now denotes this slab. The clay/rock preset
starts with a matte slab. The original reflective-dragon/platform preset retains
its exact image. The old rounded rock and marble-on-dragon results remain in
previous immutable report snapshots and source recovery copies.

## Validation and evidence

Current artifacts live in `outputs/raytracing/dragon-marble-convex-20261107/`.
The rejected cutaway study remains in `dragon-marble-outline-20261107/`.
The previous chipped and eight-cut versions remain in their respective
`dragon-marble-cuts-20261107/` and `dragon-marble-slab-20261107/` directories:

- Direct 1080p native marble-base, patterns-off, matte-base, camera arc, top view,
  contact detail and reflective-dragon controls, plus the previous concave
  base at the same thickness, material, camera and light. A top-view SVG overlays the
  actual old/new mesh boundaries and the dragon's foot contact points.
- Independent checks of the actual scene dump confirm exactly two height
  planes, flat cap normals, vertical cut faces, a closed consistently wound
  convex support, positive volume, finite unit normals and the correct material roles:
  clay dragon, active marble support recipe.
- Every one of the 5,758 vertices within 5 mm of the dragon minimum has support
  beneath it. Vertical gap is 1.0 mm minimum, 4.0 mm median, 5.6 mm p95 and
  6.0 mm maximum. The closer view exposes the model's uneven underside.
- The focused scene/configuration and slab geometry test passes for the current
  cut refinement. The prior 18-test RT suite receipt remains scoped to the eight-cut
  version; this refinement changes only the support mesh.
- The saved headless view reloads to a PNG-byte-identical 1080p image. The
  original reflective-dragon/platform preset remains PNG-byte-identical at
  1280 by 720.
- Prior eight-cut owned Xvfb checks passed clay/mirror selection, independent matte/marble base
  switching, base pattern edit/restore, Save view and resize/restore, retaining
  three AS builds and three target generations.

Fine silhouettes retain the existing single-primary-sample aliasing. This is a
local composition audition; source changes remain uncommitted, and public-site
promotion and human artistic acceptance remain separate.
