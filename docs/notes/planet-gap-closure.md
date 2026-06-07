# Planet Gap Closure Checkpoint

This note captures the completed broad planet foundation pass. The goal is to
keep the architecture doc focused on durable direction while this file records
the checkpoint, known gaps, and commit boundaries that got the project to its
current state.

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
- `PlanetSurfaceRuntime` ownership of live patch planning, previous-selection
  hysteresis history, render-origin validity checks, diagnostics, and instance
  buffer freshness;
- CPU and shader terrain sampling behind a project-local surface-field
  vocabulary;
- camera-relative rendering against a planet-scale local tangent frame;
- planet-owned solar clock, sun direction, moon direction, and body visibility;
- shared HDR post, performance UI, headless capture path, and project-local
  `PlanetUi` control-panel adapter.

## Gaps Closed

The completed batch closed the highest-leverage missing boundaries before new
surface layers arrive:

- wire sea level into the local tangent frame datum so terrain, ocean, and
  camera-relative height math agree;
- expose every terrain detail parameter through `RunConfig` and docs;
- add an explicit surface tile payload contract, even while the only provider
  is procedural, then expand its summaries with coverage, climate, roughness,
  material-count, and terrain-relevant revision data;
- make LOD transition pressure and neighbor deltas inspectable, then add the
  first neighbor-aware edge transition behavior;
- move planet camera position math to double precision and keep float
  conversion at upload boundaries;
- classify config changes so cheap replans do not rebuild topology or reset the
  camera;
- define planet atmosphere inputs as derived data from the planet celestial
  model;
- add planet atmosphere inputs and a physical-atmosphere path, later promoted
  from preview to default;
- improve moon material/phase/eclipse behavior through body rendering rather
  than sky-disk approximations;
- document repeatable visual captures for orbit, surface, dawn, day, night,
  moon occlusion, and atmosphere comparison cases.

## Commit Boundaries

This batch landed as a sequence of small commits:

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

The important implementation result is that planet now has explicit contracts
for surface field payloads, neighbor LOD diagnostics, edge transition masks,
double-precision camera state, dynamic-versus-topology config updates,
planet-derived atmosphere inputs, a physical-atmosphere mode that later became
the default, and upgraded moon body shading. The broad batch is complete; future
work should move back to feature slices rather than extending this checkpoint
indefinitely.

## Current Follow-Up

The latest follow-up batch is complete:

- added CTest-backed headless PNG smoke coverage for baseline headless output,
  surface dawn/day/night, orbit lit/terminator, daytime moon, wireframe LOD, and
  one terrain-field diagnostic;
- replaced the orbit exposure proxy with a view-aware visible-light estimate so
  rotating an orbit view does not cause abrupt brightness jumps;
- unified daytime moon atmosphere visibility around a sky-visibility term while
  keeping the moon opaque and depth-tested;
- extracted `PlanetUi` from `PlanetApp` so ImGui control layout is no longer
  mixed into the app runtime;
- extracted `PlanetSurfaceRuntime` from `PlanetApp` so surface planning,
  diagnostics, hysteresis history, render-origin checks, and instance-buffer
  upload freshness have an explicit boundary.
- added terrain-field debug views for land mask, moisture, temperature, and
  roughness so material/ocean-adjacent fields are visible without changing the
  final renderer.
- made camera height semantics explicit: datum altitude, sampled terrain height,
  and terrain-relative clearance are separate frame values, and near-plane /
  local-detail decisions use clearance where appropriate.
- replanned surface LOD from view scale by tracking clearance and near-surface
  tangent movement, so surface navigation does not reuse orbit-scale patch
  plans too long.
- added single-step neighbor LOD repair and terrain-displacement screen-error
  bounds, so the selected surface has tighter seam behavior and terrain-aware
  refinement pressure.
- clarified local detail as diagnostic-only for now. The debug views remain
  useful for ownership, blend, and height inspection, but final-view integration
  is blocked on a deliberate local/global handoff policy.
- aligned moon/atmosphere docs with the actual body pass: depth-tested moon
  geometry with premultiplied phase/daylight visibility, not a shared sky-owned
  moon sprite.
- added the missing sky-side moon contract: blended moon phases may visually
  merge into smooth sky, but procedural stars are masked behind the full
  rendered moon disk.

The remaining near-term work should move back to feature slices or targeted
hardening. It still should not port ocean, add real GIS data, build an
out-of-core streamer, add clouds, or introduce a full ephemeris until the
planet frame, LOD, and surface-field contracts need that extra pressure.
