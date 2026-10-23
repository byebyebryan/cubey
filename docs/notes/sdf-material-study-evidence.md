# SDF sphere and guardian material study

Status: implemented and natively qualified, 2026-10-23. The
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

## Integration and final evidence

`d900ba52` commits the bounded glass path and its explicit guardian limit.
The integration history checks then exposed an empty-state optics mismatch:
the GPU used the first material's optics before any surface existed, while the
CPU returned neutral defaults. `0aa1363c` makes the empty sample neutral and
adds the glass empty/backward/reset history to the final native foundation.
This is a data/validation correction, not another optical or visual iteration.

The final foundation has 31 render/validation runs, with CPU/GPU optics,
Snell/Fresnel/TIR and absorption comparisons at 0.00002, the existing geometry/
pattern bounds, mixed ownership, explicit cuts, one-build material caches,
six white-furnace controls and exact glass export/reload images. Five focused
CPU suites pass. A separate two-edit material-cut history checks all states
and backward/reset revisits without replaying the unchanged guardian history.

The final comparison contains 28 native stills, 12 glass diagnostic views and
seven eight-second / 240-frame films. Six films show opaque guardian materials;
the glass film shows the qualified sphere. Every included frame decodes and
passes the exhaustion-color screen. Guardian glass is clearly experimental in
the still comparison and cost table; there is no substitute opaque rendering.
Native clay/ceramic/bronze sphere and guardian views and glass diagnostics were
visually inspected. First-pass opaque values were retained.

The four original atmosphere PNGs (plain, limestone, accepted subtle marble and
neutral inspection) remain byte-identical in the final build. The new study
copies use a different, fixed environment; unchanged material parameters do not
imply the same lighting as those earlier atmosphere images.

Fresh profiles cover all seven materials on each subject at 1920×1080, 1×,
hard shadows, 180 submitted frames / 30 warmup and 148 completed GPU samples.
They separate shading/post, cold field build and measured CPU IBL setup including
uploads. The first timing batch overlapped owned legacy render jobs and was
discarded in full under `material-study-profile-overlap-rejected/`. The final
batch runs after capture work, without competing owned GPU jobs. Native
receipts retain exact per-case median/p95/build/setup values and artifact hashes;
these are bounded content measurements, not a material-performance ranking.

Artifacts under `outputs/sdf/` include `material-study-baseline/`, retained
opaque/glass auditions and intermediate receipts,
`material-study-final-foundation/`, `material-study-comparison/`,
`material-study-profile/`, `material-study-final-legacy/` and the final
`material-study-report/`. Pre-empty-state-fix results retain their original
identity and are superseded for final evidence. The tracked authoring,
qualification, capture and report tools reproduce the catalog and package the
source/config/scene receipts. Publication and available control checks are
recorded separately from native qualification.

## Controls and measurements

The final private-Xvfb native material check passes resize, orbit/zoom,
roughness editing and unchanged field caching. The glass check verifies the
bounded material controls are disabled and model pixels remain unchanged after
an attempted edit. Both retain one geometry build and match the final native
binary/shader identities. The driver uses an inspected 20-pixel study-label
row offset and one scroll notch to expose the material controls. Early private
screen-size/clipped-control attempts remain separate failed harness evidence.

Representative final GPU medians (ms), sphere / guardian, are: clay
2.173 / 1.422; ceramic 2.180 / 1.432; bronze 2.175 / 1.426; glass
2.524 / 2.374, with the guardian glass row explicitly experimental. The sphere
occupies more screen area, so these are case costs rather than a normalized
complexity comparison. Sphere-glass cold field / IBL setup are 5.144 / 248.847
ms; guardian-glass values are 919.466 / 273.943 ms. All 28 catalog scenes and
presets reproduce byte-for-byte.

The final native binary SHA-256 is
`780ca4d66a4f4ad7f0f8409ca73161fbaf9d5542297f9055140a7a8c27b980cb`.
Capture, foundation, profile, legacy regression and native controls agree on
that binary; their 14 compiled-shader hashes agree where recorded. The source
implementation ends at `0aa1363c`; later changes are tools and documentation.
Browser setup found no connected browser, so only source/control-script checks
and HTTP delivery can be claimed for the HTML report, not browser interaction.

## Publication and closeout

The [sphere and guardian material report](http://starship:8787/reports/f9ff89a12a4569c7f1c934cd5c9996570b316afc9ffcbed89d9931ed016983ca/index.html)
was published under Cubey on 2026-10-23. All 409 files (37,127,893 bytes)
match their served HTTP bytes, including all image, video and source assets.
The explicit entrypoint returns HTTP 200 from Carbon as well as Starship; the
two linked parent reports also return HTTP 200. Publication leaves the shared
report server running. The immutable report contains the implementation and
evidence snapshot from before this publication receipt was appended.

Static HTML paths and asset hashes pass. JavaScript syntax and a DOM event
harness cover all 28 material/view image cases, seven swatch buttons, twelve
diagnostic choices, video source selection, stale metadata callbacks and the
video-error link fallback. This checks script behavior, not browser layout or
media playback. Browser acceptance remains unverified.

The final focused CTest selection passes all 16 checks, including five SDF CPU
suites, and all 20 scoped documentation CLI recipes pass. Native evidence is
unchanged by this documentation/report-tool closeout. Milestones remain local;
this loop does not push or reopen guardian modeling, marble appearance, glass
transport depth or the declared admission threshold.
