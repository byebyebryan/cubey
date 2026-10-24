# Procedural material library expansion plan

Date: 2026-10-23

Status: implementation started; [checkpoint evidence](material-library-evidence.md)
records completed work and open qualification. The plan follows the
[surface research](material-library-surface-research.md). New appearances remain
pending human review.
Baseline: `7b7bfd07`, the [qualified mesh/SDF workshops](material-workshops-evidence.md).

## Outcome and scope

Turn the shared recipe set into a small useful library: retain the seven
accepted looks and add wood, rusted steel, woven cloth, kraft cardboard and
frosted glass. Both workshops consume the same content, controls and parameter
meanings. Each family gets one strong default, a bounded range of meaningful
controls and a specimen that reveals its defining property.

This is the next shared-material experiment in the
[three-demo roadmap](material-demo-roadmap.md). It does not reopen guardian
modeling, marble realism, cloud design or the desktop sculpting-editor problem.

The deliverable is twelve catalog entries, not twelve new shading models.
Existing GGX, rough diffuse, sheen helpers, IBL products and glass filtering
remain the starting point. Do not add a material node graph, image-texture
authoring route, external asset dependency or another environment renderer.

## Content and presentation contract

The names below are proposed stable IDs. Existing IDs and defaults stay intact.
Shared seed, affine domain and color controls remain available; the table lists
the additional controls worth exposing, not a requirement for every internal
coefficient to become a slider.

| New ID | Bounded target and controls | Main specimen and acceptance cue |
| --- | --- | --- |
| `wood` | Unvarnished hardwood; ring spacing, ring distortion, grain/pore strength, roughness | Cut block/bore with related long grain and end grain; no unrelated restart at a cut |
| `rusted-steel` | Dry oxide over steel; coverage, patch scale, edge breakup, pit strength, steel/oxide roughness | Plate and CSG cut: remaining steel reflects differently from oxide; deliberate fresh-cut companion material |
| `kraft-cardboard` | Uncoated fibrous paperboard; fiber scale, directionality, fleck strength, relief, diffuse roughness | Thin board with a face close-up and visible edge; dry fibers distinct from terracotta granules |
| `woven-cloth` | Coarse plain weave; yarn spacing, yarn width, weave relief, irregularity, sheen strength/roughness | Oriented swatch; alternating over-under structure close up, soft restrained grazing response farther away |
| `frosted-glass` | Uniform etched surface; roughness plus existing IOR and absorption controls | Clear/frosted pair and roughness strip; background detail and reflections soften while silhouette stays opaque in coverage |

Keep the existing sphere/block controls as common diagnostics. Add only the
small fixtures needed above; a simple swatch is enough for cloth, with no cloth
simulation. Corrugated flutes are optional companion geometry for the paper
study, not a shader feature or a completion prerequisite. New primitives and
another authored sculpture are unnecessary.

Use the fixed studio as the main comparison. Reuse current clear low sun and
frozen clouds for a few presentation shots. One generated diagnostic environment
with high-contrast bars supplies an optical test shared by mesh and SDF. It is
demo content using existing environment products, not a new sky mode for every
project. Keep its probe/background identity coherent.

The guardian remains an optional final application check for a suitable material
such as wood or rust; mapping cloth over a statue is not an acceptance target.

## Shared data and compatibility

### Catalog

Add one small manifest under `assets/materials`, consumed by both workshops and
their qualification tools. Each entry has a stable ID, label, family/tags,
relative recipe path and a short explanation. Derive supported rendering
requirements from the validated recipe; do not maintain a conflicting second
feature declaration. Specimen/camera presets belong to demos, not recipes.

Remove hardcoded seven-entry selection/capture lists. The mesh gallery uses a
bounded grid or pages; the SDF workshop uses the same catalog selection with one
active specimen. Do not require an SDF twelve-object gallery. Provide family
filtering and focused inspection, with a clear reset to the catalog default.

Size resident mesh tables from the validated catalog snapshot, with a proposed
32-entry cap and twelve entries in this milestone. Keep material handles stable
within that snapshot. Reject duplicate IDs, ID/path mismatches, missing recipes
and unsupported requirements with a useful error; retain the last good snapshot
on reload failure. The bounded implementation permits live recipe/metadata
reload with an unchanged mesh roster and optical routes; changing IDs, order,
entry count or routing requires restart to preserve permanent handles. A catalog
is content metadata, not a new engine asset database.

### Versioned recipes and resolved samples

Preserve v1 parsing, serialization and appearance fingerprints for the original
seven recipes. Introduce explicit recipe v2 for the new pattern parameters,
sheen and rough-glass mode. Do not silently reinterpret v1 glass or repurpose
unused components of the legacy packed pattern structure.

