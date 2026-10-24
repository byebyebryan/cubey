# Procedural material library: surface research

Date: 2026-10-23

Status: research and recommendations, checked against source at `7b7bfd07`.
No new material or rendering capability is implemented by this document. The
[implementation plan](material-library-expansion-plan.md) turns these findings
into a bounded follow-up to the [qualified workshops](material-workshops-evidence.md).

## Direction

Expand the library by surface structure and light response. Keep the existing
seven accepted looks, then aim for one convincing example each of wood, rusted
steel, woven cloth, kraft cardboard and frosted glass. A collection of twelve
distinct studies is more useful here than many color variants.

The target is recognizable, controllable procedural materials with limited
realism. The research below informs our approximations; it does not commit us
to reproducing a paper's full model or matching a measured specimen exactly.
UV-mapped images, material graphs and learned generators remain outside this pass.

| Study | Defining cue | Main experiment | Useful presentation |
| --- | --- | --- | --- |
| Wood | Related rings, lengthwise grain and end grain | A coherent solid pattern survives cuts | Block showing long grain, end grain and a bore |
| Rusted steel | Rough oxide interrupts reflective metal | One coverage field changes several response channels | Plate under a moving broad highlight; cut exposes a deliberate steel companion material |
| Woven cloth | Alternating yarns, irregular fibers and soft grazing response | Filtered weave plus a restrained sheen layer | Oriented swatch in frontal and grazing light |
| Kraft cardboard | Fine pulp fibers, flecks and matte relief | A dry fibrous surface distinct from stone and clay | Thin board with a visible edge and close-up face |
| Frosted glass | Soft reflections and blurred transmission | Rough optical transport rather than opacity | Clear/frosted pair against high-contrast environment bars |

## References and what they establish

References are primary research, official renderer/specification documentation,
manufacturer material descriptions or the authors of the photographed assets.
External photographs and renders are reference evidence, not Cubey output.
Link to them rather than copying assets into the runtime or the repository.

### Wood: structure through the volume

