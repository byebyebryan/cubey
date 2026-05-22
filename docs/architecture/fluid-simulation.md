# Fluid Simulation Direction

This is the cross-project map for Cubey fluid work. Project-specific design and
checkpoint history live beside each project under `projects/`.

## Current Framing

Cubey should not chase one universal real-time fluid solver. The practical path
is a small set of focused projects, each with different scaling assumptions:

- `projects/fluid/smoke_2d`: incompressible grid-fluid lab for advection, pressure
  solves, obstacles, vorticity, and possible 2D free-surface experiments.
- `projects/fluid/water_2d`: 2D APIC free-surface liquid baseline with a
  PIC/FLIP fallback, particles for liquid motion, and a MAC grid for pressure
  projection.
- `projects/fluid_25d`: shallow-water terrain simulation for rivers, flooding,
  basins, sources, sinks, and heightfield-driven water.
- `projects/fluid/fire_3d` and `projects/fluid/explosion_3d`: dense 3D pyro
  baselines with shared 3D storage textures, compute
  advection/injection/combustion/projection, and fullscreen volume raymarching.
  These are the deliberate first step before sparse or tiled active regions.
- Future particle/hybrid liquid project: PBF, DFSPH, FLIP/APIC, or MLS-MPM for
  small interactive liquid volumes and multiphase material experiments.

The older GPU Gems style fluid demo was still a success for Cubey. It was a
cost-effective graphics project: limited implementation time produced a flashy,
impressive visual result, even though most of the work was platform/rendering
plumbing rather than deep fluid math. That remains a good Cubey goal.

The updated lesson is not "avoid fluid in a box." It is: keep visual leverage as
a first-class goal, but choose targets that can look better, teach more, and
scale farther than repeating the same dense grid demo unchanged.

Dense 3D grids still look impressive, but they scale poorly and are hard to
integrate into a broader scene. Modern work usually solves that by narrowing the
domain, adding synthetic detail, using sparse/local simulation, or baking
results when live simulation is not required.

## Real-Time Usage Reality Check

High-quality liquid simulation is still mostly an offline/VFX and DCC workflow:
Houdini FLIP, Blender/Mantaflow-style cached simulation, baked flipbooks, and
art-directed shots. Real-time applications do use fluid-like simulation, but the
surviving forms are usually constrained by gameplay, presentation, or authoring
needs.

Observed real-time patterns:

| Pattern | Examples | Cubey Implication |
| --- | --- | --- |
| Terrain/material gameplay | `From Dust`, `Timberborn` | Prioritize `fluid_25d`: heightfield water, sources, sinks, basins, floods. |
| Pixel/cellular material sim | `Noita` | Interesting for 2D material gameplay, but separate from Navier-Stokes-style grids. |
| 2D water-routing puzzle sim | `Where's My Water?` | Good reminder that constrained water behavior can be more valuable than general fluid accuracy. |
| Engine VFX fluids | Unreal Niagara Fluids | Keep sims local, debug-friendly, and bakeable when production needs it. |
| Sparse gas/fire middleware | NVIDIA Flow | Sparse/tiled 3D gas is the real successor to a dense GPU Gems smoke box. |
| Particle liquid middleware | NVIDIA FleX / PhysX particle fluids | Useful for local liquid/splash effects, not a general world-water answer. Treat FleX as historical context. |

Takeaways:

- Real-time games rarely ship a general dense 3D fluid volume as a global
  gameplay system.
- Water that matters to gameplay is usually terrain-bound, cellular/material,
  or local particle/hybrid simulation.
- Engine-level 3D fluids often skew toward VFX/cinematics or baked outputs
  rather than always-live gameplay state.
- Cubey should keep the GPU Gems fluid-in-a-box lineage as a high-leverage demo
  lesson, but revisit it by making the output cooler and the technique choices
  more deliberate.

Examples worth deeper later case-study work:

- `From Dust`: terrain, water, lava, vegetation, erosion-like nature simulation.
- `Timberborn`: city-building water management with terrain, droughts, dams,
  floodgates, and 3D water physics.