Use named, typed parameter groups for wood, rust, weave and paper fibers, with
validated finite ranges and documented material-space units. CPU reference and
GLSL evaluation must resolve the same semantics. Compile these groups into an
explicit GPU representation with checked offsets; exact packing is an
implementation detail to settle at checkpoint 1.

Extend resolved surface sampling to carry spatial metalness in addition to
color, roughness, emission and height gradient. Rust's single coverage field
interpolates steel/oxide response consistently. Sheen color/roughness and rough
diffuse can remain recipe-level parameters for this milestone; do not add
spatial channels without a consumer. Neutral extension defaults preserve the
legacy evaluator and renderer paths.

Transmission class, alpha mode, culling and pipeline routing stay fixed for a
recipe handle. A rust-coverage slider changes opaque surface response, not
render routing. Switching an opaque recipe to glass selects a separate
prevalidated handle. Keep default material tables immutable; editable tables
still publish only after their owning frame slot has retired.

### Coordinates and filtering

Wood and rust use explicit object-anchored solid coordinates. Wood's axis comes
from its material domain, not camera direction or object bounds. A cut evaluates
the same solid field at the exposed point unless CSG explicitly chooses a
different material/domain.

Cloth and paper use the two axes of an explicit planar material domain on their
swatches. This can use the current point/footprint sampler without requiring a
general surface-chart API. Preserve that practical boundary: arbitrary seamless
cloth draping, triplanar weave and a global tangent atlas are deferred. Document
edge stretching on a block or sphere rather than hiding it with noise.

Filter periodic rings/weave and fine fibers from the first implementation.
Under minification, preserve average coverage/color and a stable rough response
while resolved relief fades. A vanished bump must not turn a coarse cloth into
glossy plastic. Use transformed mesh derivatives and the existing conservative
SDF footprint policy first. Add anisotropic footprint machinery only if the
oriented swatch test demonstrates a failure the current method cannot bound.

### Persistence and SDF ownership

Saving and reloading every exposed control is part of each content slice.
Catalog metadata is excluded from the appearance fingerprint; resolved recipe
values, seed, domain and active response fields are included. Appearance changes
must preserve geometry fingerprints and field-cache hits.

The current shared-recipe-to-inline-v2 SDF adapter cannot silently discard new
parameters. Recommended route: add an explicit authored-scene v3 material form
containing self-contained resolved shared recipes. Retain v2 loading, writing
and legacy hashes unchanged for old scenes; geometry/history semantics remain
the same. New scenes must not depend on an external recipe file retaining its
future contents. Do this before claiming scene-export support for new materials.

Keep CSG material selection and smooth blending explicit. Blend resolved opaque
response channels consistently; retain current glass restrictions rather than
inventing mixed optical media. A rusted outer surface and steel cut use separate
deliberate material assignments. The library's entry count does not raise the
current per-scene sixteen-material limit or the SDF field allocation.

## Renderer changes

Opaque studies share evaluators between mesh and SDF. Feed sampled metalness to
each renderer and connect recipe sheen to the mesh factors already implemented.
For SDF, reuse the shared Charlie helpers and generated LUT alpha, including
base-layer energy attenuation for direct and environment response. Verify zero
sheen preserves the existing path. Do not build another sheen LUT or a full
woven-yarn scattering model.

Frosted glass gets its own opt-in transport path. Mesh uses the existing
rough-transmission pyramid/environment LOD machinery. SDF retains its bounded
interface path and measured in-medium segments, then approximates rough angular
spread with filtered environment samples. Account for roughness and interface
propagation, clamp to actual mip availability and keep the legacy sharp path
unchanged. Use eight crossings as the baseline and sixteen as a diagnostic;
increasing crossings is not the roughness implementation.

This SDF approximation will not match a full microfacet BTDF or raster scene
refraction. Show that boundary in the workshop/report. Reflection and transmission
must both respond to roughness; an opacity trick or a single blurred reflection
does not pass. No arbitrary opaque-geometry transmission, nested media,
multiple-ray integration, caustics or new scattering volume is included.

## Implementation checkpoints

Commit reviewable checkpoints during the future implementation loop. Do not
start by making every material at once or by redesigning shared rendering.

