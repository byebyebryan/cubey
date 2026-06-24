# Geometry Moon Migration

Update: visible moon geometry now uses the spherical `LunarSurfaceMap` described
in `sky-moon-surface-detail-plan.md`. This note remains as the historical plan
for unifying visible moon rendering onto geometry.

The visible moon is moving to the shared `CelestialBodyFrame` geometry path.
This keeps moon placement, phase, and view-dependent scale outside the
fullscreen atmosphere shader and avoids maintaining both a shader disk and a
body renderer as equal runtime paths.

## Decisions

- Geometry is the canonical app-visible moon for atmosphere, planet, and the
  atmosphere-backed fluid demos.
- Superseded: the generated lunar atlas was the first geometry appearance
  source. Visible geometry and moon debug views now use an equirectangular lunar
  surface map; the old near-side disk atlas has been removed.
- The atmosphere shader keeps moon uniforms for moonlight, night-sky washout,
  and star masking, but no longer renders the moon disk.
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
8. Replace the old `moon` / `moon-surface` debug atlas views with geometry
   debug rendering. Done in `atmosphere(moon): render debug views as geometry`.
9. Remove the old `LunarAtlas` generator, descriptor binding, and shader disk
   path. Done in `render(atmosphere): remove legacy lunar atlas`.

## Current Runtime Boundary

- Planet, standalone atmosphere final view, fire/explosion atmosphere
  backgrounds, and water surface atmosphere backgrounds now render the visible
  moon through `CelestialBodyFrame` geometry.
- The atmosphere shader no longer renders a moon disk. It still receives moon
  state for moonlight, star masking, and sky washout.
- The `render_moon_disk` config flag currently means "show the visible moon" at
  the public config boundary. Migrated apps consume it as geometry moon intent,
  then pass a copied atmosphere config with inline moon rendering disabled to
  the fullscreen atmosphere shader.

## Validation

Run focused atmosphere, planet, pyro, and water tests, the sky test label, and
headless smoke captures for atmosphere, planet, fire/explosion, and water.
Finish with `git diff --check` and a clean `git status --short`.
