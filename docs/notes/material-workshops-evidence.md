# PBR and SDF material workshop qualification

Date: 2026-10-23. Status: both authorized workshop stages implemented and locally
qualified; new presentation remains subject to user artistic review. No push.
Authored sculpting/showcase expansion remains deferred.

[Native review report](http://starship:8787/reports/2fcdfdaa2541d39e220a1cc6533bb5213a8d563c429e06bbe674b5f2bc2a7d0c/)
contains 98 native PNGs, five eight-second films, matched material/specimen
selectors, component/cut controls, pass timings, recipes and source receipts.
The 35.7 MiB bundle is separate from the repository's public showcase media.

## Implemented scope

- `pbr_materials`: ordinary mesh sphere gallery and a focused sphere/beveled-block
  workshop on the existing forward renderer. Recipe/specimen selection,
  orbit/reset, colors, roughness, metalness, rough diffuse, pattern/seed/bump,
  relevant optical controls, component views and recipe persistence are available.
- `sdf --workshop sphere|block|csg`: the existing renderer/field, shared recipes
  and a four-primitive CSG specimen with smooth fusion, toroidal groove and bore.
  Cut-material ownership and smooth material blending are independent controls.
- Seven recipes in `assets/materials`, strict v1 parsing/persistence,
  geometry-neutral CPU/GLSL evaluation, explicit affine domains and transformed
  bump gradients. Identity domains preserve legacy SDF fingerprints.
- Fixed studio, clear low sun and frozen clouds reuse existing environment
  services. Studio creates no atmosphere/cloud resources. No terrain/ground plane.
- Mesh live publication uses preallocated tables per retired frame slot. Default
  material tables remain immutable; routing-changing updates are rejected.
  SDF keeps its queue-idle publication boundary and field-cache reuse.

The [shared material contract](../architecture/material-recipes.md) owns current
semantics. [PBR](../../projects/pbr_materials/README.md) and
[SDF](../../projects/sdf/README.md#material-workshop) own the runnable front doors.

## Source and compatibility

Plans were committed first as `b35d1a89`. Implementation milestones were
`3ef4320b` (content/front door), `06cbc4c7` (mesh workshop), `cb64d919` (SDF workshop
and domains), `33eecfae` (controls/qualification) and `f1e70210` (front-door docs
and bounded workshop panel). Rendering qualification uses snapshot
`f1e70210205317b369dc065763e192e6049ba447`; subsequent report/docs work does not
change its renderer or recipe bytes.

The local `outputs/material-workshops/qualification/manifest.json` records all
116 native command receipts, per-run binary/shader identities, recipe hashes
and artifact hashes. The same manifest is included in the immutable report.

Preservation checks:

- All 28 accepted catalog scene/preset source hashes unchanged.
- Seven standalone sphere recipes have exactly the inline catalog's geometry
  and appearance fingerprints; deterministic recipe and authored-scene round trips.
- Initial mechanical extraction retained all 29 SDF SPV bytes. The later domain
  extension changes relevant shaders; the final compatibility gate is native
  image parity and CPU/GPU/legacy identity checks.
- Final sphere clay/glass and guardian clay/glass reference PNGs byte-identical
  to accepted cloud-handoff controls, with zero Vulkan validation errors.
- Final furnace, IOR and glTF control PNGs byte-identical to pre-change captures.
- Native material-cubes windowed smoke passes with the extended PBR uniform block.

CPU/GPU sampling passes for sphere, block, CSG and a nonuniform affine material
domain. The mapped CSG run has max distance error `5.96e-8`, material error
`1.01e-5`, gradient error `2.24e-8`, zero part mismatches and zero fast-path error.
Its field oracle has zero mismatches/exhaustions and one classified sampling
ambiguity; the largest hit-position difference (`0.156`) belongs to that
ambiguity. It is not presented as uniform ray agreement.

## Editing and optics

Both used frame slots pass 1,000 material updates each: seven descriptors and
7,168 uniform bytes per table throughout, no residency growth. A deliberately
routing-changing update is rejected while its old CPU definition remains intact.

Native X11/Xvfb interaction covers mesh studio/cloud/studio switching, recipe
save/reload and clean shutdown. That check exposed and fixed a renderer lifetime
error: the switch now retires resources on the retained renderer object rather
than clearing its owning renderer service. SDF cut ownership/blend changes and
save/reload log `field_rebuilt=0`. Files saved through either UI cross-load/export
unchanged in the other geometry adapter.

Shared V1 glass retains plain white, roughness 0.05 and editable IOR/attenuation.
Mesh thickness is an explicit 0.5-unit estimate; SDF measures interface segments
and defaults to eight crossings in workshop mode. This is shared optical intent,
not matched transport. In one oblique clouded CSG diagnostic, eight and sixteen
crossings both give zero capped/error pixels and the same TIR classification.
Dark regions retain the existing environment/TIR approximation and physical
lower-hemisphere reflection policy. No optical solver or realism retuning was added.

## Rendering cost

RTX 5070 Ti, 1920×1080, 1×, validation enabled, headless render-only with readback
and encoding disabled. Sixteen warmup frames and 94 retired GPU samples per
case. GPU jobs were sequential with no owned competing workload.

| Specimen | Studio median ms | Clear median ms | Clouds median ms |
| --- | ---: | ---: | ---: |
| Mesh seven-material gallery | 0.303 | 0.844 | 2.607 |
| Mesh glass sphere | 0.224 | 0.762 | 2.530 |
| SDF marble CSG | 2.837 | 3.094 | 4.205 |
| SDF glass CSG | 3.969 | 4.369 | 5.430 |

Every measured case is below 16.7 ms. Mesh and atmosphere SDF use whole-scene GPU
scopes; studio SDF sums each frame's trace/display scopes. Pass medians/P95 and
methods are in the report's `timings.json`. Setup, generated IBL, probe settling
and field construction are outside steady-frame timing.

The compact CSG field builds in roughly 6–7 ms, but the retained axis-64 field
still reserves 912,997,552 bytes (about 871 MiB). Storage redesign is deferred.

## Verification and delivery

Full core tests, five recipe/workshop/authored/material/config test executables,
all public header compile checks, foundation include/configured link boundaries,
project layout and documentation navigation pass. Seven furnace captures and
seven corresponding conformance validators pass. The scoped native documentation
CLI audit checks 27 recipes; the full repository CLI audit cannot qualify
unbuilt unrelated binaries in this release tree.

All 291 published files have matching HTTP SHA-256 bytes from Starship and Carbon.
The browser connector has no available browser in this session, so browser visual
and selector acceptance are not claimed. The static references and JavaScript
syntax were checked; native images were inspected. HTTP/native qualification
remains separate from user artistic acceptance.

Reproduce native output with
`python3 projects/pbr_materials/tools/qualify_workshops.py --stage all`.
Build the report with
`python3 projects/pbr_materials/tools/build_workshops_report.py` after the timing
summary and compatibility/diagnostic receipts exist under the recorded output
paths. Native capture receipts remain point-in-time evidence, not a renderer-wide
performance promise.