| Checkpoint | Work | Exit evidence |
| --- | --- | --- |
| 0. Pin visual targets and feasibility | Retain the reference links and one cue sheet per family. Make a small native rough-glass test using existing mesh filtering and a bounded SDF environment path; confirm the sheen LUT can be consumed by SDF. | Clear/frosted bar test establishes whether the bounded approach is viable; proposed approximation and cost recorded before polishing content. A failed spike is reported explicitly. |
| 1. Shared content contract | Catalog, recipe v2, neutral sample extensions, bounded resource sizing, SDF resolved-recipe persistence and legacy guards. Add only the parameter groups needed by the following slices. | Original seven remain unchanged; invalid reload retains last good state; new control values and domain survive recipe/scene round trips. |
| 2. Wood vertical slice | Solid growth field, filtered rings/grain/pores, controls and cut-block fixture in both consumers. | Native face/end-grain/cut views, transformed-domain continuity, close-to-far motion and one bounded appearance correction. |
| 3. Rust vertical slice | Multiscale oxide coverage, sampled metalness, roughness/relief correlation and explicit steel cut assignment. | Coverage/metalness/roughness component plate; endpoints and cut ownership demonstrated under matched lighting. |
| 4. Fibrous surfaces | Kraft fibers and plain weave, swatch orientation, filtering and shared restrained sheen. Reuse only helpers with actual common semantics. | Cardboard differs from clay; cloth differs from noise and cardboard; grazing response and a slow pullback remain stable. |
| 5. Frosted-glass content | Complete the already tested opt-in path, recipe controls, persistence and matched clear/frosted specimen. | Fixed IOR/absorption roughness strip softens reflection and transmission; legacy glass remains unchanged; interface limits visible. |
| 6. Curate and qualify | Twelve-entry library, scaled gallery/selection, selected environment presentation, documentation and bounded native report. | Compatibility, resource/performance and publication checks complete; new appearances presented for human review. |

Checkpoint 0 is a narrow future prototype, not a new research program. If the
SDF frosting approximation cannot show convincing blur after one correction,
finish the opaque library and record frosted glass as experimental or partial.
Do not label all five complete, remove the limitation from the report, or expand
the goal into a transport rewrite to satisfy the entry count.

## Validation and effort boundary

Tests should cover independent semantics rather than merely mirror helper code:

- Preserve existing recipe/scene golden fingerprints, old material captures,
  geometry fingerprints and cache-hit behavior. Include the existing glTF/forward
  consumers so zero procedural/sheens extensions retain their defaults.
- Check CPU/GLSL samples for deterministic channels, rust endpoints and filtered
  transitions, affine gradient behavior and finite results across exposed bounds.
- Round-trip all new parameters through recipes and resolved SDF scene snapshots;
  reject unsupported versions, malformed groups and routing-changing edits.
- Demonstrate wood cut continuity, explicit CSG cut ownership, oriented weave,
  minification stability and rough-glass reflection/transmission separately.
- Exercise repeated edits across in-flight slots and failed reloads. Require
  bounded handles, buffers and descriptors; pattern controls must not allocate a
  new GPU resource per edit. Keep SDF's existing queue-idle publication boundary.
- Run focused core/render tests and the required header, foundation, recipe and
  documentation checks after the relevant changes. Do not repeatedly rerun GPU
  qualification without a changed renderer or unresolved visual concern.

Profile representative opaque, sheen and glass paths at the prior 1080p 1x
qualification settings, with warmup and validation status recorded. Report
steady-state pass costs separately from field construction, probe generation
and cold-start costs. Aim to retain a full-frame 16.7 ms presentation budget on
the qualified machine; record deltas from the prior workshop evidence. This is
an acceptance target, not a current measurement or a promise for other hardware.

Keep evidence selective: a studio sphere for each of twelve materials in both
consumers (24 stills), one defining fixture per new material in both consumers
(10 stills), selected component/control plates and a few environment heroes.
Target at most 64 stills and four short films in the new report. A small matrix
that answers the material questions is enough; avoid every material/specimen/
environment permutation. Keep old reports immutable and record source, recipe,
environment and capture identities for new evidence.

Each family gets its initial audition and one focused visual correction. The
remaining test is whether its defining cue reads clearly at useful distances,
not whether it has become a physically complete material simulation. If a cue
does not read, retain the evidence and state the unresolved issue. Human visual
acceptance follows native technical qualification; neither source tests nor
served-byte parity establish that the material looks good.

## Completion and deferred work

Complete means the accepted seven are preserved, the five new families have
usable controls and honest support labels, both consumers use the shared
catalog, persistence retains the new data, and a bounded native report explains
the results and limitations. A partial frosted-glass result is a recorded scope
change, not completion of the full twelve-material target.

Defer species collections, knots/figured-wood BRDFs, painted/peeling metal,
curvature-driven weathering, arbitrary cloth charts, yarn/fiber geometry,
fuzzy edges, image UVs, procedural node graphs, measured-material import,
full OpenPBR adoption and physically complete rough refraction. Reopen those
only for a selected visual failure or a new content goal. No push or publication
of a new public showcase is implicit in this plan.