- `Noita`: pixel/material simulation as the core interaction model.
- `Where's My Water?`: mobile physics-puzzle water routing.
- `Vessel`, `PixelJunk Shooter`, and `Hydrophobia`: remembered liquid-centric
  games worth investigating with primary sources before using them as design
  anchors.

## Scaling Strategies

- Prefer 2D or 2.5D when it matches the effect. `fluid_25d` turns terrain water
  from a cubic volume problem into a heightfield problem.
- Use sparse/tiled allocation for 3D gas. Keep active bricks only where smoke,
  heat, or emitters exist.
- Keep simulations local to emitters, cameras, or gameplay regions when the
  effect does not need world-scale state.
- Add detail synthetically with vorticity confinement, turbulence, foam,
  particles, sprites, or shading instead of simulating every scale directly.
- Keep data GPU-resident. Treat CPU readback as an artifact/debug path, not the
  core gameplay or rendering loop.
- Bake when the effect does not need live interaction.

## Visual Leverage

Graphics projects and rendering demos still have an advantage: they can produce
compelling output quickly. Cubey should preserve that bias. "Looks cool" is not
a shallow goal here; it is part of why a graphics workbench is worth building.

For fluid work, visual leverage can come from:

- Better shading: lighting, depth cues, refraction/absorption, foam, color ramps,
  and velocity/debug overlays.
- Better motion: reduced numerical diffusion, vorticity confinement, richer
  injectors, moving obstacles, and stable feedback.
- Better presentation: orbiting cameras, cinematic presets, deterministic
  headless captures, and short demo modes.
- Better structure: local/sparse simulation, heightfield water, particles, or
  hybrid methods that produce more interesting output per unit of compute.

So the question is not whether a boxed fluid is allowed. It is what boxed or
constrained setup gives the best ratio of implementation effort to impressive
visual result now that Cubey has already done the basic GPU Gems version once.

## Technique Map

| Technique | Cubey Fit | Notes |
| --- | --- | --- |
| Dense Eulerian grid | Good for `smoke_2d`, baseline for `fire_3d` / `explosion_3d` | Simple, inspectable, still useful for learning; 3D scales poorly and should lead toward sparse/local work. |
| Better advection | High | MacCormack/BFECC directly reduce classic Stable Fluids diffusion. |
| Stronger pressure solve | High | CG or multigrid is a real scaling lever over fixed Jacobi iterations. |
| Obstacles/boundaries | High | Makes `smoke_2d` feel scene-relevant instead of purely decorative. |
| Level set liquid | Medium | Useful for signed-distance surfaces and collision work, but pure level sets lose mass and are no longer the `water_2d` baseline. |
| Particle level set | Medium | Corrects level-set mass loss with marker particles; more moving parts. |
| VOF | Medium | Better mass conservation, harder interface reconstruction. |
| Shallow water / virtual pipes | High | Best first path for scalable terrain water in `fluid_25d`. |
| Saint-Venant finite volume | Medium/high later | More rigorous terrain-water experiment after virtual pipes exists. |
| SPH / DFSPH | Medium | Good for small particle liquids and splashes; particle-count-bound. |
| PBF | Medium | Stable real-time interactive liquid toy; less physically rigorous. |
| FLIP/APIC | High | `water_2d` now uses APIC by default with a PIC/FLIP fallback. Meshing, richer collision, stronger pressure solves, and 3D coupling remain substantial later work. |
| MPM / MLS-MPM | Medium later | Interesting for mud, snow, sand, viscous fluids, and multiphase effects. |
| Lattice Boltzmann | Medium/low | Interesting flow-around-obstacle lab; not first choice for Cubey water. |
| Sparse 3D gas | High later | Best modern answer to the old dense 3D smoke/fire box. |
| Neural/cached fields | Low for now | Useful research/tooling direction, not a first implementation path. |

## Candidate Particle And Hybrid Projects

These are techniques Cubey should try, but not before the current terrain-water
and grid-fluid paths give us stronger runtime, debugging, and validation
pressure.

