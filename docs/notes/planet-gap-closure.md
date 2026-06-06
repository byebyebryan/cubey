# Planet Gap Closure Checkpoint

This note captures the current planet-project context before the next broad
foundation pass. The goal is to keep the architecture doc focused on durable
direction while this file records the near-term batch, known gaps, and commit
boundaries.

## Current State

`projects/planet` is now the owner of planet-scale frame, LOD, terrain
placeholder fields, local sky, and mean celestial state. It renders a
cube-sphere planet through an HDR scene color path, uses project-local
atmosphere terms for the sky and surface haze, and draws the moon as
depth-tested body geometry. The project remains intentionally separate from
`projects/ocean`: ocean has reached a useful horizon-scale stopping point, and
planet should host it later once frame, LOD, and atmosphere contracts are
stable.

The important current contracts are:

- stable surface patch addresses in `face/level/x/y` form;
- reusable patch-grid geometry with per-frame patch instance uploads;
- CPU and shader terrain sampling behind a project-local surface-field
  vocabulary;
- camera-relative rendering against a planet-scale local tangent frame;
- planet-owned solar clock, sun direction, moon direction, and body visibility;
- shared HDR post, performance UI, and headless capture path.

## Gaps To Close

The next batch should close the highest-leverage missing boundaries before new
surface layers arrive:

- wire sea level into the local tangent frame datum so terrain, ocean, and
  camera-relative height math agree;
- expose every terrain detail parameter through `RunConfig` and docs;
- add an explicit surface tile payload contract, even while the only provider
  is procedural;
- make LOD transition pressure and neighbor deltas inspectable, then add the
  first neighbor-aware edge transition behavior;
- move planet camera position math to double precision and keep float
  conversion at upload boundaries;
- classify config changes so cheap replans do not rebuild topology or reset the
  camera;
- define planet atmosphere inputs as derived data from the planet celestial
  model;
- add an opt-in physical-atmosphere preview path without replacing the current
  analytic sky by default;
- improve moon material/phase/eclipse behavior through body rendering rather
  than sky-disk approximations;
- document repeatable visual captures for orbit, surface, dawn, day, night,
  moon occlusion, and atmosphere comparison cases.

## Commit Boundaries

Keep this as a large batch of small commits:

1. docs checkpoint;
2. surface config parity and sea-level datum;
3. surface tile payload contract;
4. LOD transition diagnostics;
5. neighbor-aware LOD edge behavior;
6. double-precision camera state;
7. dynamic replan versus topology rebuild split;
8. planet atmosphere input adapter;
9. physical-atmosphere preview mode;
10. moon rendering upgrade;
11. visual capture recipes and final doc sync.

These pieces are mostly independent, but several touch `planet_app`,
`planet_frame`, and shared config parsing. Land them in this order to keep each
diff reviewable and to avoid hiding behavior changes inside scaffolding commits.

## Deferred

This batch should not port ocean, add real GIS data, build an out-of-core
streamer, replace the current sky outright, add clouds, or introduce a full
ephemeris. Those are later planet-product features once this foundation is
less fluid.
