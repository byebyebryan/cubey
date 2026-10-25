# Native landscape and material showcase refinement

The square/4:3 Guardian and Robot sources previously left space inside the
common 16:9 site frames. New native 1920x1080 captures replace those sources:
marble and limestone Guardian stills, the Robot still and its construction film.
Their accepted geometry, recipes, poses and view presets remain unchanged.
The web derivatives scale the frame; they do not crop, stretch or add padding.

The material presentation now has four matched eight-second films: forward PBR
and SDF, each with dusk and dawn light. All twelve catalog materials occupy one
four by three sphere lineup. A separate saved cut sequence shows wood, marble,
rusted steel and clear glass together. Solid, tool preview and committed-cut
stills use the same camera; the film adds a final camera arc.

## Native identity and scope

The landscape sculpture/Robot captures use source `ec17b49f`. The accepted
material masters use `42b7538a`; a later native workshop UI fix at `31ac67c2`
produces a byte-identical lineup poster. All masters are 1920x1080. Material
films contain 240 frames at 30 fps, and the Robot film contains 480 frames at
30 fps. These are recorded playback specifications, not native performance
measurements. Captures ran on the NVIDIA GeForce RTX 5070 Ti with required
Vulkan validation.

Both lineups use radius 0.075, distance 0.855, a fixed 45-degree camera, identical
positions, sun azimuth -155 degrees and no specimen shadows. Dusk sweeps sun
elevation from 14 to -1.2 degrees; dawn reverses that range. The shared atmosphere
function supplies exposure. Cloud time advances from 300 to 540 seconds across
each film, an artistic acceleration rather than a full solar-day simulation.
The sun, cloud and exposure logs agree between renderers. Each film records 240
sky-probe generations and 31 cloud-probe generations, using 128-pixel probes.

The SDF field remains bounded. New V3 `environment_only_gallery` documents
explicitly admit separate homogeneous sphere/box/cylinder bodies and inherited
cuts. Conservative bounds must have a gap and lie inside the field. Touching,
overlapping, nested or smoothly blended bodies and cut-material overrides are
rejected. A glass path that exits toward another gallery material resolves to
the environment. This does not add inter-object refraction, nested optical
media, caustics, or a general rough BTDF. Existing single-material scenes keep
their previous path. The rust cut inherits the continuous oxide/steel field;
it is not the separate fresh-steel cut override.

The authoring generator, native presets, scene documents, domain translation
and reproduction notes live in
[the native presentation guide](../../projects/pbr_materials/presets/presentation/README.md).
The [capture receipt](../../site/media/receipts/native-showcases.json) records
commands, source revisions, binary/input/shader identities, master hashes,
web encoding commands, diagnostics and CPU/GPU oracle evidence. It identifies
the older sculpture capture source separately from the new material source.

## Acceptance evidence

- Six scoped native CPU tests pass, including clock agreement at every frame,
  twelve material recipes/local domains, cut inheritance, persistence and
  rejection of invalid gallery layouts.
- GPU field oracles report zero ray or part mismatches and zero exhausted rays
  for the lineup and cut board. The cut history passes all states, backward
  seek, reset and cache reuse.
- Thirteen glass diagnostic views cover dusk/dawn endpoints and midpoint, plus
  the cut sequence before, during and after its camera arc. They report zero
  trace errors and zero crossing-budget caps with the 16-interface budget.
  Total internal reflection remains an environment approximation.
- The old frozen four-specimen forward study and wood-cut study remain
  byte-identical to the captures made before the native changes.
- Eight scoped documentation/layout/media tests and the foundation include
  policy pass. The broader CLI recipe lane still depends on a complete
  `build/dev` installation; these new presets were exercised with the release
  binaries instead.
- Site verification passes: twenty pages, seven subjects, twenty-one demo
  entries, thirteen native owners, seventy-three media assets. The largest
  conservative page resource bound is 436,478 bytes; the material page is
  284,804 bytes, excluding films requested by the visitor.
- Browser checks cover native playback, renderer/phase selection, restored
  query state, the cut comparison and landscape Guardian/Robot frames. At
  320/390/768-pixel review widths, content has no horizontal overflow, the
  material film retains 16:9 and all four controls are 44 pixels high.

The site viewer keeps one film active. Switching renderer retains phase and
playhead and resumes an already playing film. Switching dusk/dawn starts the
chosen transition at its beginning. Other films pause when playback or a scene
comparison starts. All four films are accessible without JavaScript, and
the original individual material stills remain inspectable under frozen light.

## Delivery

Web films are silent 1280x720 H.264/yuv420p with fast-start metadata. The new
individual films range from 0.39 to 1.40 MB. The selected site media totals
27,611,630 bytes, including image thumbnails, and is served alongside the site
by GitHub Pages. No GitHub attachment upload or separate media host is needed.
Historical sealed media under `docs/media/` remains available independently.

The publication target remains [the public Cubey site](https://dev.byebyebryan.com/cubey/).
Deployment identity is independently visible at
[`metadata/deployment.json`](https://dev.byebyebryan.com/cubey/metadata/deployment.json).
