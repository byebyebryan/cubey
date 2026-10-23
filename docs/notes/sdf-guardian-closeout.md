# Guardian sculpture closeout

Date: 2026-10-22

Status: settled by user review. Retain the current organic, mirrored guardian,
limestone and subtle marble as the completed content experiment. Further
modeling and material realism are deferred; this closeout changes documentation
only. It does not refresh published captures or construction films.

## Accepted result

The guardian gives the SDF renderer an authored subject with a recognizable
silhouette, facial expression and a readable construction history. Swept horns,
pointed ears, a heavy brow, curved mandible and rounded skull carry the form
through front, profile and rear views. The plain surface remains useful for
shape inspection; limestone emphasizes the relief, while marble adds a more
decorative stone treatment.

| Variant | Retained static preset | Construction preset |
| --- | --- | --- |
| Plain mirrored guardian | [Static](../../projects/sdf/presets/sculpture-guardian-mirrored.json) | [Build](../../projects/sdf/presets/sculpture-guardian-mirrored-build.json) |
| Limestone | [Static](../../projects/sdf/presets/sculpture-guardian-limestone.json) | [Build](../../projects/sdf/presets/sculpture-guardian-limestone-build.json) |
| Stylized honed marble | [Static](../../projects/sdf/presets/sculpture-guardian-marble.json) | [Build](../../projects/sdf/presets/sculpture-guardian-marble-build.json) |

Marble retains Balanced detail: scale 6, vein frequency 7.25, width 0.07,
warp 5.5 and grain scale 280. The accepted palette is the original pale stone
plus restrained cream mineral regions. Saved color variation is 0.08; zero
retains the neutral version. The stronger 0.16 comparison was an inspection
aid, not the chosen default. Original, Denser and Fine detail choices remain
available without selecting a new appearance pass.

The target is limited realism: a pleasing, legible stone cue that supports the
sculpture. Matching individual reference slabs or building a geological material
simulation is outside this settled result.

## What the experiment demonstrates

The saved solid contains 1,011 explicit primitives and 2,021 instructions.
Its history has 65 whole-stroke operations: 27 additive and 38 subtractive,
including 48 mirrored pairs and 17 single actions. The primitive count includes
sampled stroke segments; it is not a count of independent artistic decisions.
Geometry identity is `d5fc89f0abe076f0` across the three material variants.

Construction communicates the technique better than a finished still alone:
large forms merge, sockets and mouth open, then shallow cuts establish the
expression and surface relief. Scripted reflection, atomic undo/seek and red
cut previews make those actions inspectable. Continuous model-space materials
remain coherent across fused forms and newly exposed surfaces, and appearance
edits retain the distance field. The native view uses hard self-shadows and
shared atmosphere/sky, with no ground plane.

The result demonstrates authored SDF rendering and replay. A finished statue
could also be made with meshes; this study does not establish an exclusive SDF
effect or a finished interactive sculpting product.

## Review and retained limits

Review covered the latest marble orbit, hero/face/rear views, plain/clay and
limestone inspection views, selected construction states and a captioned cut
preview. The silhouette and broad form hold together. Some fine forehead,
cheek and neck grooves remain repetitive, sampled stroke transitions can show
in close-up, and the shoulder/plinth transition is less resolved than the face.
These are accepted limits of the stylized scripted piece.

Marble still has smooth repeated bands and closed loops, with a limited vein
hierarchy. The subtle color regions improve variation without solving that
underlying structure. User review accepts the current material at restrained
strength and closes further realism work for now.

The latest recorded RTX 5070 Ti profile uses 1920×1080, supersample 1 and hard
shadows. Default marble measures 1.799 ms median / 1.852 ms p95 for SDF shading
plus post; its cold field build is 917.307 ms. The field allocation alone is
912,997,552 bytes, about 871 MiB. These retain their original receipt scope;
this documentation closeout adds no new benchmark. Geometry actions rebuild
the complete field synchronously, and live replay can skip actions while
catching up. Responsive continuous brush editing remains separate work.

## Evidence and presentation boundary

- [Organic guardian evidence](sdf-guardian-organic-evidence.md) records the
  thousand-leaf form, source evolution and complete prefix qualification.
- [Mirrored stroke evidence](sdf-mirror-evidence.md) records the 65-action
  history, reflection, paired previews and native undo/redo checks.
- [Procedural material evidence](sdf-procedural-materials-evidence.md) separates
  complete earlier material histories from later focused color qualification
  and profiles. The latest color implementation is `6d870222`; its native
  foundation, final captures and performance receipts agree on artifact hashes.
- [Settled marble views and turntables](http://starship:8787/reports/0a7461860a17aeccae6adc3f5e16cdafa358f9d28031cbd5b6fa3299ceb2a1ca/index.html)
  show the current neutral/default/stronger material, starting with native
  swatches. The report initially selects the stronger amount for comparison.
- [Earlier complete material presentation](http://starship:8787/reports/65b3ef76be120d14f259412977d3e50fa6e59e435298cd415ece6edb84d1ec87/)
  contains the 80-second construction/orbit film. Its exported geometry tree
  and all 65 history entries match the current scene exactly, but its marble
  uses the earlier Original detail settings without the new cream variation.

A future presentation pass could refresh that film with the accepted material,
emphasize decisive cuts, shorten repeated fine-detail steps and show rear work
more clearly. That is optional follow-up, not a prerequisite to settling this
content or an active implementation commitment. Published reports remain
immutable records. Browser interaction review of the latest report remains
unverified; HTTP delivery and native correctness are separate evidence.

Reopen guardian work for a concrete rendering/authoring failure, an explicitly
selected presentation pass, or a new subject that needs additional capability.
Further vein realism, extra sculpt detail, new lighting systems, terrain
backgrounds and AI-generated SDF content remain separately selected experiments.

## Closeout verification

The documentation review found no blocking closeout issue. The Release SDF
application and three C++ test targets are up to date. Five SDF CPU suites and
eleven rendering/layer/layout/documentation/build policy checks pass; all 18
scoped SDF CLI recipes pass with display and Vulkan drivers disabled. The
pending SDF commit range and staged closeout pass whitespace checks.

The existing final native binary and all 13 compiled shaders match the latest
40-run material foundation, selected swatch, guardian comparison and profile
receipts. Implementation snapshots match the current source. Those native
runs were not repeated for this documentation-only change. Repository-wide
runtime tests were not rerun; unrelated application executables are absent
from this SDF-focused build.
