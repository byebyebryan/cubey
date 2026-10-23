# SDF sphere and guardian material study

Status: implementation and qualification in progress, 2026-10-23. The
[execution plan](sdf-material-study-plan.md) fixes the material/subject roster
and experiment limits. The settled guardian and stone presets remain separate.

## Opaque checkpoint

`878a4842` records the plan and `1e1e0a05` adds the shared material catalog,
additive optics and opt-in generated IBL route. The sphere and guardian receive
identical material records. Plain, limestone and subtle marble copy the accepted
values; new looks are dry terracotta clay (roughness 0.90), teal ceramic (0.22)
and warm satin bronze (0.32, metallic 1). Native first-pass inspection found
the three responses distinct, without requiring a corrective look pass.

`outputs/sdf/material-study-baseline/` retains the pre-edit binary, shaders,
four native PNGs and complete commands. Plain, limestone, marble and neutral
inspection PNGs match exactly after the opaque extension in
`material-study-legacy-regression/`. Existing 32-byte material, 80-byte pattern
and 64-byte geometry records are retained; a parallel 32-byte optics record
adds metallic, transmission, IOR and attenuation. Default optics retain prior
appearance hashes/exports. Optical properties do not enter geometry identity.

The initial 20-run opaque foundation checks all six opaque sphere looks,
shortcut/full equality, mixed optical ownership, an explicit metal cut material
and six white-furnace cases. The furnace flags radiance above 1.02 before HDR
output can clip it. All pass. The final expanded foundation adds glass native
checks, GPU interface/absorption parity and exact glass export/reload PNGs.

## Bounded glass checkpoint

The homogeneous path is restricted to one neutral, plain material with fixed
roughness 0.05. Refraction uses oriented geometric normals and measured
in-medium distance; absorption uses the common material's linear attenuation
color/distance. The shader processes at most four interfaces. Reflections and
final transmitted radiance sample the same generated environment. Total
internal reflection terminates with an environment approximation; there is no
recursive internal reflection, caustic or opaque-scene transmission support.

Independent CPU checks cover Snell refraction, normal-incidence Fresnel,
total internal reflection and Beer-Lambert path-length composition. Native
authored validation adds interface direction/Fresnel/TIR and absorption
CPU/GPU comparisons at the unchanged 0.00002 bound. Refracted inside-to-exit
rays also enter the existing independent field oracle without relaxing its
sampling bounds.

The first glass trial passed on the sphere but flagged small trace parameters
as failures on the guardian. The trace starts at an offset origin, so that
parameter alone does not establish a repeated self-hit. The one allowed
correction tests world-space progress instead. The corrected sphere, separated
solids and hollow-shell fixtures pass; hero/detail and eight guardian orbit
samples contain no exhaustion/error pixels.

The corrected guardian still fails the predeclared admission gate: the worst
rear orbit sample reaches the crossing cap on 1.1068% of visible glass pixels,
above the 1% limit. Keep sphere glass as the qualified material example and
guardian glass as an explicitly experimental illustration. Do not raise the
crossing budget or threshold. TIR environment fallback affects roughly 39–60%
of the sampled guardian pixels, a significant documented approximation.

Trials and diagnostics are retained in `material-study-glass-{a,b}/`. Debug 9
encodes crossing-cap fallback in red, TIR fallback in green and glass coverage
in blue 0.25; traversal/progress errors retain the separate magenta failure
signal. The native diagnostic classifier uses coverage 45 < blue < 180,
red/green > 180 for fallback flags and the existing magenta error criterion.

## Remaining delivery

Final matched captures, controlled per-material profiles, final legacy-image
regressions, native controls and the published sphere/guardian report are still
pending. Earlier qualification receipts keep their original binary/shader
identities; they are not claims about later builds.