[Liu, Dong, Hašan and Marschner, 2016](https://www.cs.cornell.edu/projects/wood/)
build wood from volumetric growth rings, pores, rays and growth distortions;
fiber directions also support anisotropic figure. Their
[supplement](https://www.cs.cornell.edu/projects/wood/supplemental.html) provides
component comparisons and cut-object demonstrations. The relevant lesson is
the relationship between features throughout a solid, not simply brown stripes.

For a physical appearance reference, the author's
[white-oak photographs](https://www.wood-database.com/white-oak/) show different
face orientations and end grain. These are useful for comparing directional
structure; this pass need not claim white-oak species accuracy.

Our proposed approximation uses a distorted cylindrical growth field around an
explicit material axis, asymmetric ring bands and elongated pore/fiber detail.
The same field supplies face and end-grain appearances. Color, roughness and
relief should share structure without being identical copies of one signal.
Start with an unvarnished hardwood look. Knots, sapwood, species collections,
curly figure and a dedicated anisotropic wood BRDF are later experiments.

The strongest test is a cut block whose newly exposed surface agrees with the
outer grain. A sphere alone can conceal the distinction between a solid field
and a projected stripe pattern. Controls should cover ring spacing, distortion,
grain/pore strength and roughness, with the existing seed and affine domain
controlling repeatability, scale, axis and orientation.

### Rust: spatial response, not only color

[Adobe's OpenPBR guide](https://experienceleague.adobe.com/en/docs/substance-3d/general-knowledge/openpbr/openpbr-overview)
distinguishes conductive metal from nonmetallic rust. That is the important
principle to borrow; adopting the entire OpenPBR model is unnecessary.

The photographed reference on
[Poly Haven's Rusty Metal 05](https://polyhaven.com/a/rusty_metal_05) shows broad
uneven corrosion regions and much smaller surface detail. Its context photograph
also contains surviving paint: the blue areas must not be read as bare-steel
color. Use it for patch scale, edges and pitting, not as a two-material ground
truth. Its material preview is a rendered reconstruction, separate from that photo.

Our proposed look is exposed steel with dry oxide patches. A deterministic
multiscale coverage field drives correlated base color, metalness, roughness and
shallow relief. Coverage endpoints represent steel and oxide; intermediate
values represent filtered coverage/transition, not a new partly conductive
substance. Add restrained fine pits after the larger patches read clearly.

This needs a spatial metalness output from the shared sampler. Brown noise over
uniform metal would keep rust reflecting like metal; brown noise over a uniform
dielectric would lose the remaining steel. A component view should make this
difference visible. Weathering simulation, curvature-driven masks, peeling paint
and displaced corrosion silhouettes are unnecessary for the first example.

An explicit steel cut material is a useful CSG demonstration. Assigning it is
authoring intent, not a claim that the procedural shader knows a cut is fresh.

### Cloth: weave and sheen are separate cues

[Irawan and Marschner's woven-cotton research](https://www.cs.cornell.edu/~srm/publications.html)
addresses both resolved yarn texture and aggregate response when that texture
becomes too small to resolve. The older direct technical-report URL currently
redirects to Cornell's general graphics page; the author's publication index
retains its title and attribution. More extensive predictive fiber models are
documented by the authors' [CT cloth project](https://research.cs.cornell.edu/ctcloth/).
Those volumetric models exceed this experiment's scope.

The photographer's reference on
[Poly Haven's Rough Linen](https://polyhaven.com/a/rough_linen) makes two scales
easy to distinguish: the repeated woven yarn structure and finer irregular
fibers. Weave remains directional around the folds; it is not generic granular
noise. The page separates photography, material renders and channel maps.

The official
[Khronos sheen model](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_sheen)
provides a Charlie lobe with base-layer energy attenuation. It describes
velvet-like microfibers, so adding it alone does not reproduce woven cotton.
For our limited target, a coarse plain-weave pattern plus subtle sheen is a
reasonable approximation to test, rather than a measured woven-cloth BSDF.

Build alternating warp/weft over-under relief, narrow yarn gaps, slight yarn
irregularity and subordinate fiber variation. Use a dry dielectric base and
restrained grazing sheen. Filter the periodic pattern from the first version;
an attractive still that shimmers during a slow camera move is insufficient.

Use an explicitly oriented planar swatch first. Procedural without image UVs
still needs coordinates. The existing affine material domain can define the
swatch's two axes. It cannot invent a globally continuous cloth chart or drape
direction over arbitrary SDF geometry. A sphere may expose that limitation as a
diagnostic; seamless wrapping, cloth simulation, actual thread geometry and
transparent weave holes are separate work.

### Cardboard: a fibrous surface and a geometric construction

[Smurfit Kappa's corrugated-sheet reference](https://www.smurfitkappa.com/uk/products-and-services/paper-and-board/conventional-corrugated-sheets)
describes liner and fluting papers and the resulting board constructions.
It is useful for separating paper surface appearance from the board's actual
layered shape. Corrugation at an exposed edge is geometry, not fine bump.

Start with uncoated kraft paperboard: short irregular fibers, subtle preferred
direction, sparse pulp flecks, weak color variation and shallow multiscale
relief. Use the existing rough-diffuse response to retain a dry, matte appearance.
Its identification must come from fiber structure and response, not just a
brown base color or the existing terracotta grain pattern at another scale.

Use a thin board specimen with face and edge visible. A small companion
corrugated construction can illustrate the distinction if it is inexpensive,
but it is not required for the paper material to pass. Printing, torn/fuzzy
silhouettes, wet pulp and subsurface fiber transport remain deferred. The same
explicit swatch-domain policy as cloth avoids pretending that all surface
patterns are naturally volumetric.

### Frosted glass: the main transport gap

[Walter et al., 2007](https://www.cs.cornell.edu/~srm/publications/EGSR07-btdf.pdf)
model and measure rough-surface refraction. Their first-page globe render uses
sharp background bars to make the difference between smooth and etched regions
visible. This is a particularly useful diagnostic composition, not a proposed
performance target for our renderer.

The official
[Khronos transmission extension](https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_materials_transmission)
separates optical transmission from alpha coverage and accounts for roughness
in transmission. [AGC's Matelux product](https://www.agc-yourglass.com/en-UK/brands/matelux)
is a physical reference for acid-etched satin glass. Choose that surface-roughened
look rather than opaque white glass or a cloudy scattering volume.

Our first specimen should keep a neutral base, fixed IOR and controlled
attenuation while roughness softens both reflection and transmitted environment
detail. A lower opacity or noisy normal does not establish this behavior.
Renderer parameters must retain Cubey's existing perceptual-roughness meaning;
microfacet slope parameters in papers or other renderers are not interchangeable.

For mesh glass, build on the existing rough-transmission filtering. For SDF,
test a bounded environment-prefilter/cone approximation around the existing
interface path. This would approximate angular spread, not solve the full rough
BTDF over multiple rays. Retain measured in-medium distance for absorption and
the current crossing budget. Raising 8 crossings to 16 does not create frosting.

Use a common high-contrast environment for the paired test. SDF glass currently
transmits environment radiance rather than arbitrary opaque scene geometry, so
a physical checkerboard behind the object would test a different missing feature.
Full scene refraction, caustics, nested media and a Monte Carlo transport path
remain outside the proposed milestone.

## Current capability audit

These are current source findings, separate from the proposed material models.

| Area | Available now | Required follow-up |
| --- | --- | --- |
| Portable content | Validated v1 recipe files, seed, affine material coordinates, save/reload and two consumers | Versioned named parameters for new patterns; shared catalog metadata |
| Surface sampling | Color, roughness, emission and height gradient; filtered legacy patterns | Spatial metalness for rust; explicit neutral extension defaults |
| Coordinates | Object-anchored affine domain and gradient chain rule; mesh derivatives and conservative SDF scalar footprint | Oriented swatch policy and filtering tests; no global surface-atlas promise |
| Opaque shading | Shared GGX and rough diffuse; mesh factors already expose sheen/anisotropy | Connect recipe sheen to both consumers; SDF needs the existing shared sheen response and energy handling |
| Sheen environment support | Generated IBL BRDF LUT already stores Charlie directional albedo in alpha; mesh uses it | Reuse this data rather than generating another LUT or IBL system |
| Glass | Mesh rough-transmission LOD helpers; SDF bounded sharp interface tracing and environment reflection/transmission | Explicit rough-glass recipe mode and bounded SDF filtering; v1 glass currently requires roughness 0.05 and no pattern |
| Library UI/resources | Seven hardcoded entries; mesh tables have capacity eight | Shared manifest, bounded table sizing, scalable gallery/selection |
| SDF persistence | Inline authored v2 materials, material ownership, stable geometry cache and legacy appearance hashes | New shared fields must survive scene export; no silent downgrade through the old inline adapter |

Source anchors: [recipe contract](../architecture/material-recipes.md),
[sample types](../../include/cubey/procedural/surface_material.h),
[recipe parser](../../src/cubey/asset/material_recipe.cpp),
[PBR shader](../../shaders/cubey/forward_pbr/forward_pbr.frag),
[shared BRDFs](../../shaders/cubey/pbr.glsl),
[generated IBL](../../src/cubey/render/generated_ibl.cpp),
[SDF lighting](../../projects/sdf/shaders/lighting.glsl),
[SDF workshop adapter](../../projects/sdf/sdf_workshop.cpp), and
[SDF authored format](../../projects/sdf/sdf_authored.cpp).

## Recommendation

Proceed with five focused studies, with wood as the first complete content
slice. Rust then establishes spatial response channels; cardboard and cloth
cover fibrous surfaces and sheen. Give frosted glass an early feasibility check
and a separate final implementation checkpoint because its missing capability
is transport, not another noise function.

Keep the sphere comparison, but give each study one specimen that explains its
structure. Preserve the accepted seven materials and existing environments.
Finish each new look after an initial audition and one focused correction; if
it still misses its target, record the limitation rather than extending the
goal into a realism project. Native correctness and human appearance review
remain separate gates.
