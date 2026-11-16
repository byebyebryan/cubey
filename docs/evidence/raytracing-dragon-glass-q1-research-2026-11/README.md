# Dragon glass Q1 research record

2026-11-14. Supports the [proposed optical-normal experiment](../../notes/raytracing-dragon-glass-q1-plan.md).
The baseline is the qualified Q0.1 working tree, not HEAD `10328ef2` alone.
No new shader/runtime implementation or GPU capture is part of this planning
pass. Existing Q0.1 qualification remains dated to its own captured identity.

- `inputs.json` pins the inspected runtime/tool, asset, recipe, preset and
  ordinary binary/shader identities. It distinguishes working-tree inputs from
  a committed revision. Recheck them before implementation.
- `sources.json` records primary-source URLs, reviewed scope and what each
  source does not establish. No external implementation is imported.
- `validation.json` records documentation checks and preservation of the
  existing runtime, model/materials and other non-documentation files.

Source inspection established that dielectric radiance uses the geometric
normal in `material_workshop.glsl`; smooth `Surface.n` is already reoriented
for shading in `trace.rgen`. The optical path's cone assumes zero curvature,
and its straight shadow path evaluates Fresnel about the geometric normal.
History identity contains camera/environment and selected workshop fields;
an additional policy needs explicit identity coverage. `environment.glass.w`
already has geometry/spawning semantics and cannot safely become a new policy
ordinal. These are implementation constraints, not results for a smooth policy.

The first candidate is a Cubey whole-interface fallback experiment. The cited
BSDF treatments motivate geometric classification and consistent transport
weights; none supplies a complete guard, guarantees reciprocity for a
view-dependent fallback, or qualifies the dragon. The filtering references
explain the next dependency without selecting a cone or differential port.
