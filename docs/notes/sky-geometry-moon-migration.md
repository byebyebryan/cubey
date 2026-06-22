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

1. Document the migration and current boundaries. Done in
   `docs(sky): plan geometry moon migration`.
2. Make `CelestialBodyFrame` atlas-backed and depth-mode aware. Done in
   `render(moon): make celestial body frame atlas-backed`.
3. Bind the lunar atlas into planet moon geometry. Done in
   `render(moon): make celestial body frame atlas-backed`.
4. Add geometry moon rendering to the standalone atmosphere project. Done in
   `atmosphere(moon): render visible moon as geometry`.
5. Add geometry moon rendering to atmosphere-backed fluid scenes. Done in
   `fluid(moon): render sky moon as geometry`.
6. Disable the inline atmosphere moon disk where geometry is now active. Done
   for planet, standalone atmosphere final view, and atmosphere-backed
   fire/explosion/water backgrounds.
7. Refresh captures under `outputs/sky-moon-geo-migration-001/` and record the
   remaining visual follow-ups.

## Current Runtime Boundary

- Planet, standalone atmosphere final view, fire/explosion atmosphere
  backgrounds, and water surface atmosphere backgrounds now render the visible
  moon through `CelestialBodyFrame` geometry.
- The atmosphere shader moon disk remains for `Moon` and `MoonSurface` debug
  views and for unmigrated generic consumers that do not yet insert geometry.
- The `render_moon_disk` config flag currently means "show the visible moon" at
  the public config boundary. Migrated apps consume it as the geometry moon
  enable/brightness control, then pass a copied atmosphere config with the
  inline disk disabled to the fullscreen atmosphere shader.

## Validation

Run focused atmosphere, planet, pyro, and water tests, the sky test label, and
headless smoke captures for atmosphere, planet, fire/explosion, and water.
Finish with `git diff --check` and a clean `git status --short`.