### FLIP / PIC / APIC

FLIP is a hybrid particle-grid liquid method. Particles carry liquid state and
free-surface detail; a temporary grid handles forces, collisions, and the
incompressible pressure projection.

Typical step:

```text
particle velocities -> grid
apply forces on grid
project grid velocity to be divergence-free
transfer grid velocity change back to particles
advect particles
reconstruct or render the particle surface
```

The interesting part is the transfer back to particles. PIC samples the new grid
velocity directly, which is stable but dissipative. FLIP samples the grid
velocity delta and adds it to each particle, which preserves more energy but can
be noisy. Many solvers blend PIC and FLIP. APIC carries local affine velocity
information per particle to reduce dissipation and noise; `water_2d` now uses
that path by default while retaining the PIC/FLIP path for comparison.

Why it is worth trying:

- It extends Cubey's current grid-pressure knowledge into free-surface liquid.
- It is a better target than pure level set if the goal is lively pouring,
  splashing, merging, and separating liquid.
- It gives useful infrastructure pressure: GPU particles, particle-grid
  transfer, spatial bins, surface reconstruction, and debug views.

Current Cubey target:

```text
projects/fluid/water_2d
```

Continue in 2D with particles plus a MAC-style grid, APIC transfer by default,
PIC/FLIP blend control for comparison, simple collision boundaries,
runtime-editable fill volume, reset presets, and particle-splat surface
rendering. The current `water_2d` path also includes a bounded hose-particle
ring over inactive particle slots, capped fixed-capacity particle bins, and a
bottom drain so continuous material flow can be tested without particle
compaction or readback. Defer 3D meshing, VDB-style surfacing, sparse/adaptive
particle storage, and complex collision coupling.

### SPH / PBF / DFSPH

SPH is a meshless particle-fluid method. Each particle estimates density,
pressure, viscosity, and forces from nearby particles through smoothing kernels.
The central data-structure problem is fast neighbor search.

Typical step:

```text
build particle bins or spatial hash
find neighbors
estimate density / constraints
solve pressure or position constraints
integrate particles
handle boundaries
render splats or reconstruct a surface
```

Why it is worth trying:

- It naturally represents droplets, splashes, merging, and separation.
- It is a direct way to pressure-test GPU particle data structures and neighbor
  search in Cubey.
- PBF is a stable, approachable real-time variant for an interactive liquid toy.
- DFSPH is a more serious incompressible SPH target once the particle path is
  working.

Tradeoffs:

- It is particle-count-bound and not the right answer for large rivers or
  flooding.
- Boundary handling and incompressibility are easy to get subtly wrong.
- Smooth rendering needs splats, screen-space surface reconstruction, metaballs,
  or meshing.

Suggested Cubey target:

```text
projects/liquid_particles_2d
```

Start with 2D PBF or simple SPH, GPU spatial bins, wall collisions, and splat
rendering. Use that to decide whether a later DFSPH or 3D particle-liquid
project is worth the complexity.

## Suggested Order

1. Continue `fluid_25d` with virtual-pipes shallow water over terrain.
2. Continue `smoke_2d` with pressure-solver experiments, moving obstacles,
   stronger diagnostics, and a clearer smoke/dye versus free-surface-liquid
   direction.
3. Continue `water_2d` from the current 2D APIC/PIC-FLIP baseline toward better
   surfacing, pressure solves, boundaries, validation/debug views, and richer
   water scenarios.
4. Try `liquid_particles_2d` with PBF/simple SPH to prove GPU neighbor search
   and particle-liquid rendering.
5. Consider a 3D liquid variant once the 2D particle-grid infrastructure is
   comfortable.
6. Continue `fire_3d` / `explosion_3d` from the dense boxed baseline toward
   stronger shading, detail synthesis, sparse/local simulation, and demo presentation than the
   original Cubey version.
7. Consider DFSPH, MLS-MPM, or additional 3D liquid variants only after the 2D
   particle and hybrid projects have paid for their infrastructure.

