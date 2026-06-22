# Geometry Moon Migration

The visible moon is moving to the shared `CelestialBodyFrame` geometry path.
This keeps moon placement, phase, and view-dependent scale outside the
fullscreen atmosphere shader and avoids maintaining both a shader disk and a
body renderer as equal runtime paths.

## Decisions

- Geometry is the canonical app-visible moon for atmosphere, planet, and the
  atmosphere-backed fluid demos.
- The generated lunar atlas remains the v1 appearance source.
- The atlas is a near-side disk atlas, not an equirectangular sphere map, so
  geometry samples it by projecting surface normals into a disk basis.
- The atmosphere shader keeps moon uniforms for moonlight, night-sky washout,
  star masking, and debug atlas views.
- Generic forward PBR, ocean, and reflection probes are follow-up consumers.

## Commit Sequence

1. Document the migration and current boundaries.
2. Make `CelestialBodyFrame` atlas-backed and depth-mode aware.
3. Bind the lunar atlas into planet moon geometry.
4. Add geometry moon rendering to the standalone atmosphere project.
5. Add geometry moon rendering to atmosphere-backed fluid scenes.
6. Disable the inline atmosphere moon disk where geometry is now active.
7. Refresh captures under `outputs/sky-moon-geo-migration-001/` and record the
   remaining visual follow-ups.

## Validation

Run focused atmosphere and planet tests, the sky test label, and headless smoke
captures for atmosphere, planet, fire/explosion, and water. Finish with
`git diff --check` and a clean `git status --short`.
