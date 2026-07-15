# Surface Ocean V1

Date: 2026-07-13

Surface Ocean V1 is the accepted horizon-scale checkpoint for
`projects/ocean`. It closes the current open-water look around the existing
spectral core, shared atmosphere/cloud lighting, a repeatable sea-state matrix,
and an explicit boundary for future terrain reuse. It does not claim finished
coastal, river, shallow-water, aerial, or planet-scale rendering.

## Accepted Surface Contract

- The renderer uses regular C0 and C1 spectral slots for every serialized sea
  state. C2-C4 remain neutral opt-in slots and are not preset cost.
- `Windy` is the default general-purpose state. `Calm` is reflection-led with
  restrained displacement and no persistent whitecap field. `Stormy` preserves
  the previously accepted ocean settings as the rough-water fallback.
- Sea state owns C0/C1 wind, fetch, displacement, normals, swell, directional
  spread, Jacobian whitecap response, foam accumulation, and a bounded set of
  water-material controls.
- Sea state does not own map size, field precision, enabled work count, camera,
  clouds, time of day, spectral domains, tile lengths, seeds, or directions.
- Applying a preset invalidates the spectrum and foam history because the wave
  source changed. It does not recreate wave resources because all presets keep
  the same field layout.
- Manual edits to preset-owned controls infer `Custom` in the GUI. Config files
  and CLI accept only `calm`, `windy`, and `stormy`, so saved runs remain
  deterministic.

The state progression is behavioral rather than a quality ladder:

| State | C0/C1 wind | C0/C1 fetch | Displacement character | Whitecaps |
|---|---:|---:|---|---|
| Calm | 5 / 4 m/s | 35 / 25 km | Low, broad ripples | None to occasional |
| Windy | 11 / 9 m/s | 150 / 110 km | Moderate open-water motion | Intermittent crest-aligned |
| Stormy | 18 / 16 m/s | 350 / 330 km | Accepted rough-water baseline | Persistent and extensive |

Exact preset values live in `projects/ocean/ocean_sea_state.cpp`; duplicated
defaults are covered by inference and config tests.

## Review Evidence

Generate the deterministic review pack with:

```sh
MOTION=0 projects/ocean/capture_ocean_review.sh outputs/ocean-surface-v1
```

The contact sheet compares Calm, Windy, and Stormy at low, mid, and high camera
scales, then checks shared clouds, dawn/dusk/night lighting, warmed foam,
reflection, shadow, specular, LOD, and far-field diagnostics. Use `MOTION=1` to
add fixed-lighting wave clips and a cloudy dusk sequence. All rows hold map
size, C0/C1 workload, domains, directions, seeds, and environment inputs fixed
unless the row explicitly changes them.

The accepted visual read is clear at low and mid scale. High views are still
mostly carried by the reflection/glitter handoff; resolved shape and foam fade
aggressively there. That is a known far-field material/LOD limitation, not a
reason to add more preset cascades.

## Water-Body Reuse Boundary

The open-ocean surface can later become a reusable renderer component, but one
sea-state enum is not a general water-body model.

- Open ocean, coasts, and very large lakes can reuse the spectral surface and
  water material once a caller provides a local frame, water datum, visible
  extent, depth/shore fields, and scene depth.
- Bounded lakes need a water mask, local datum, shoreline treatment, and depth-
  aware damping. They may reuse the spectral surface at sufficient scale, but
  should not inherit an infinite horizon or ocean fetch assumptions by default.
- Rivers, streams, ponds, wetlands, and flood water need separate local or
  flow-aware surface models. Sea-state presets do not replace current, shallow-
  water, wet/dry, or boundary conditions.
- Terrain owns bathymetry, shoreline signed distance, slope/material bands,
  local water levels, masks, and eventually flow hints. Ocean owns spectral
  displacement, normals, foam, reflection, and open-water material response.

`cubey::render::TerrainOceanFieldView` is the existing data vocabulary for
height, water depth, shoreline distance, and slope. The active ocean project
currently binds a synthetic diagnostic field and a small opt-in shoreline foam
hook; it is not yet consuming live terrain output. The separate terrain stream
should produce stable water-body products before a runtime adapter is added.

## Future Composition

The intended local composition order is:

1. shared atmosphere and cloud background/products;
2. opaque terrain and scene depth;
3. ocean or large standing-water surface against terrain depth and shoreline
   fields;
4. cloud/aerial overlays and HDR post.

Ocean defaults to a coherent reflected cloud view with a cached,
roughness-filtered cloud environment fallback. General geometry reflection and
aerial/orbit cloud reflection remain outside the local surface contract. After
terrain products merge, the next integration batch should consume real
bathymetry, shore distance, local datum, and scene depth before attempting surf
or shallow water. Near-field wakes, spray, refraction, and dedicated river/lake
solvers are later work.

Planet-scale navigation, global ocean topology, aerial/orbit clouds, and global
weather remain owned by `projects/planet` or future shared planet adapters.

## Closure Decisions

Accepted runtime paths:

- shared atmosphere and surface-cloud environment as the lighting authority;
- planar reflected clouds with cached environment fallback;
- local projected cloud transmittance and bounded wave self-shadowing;
- camera-relative clipmap geometry with altitude-aware sizing and culling;
- reflection-led far water with footprint-filtered detail and sun glitter;
- one configurable cascade array with C0/C1 enabled by presets.

Removed closure experiments:

- current-view and hybrid cloud reflections;
- feature-isolation presets and port-era cascade roles;
- spectral moment/mip handoff textures and compute work;
- synthetic far normals and filtered far-whitecap carriers;
- duplicate sea-state and cloud-specific capture harnesses.

These removals are deliberate. Reintroducing one requires new evidence against
the canonical review matrix rather than preserving it as another standing mode.
