# Reference-First Rendering Feature Workflow

Cubey's rendering demos are meant to produce credible visual results with
minimal wasted effort while stress-testing the renderer foundation. They are not
research projects for inventing new atmosphere, ocean, cloud, terrain, planet,
or material techniques from scratch.

Graphics work is sensitive to small implementation details. A poor image can
come from the wrong technique, a shader bug, a coordinate-space mismatch, a
resource-layout issue, or simply weak tuning. Starting from a known-good public
reference reduces that ambiguity and gives the project a concrete visual
baseline before Cubey-specific integration begins.

## Default Policy

For complex visual systems, start reference-first:

1. Select a good-looking, runnable, permissively licensed reference.
2. Port the core implementation as faithfully as practical into a standalone or
   tightly isolated Cubey project path.
3. Capture the visual result before major architecture cleanup or extension.
4. Integrate the working result into Cubey renderer, engine, config, UI, and
   project contracts.
5. Extend only for the intended Cubey use case, with feature isolation and
   diagnostics so additions can be judged against the reference-derived core.

The research phase should mostly choose and understand the reference: license,
visual target, data inputs, coordinate spaces, resource lifetimes, update
cadence, and integration constraints. It should not default to surveying the
most advanced technique and rebuilding it from papers unless there is no
reasonable reference to port.

## Reference Criteria

A useful reference should provide most of these:

- visible output that is already close to the desired look;
- source code that can be run, inspected, and captured locally;
- a license compatible with Cubey's use;
- enough comments, docs, or shader structure to identify the main data flow;
- clear assumptions about units, spaces, camera scale, texture formats, and
  temporal update;
- a scope small enough to port without importing an unrelated engine.

If a reference is visually strong but architecturally large, port the smallest
visual core first. Treat the rest as donor material, not as a mandate to clone
the whole engine.

## Porting Shape

Early ports should preserve behavior before they become idiomatic Cubey code.
Prefer explicit, local names and comments that map source concepts to Cubey
concepts over premature abstraction. A faithful but slightly awkward first port
is easier to debug than a clean rewrite with no visual baseline.

Once parity is credible, split the code by Cubey ownership:

- reusable render or engine contracts only when multiple projects need the same
  durable boundary;
- project-local recipes for art direction, domain constants, controls, and
  visual tuning;
- shared shader/procedural helpers only when formulas match or a golden-value
  parity pass exists;
- archived reference or legacy code while direct comparison remains useful.

Use headless captures, debug views, GUI inspection, and feature-isolation
toggles to keep the port testable. If a later extension regresses the image, it
should be possible to turn that extension off and compare against the
reference-derived path.

## What To Avoid

Avoid these defaults for visual feature work:

- starting with a broad from-scratch procedural design when a good reference
  exists;
- mixing the reference port, Cubey architecture cleanup, and art-direction
  changes in one pass;
- tuning a bad result without first proving whether the source port is faithful;
- extracting shared abstractions before at least one project has a working
  image;
- keeping multiple permanent comparison projects after their guardrail value is
  gone.

Scratch implementations are still useful for small foundation pieces, narrow
tests, or domains where no good reference exists. They should stay intentionally
small and have concrete capture or test criteria.

## Project Application

`projects/ocean` follows this policy by using the GodotOceanWaves-derived
spectrum/FFT/unpack path as its core, then adding Cubey atmosphere, terrain
fields, diagnostics, and feature-isolation controls around it.

The cloud direction also follows this policy: `cloud_ref` is the visual-shape
reference, `projects/cloud_ref_2` is an architecture reference, and
`projects/clouds_legacy` is integration evidence. Both projects are retired;
neither should be treated as the single final renderer. See the retirement
archive for the recovery anchor.

Future atmosphere, terrain, planet, ocean, cloud, material, and lighting work
should use the same pattern unless the task is explicitly a foundation spike:
find a credible reference, port the working visual core, capture it, then
integrate and extend it for Cubey's specific demo and renderer-stress needs.

For substantial rendering feature work, use a dedicated sibling git worktree
when another agent or branch is active. Keep early reference ports isolated from
mainline integration until visual parity, focused validation, and documentation
are ready.
