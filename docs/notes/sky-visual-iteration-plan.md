# Sky Visual Iteration Plan

This note scopes the first visual pass after removing the legacy planet
`SkyFrame` backend. The goal is to improve the unified atmosphere sun
presentation while keeping the sky ownership model stable.

## Scope

In scope:

- clean standalone atmosphere captures without the optional red ground reference;
- stronger validation captures for planet dawn, antisun dawn, orbit limb, moon
  occlusion, and daytime moon washout;
- a bounded sun disk/halo tuning pass in the shared atmosphere shader;
- review notes that record generated images under `outputs/`.

Out of scope:

- reintroducing a selectable planet sky backend;
- moving the sun to explicit body geometry;
- changing moon body ownership or moon washout behavior;
- replacing procedural stars or Milky Way assets;
- starting LUT-backed transmittance, sky-view, or multi-scattering work;
- terrain, cloud, ocean, or local-detail changes.

## Planned Commits

1. Record this plan and link it from the notes index.
2. Add a headless atmosphere CLI/config toggle for reference geometry so clean
   review captures do not include the debug grid marker.
3. Add focused planet sky PNG smoke views for sun-facing dawn, antisun dawn, and
   moon occlusion.
4. Generate and review the pre-tuning capture set under
   `outputs/sky-visual-pass-001/`.
5. Add a bounded shared-atmosphere sun halo around the existing sun disk.
6. Generate and review the post-tuning capture set under
   `outputs/sky-visual-pass-001-post-sun/`.

## Acceptance

- `ctest --preset dev -L sky --output-on-failure` passes after the smoke-test
  additions.
- Standalone review captures can be generated with the ground reference disabled
  from the command line.
- Planet dawn captures show a clearer sun disk/glow without turning the sky into
  a space-backdrop look.
- Historical legacy captures remain reference material only; active validation
  uses the unified atmosphere path.