## References

- Mark J. Harris, "Fast Fluid Dynamics Simulation on the GPU", GPU Gems Chapter
  38.
  <https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-38-fast-fluid-dynamics-simulation-gpu>
- Keenan Crane, Ignacio Llamas, and Sarah Tariq, "Real-Time Simulation and
  Rendering of 3D Fluids", GPU Gems 3 Chapter 30.
  <https://developer.nvidia.com/gpugems/gpugems3/part-v-physics-simulation/chapter-30-real-time-simulation-and-rendering-3d-fluids>
- NVIDIA Flow documentation for sparse real-time voxel fluids.
  <https://nvidia-omniverse.github.io/PhysX/flow/index.html>
- Legacy NVIDIA Flow artist-tool docs for dynamic-grid, real-time fluid
  simulation in Unreal Engine.
  <https://docs.nvidia.com/gameworks/content/artisttools/Flow/FLOWUe4_Intro.html>
- NVIDIA FleX historical docs for unified particle physics and particle fluids.
  <https://developer.nvidia.com/flex>
- Unreal Niagara Fluids documentation for current engine-level 2D/3D fluid
  templates and baking guidance.
  <https://dev.epicgames.com/documentation/en-us/unreal-engine/niagara-fluids-in-unreal-engine>
- Ubisoft `From Dust` page for the game as an advanced nature-simulation case.
  <https://www.ubisoft.com/en-us/company/about-us/our-brands/from-dust>
- Official `Noita` page for pixel-level simulation as a shipped gameplay model.
  <https://noitagame.com/>
- `Timberborn` Steam page for terrain, drought, water-physics, and terraforming
  positioning.
  <https://store.steampowered.com/app/1062090/Timberborn/>
- Disney `Where's My Water?` Play Store page for physics-puzzle water routing.
  <https://play.google.com/store/apps/details?id=com.disney.WMW>
- SideFX Houdini particle fluid docs for the production FLIP model: particle
  velocity transfer to a grid, grid projection, and transfer back to particles.
  <https://www.sidefx.com/docs/houdini/fluid/liquids>
- SideFX Houdini FLIP Solver docs for the temporary velocity grid and projection
  model.
  <https://www.sidefx.com/docs/houdini/nodes/dop/flipsolver.html>
- Yongning Zhu and Robert Bridson, "Animating Sand as a Fluid", a classic
  particle/grid PIC/FLIP-style reference.
  <https://www.cs.ubc.ca/~rbridson/docs/zhu-siggraph05-sandfluid.pdf>
- Chenfanfu Jiang et al., "The Affine Particle-In-Cell Method".
  <https://www.math.ucla.edu/~cffjiang/research/apic/paper.pdf>
- Miles Macklin and Matthias Mueller, Position Based Fluids.
  <https://blog.mmacklin.com/project/pbf/>
- Jan Bender and Dan Koschier, Divergence-Free SPH for Incompressible and
  Viscous Fluids.
  <https://discovery.ucl.ac.uk/id/eprint/10056699/>
- SPlisHSPlasH, an actively maintained SPH library covering 2D/3D fluids and
  solvers such as WCSPH, PCISPH, PBF, IISPH, and DFSPH.
  <https://splishsplash.physics-simulation.org/>
- DualSPHysics, an SPH solver used for free-surface and multiphysics flows.
  <https://dual.sphysics.org/features/>
- Theodore Kim et al., Wavelet Turbulence for Fluid Simulation.
  <https://www.cs.cornell.edu/~tedkim/WTURB/>
- Ken Museth, NanoVDB: A GPU-Friendly and Portable VDB Data Structure For
  Real-Time Rendering And Simulation.
  <https://research.nvidia.com/labs/prl/publication/nanovdb/>
- Douglas Enright, Ronald Fedkiw, Joel Ferziger, and Ian Mitchell, "A Hybrid
  Particle Level Set Method for Improved Interface Capturing".
  <https://physbam.stanford.edu/papers/stanford2001-04.pdf>
