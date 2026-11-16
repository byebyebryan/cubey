# Dragon glass Q1 whole interface fallback rejected

**2026-11-16 follow-up:** the analytical findings below remain valid. After the
[actual dragon appearance comparison](raytracing-dragon-glass-normal-looks.md),
the user accepted the smooth policy as a demo appearance choice. The
[smooth-default record](raytracing-dragon-glass-smooth-default.md) describes that
later integration; the no-integration statements below describe this earlier run.

2026-11-14 local run date; raw receipts use UTC. Executes the first control
stage of the [Q1 plan](raytracing-dragon-glass-q1-plan.md).

**Reject the first guarded-smooth candidate before dragon/native integration.**
It keeps the tested positive branches on the correct geometric sides, but the
fallback switch creates a finite optical discontinuity, and the resulting
radiance operator is nonconservative. The corrected Q0.1 geometric renderer
remains the default.
This is a completed negative experiment, not a qualified smooth-glass feature.

A later user-requested [visual comparison](raytracing-dragon-glass-normal-looks.md)
renders the actual dragon in private builds. It supplies the appearance evidence
missing from this first-stage result and keeps demo priority separate from the
analytical qualification decision below.

[Interactive GPU controls and energy comparison](http://starship:8787/reports/e95fac2a4ff410df3ca08d1a6cc01345196834015d92360d7b5f0034e8e4a4cd/)
provide the before/after switch, analytic illustration and independent integrals.

## Candidate and faithful execution

`shaders/optical_normal_experiment.glsl` implements one sharp-interface policy.
It validates the raw interpolated normal, incidence and both positive Fresnel
branches before selection. Failure selects the complete geometric interface;
it never flips or discards an already selected direction. Entry/exit and eta
are supplied by the geometric side. IOR 1 stays straight. The named guard is
`2^-16`, frozen before the GPU/image comparisons. There is no iterative solver,
sample clamp, throughput cutoff, new medium convention or offset change.

The helper is not included by ordinary shaders or selected by normal config.
Two complete private source/executable/shader sets use a labeled substitute for
the existing dielectric-contract entry point. The first evaluates 65,536
interface controls; the second integrates both outgoing hemispheres with
4,194,304 cosine/azimuth cells. Both require Vulkan synchronization validation.
They evaluate interface arithmetic in the ray-generation stage, without tracing
the dragon or measuring a scene-rendering workload.

The independent CPU oracle uses float64 Snell/Fresnel equations. Arithmetic
checks consume the recorded float operands rather than renormalizing a GPU
normal in double precision. The maximum regular reflection/transmission vector
errors are `1.46e-7` / `3.22e-6`; Fresnel absolute error is `9.21e-6`, within the
existing dielectric qualifier's `1e-5` limits. All selected outputs are finite,
and no positive branch points to the wrong geometric side. The 12,288 IOR-1
observations retain bit-exact incoming/transmitted direction words.

Equal-normal controls pass regular float tolerances, with maximum transmitted
component difference `6.18e-7` between the compiled smooth and geometric helper
branches. They are not universally bit-identical. Critical-angle controls are
separate: six policy classifications differ between GPU float32 and the
float64 normal-field oracle, and equal-normal transmitted components can differ
by `1.72e-4` in that band. They remain in the receipt and are not used to claim
a robust critical-angle policy. All 65,536 observed geometric-side tests pass.

## First failure is a discontinuity in the chosen model

For incident-side geometric normal `(0,0,1)`, smooth normal tilted `-20` degrees
in the incidence plane and eta `1.5`, two adjacent GPU inputs have:

| Quantity | Before fallback | After fallback |
| --- | ---: | ---: |
| Reported incidence angle | 49.9991149902 degrees | 49.9991531372 degrees |
| Interface selection | Smooth | Geometric, reflection-side guard |
| Fresnel reflectance | .17103131 | .05766136 |

Their actual vector angle differs by `3.15e-5` degrees. Reflection jumps
`39.99996` degrees and transmission `11.92049` degrees. A smaller `-2` degree
normal tilt also yields a `3.99997` degree reflection jump and `1.90647` degree
transmission jump near 86 degrees incidence. These are finite changes at a
smooth field's fallback threshold, not Monte Carlo noise or a triangle-edge
change. Moving the numerical guard shifts the threshold but cannot remove the
different limiting interface directions.

The reselected policy also fails reverse-path consistency. Across the regular
GPU controls, reverse reflection differs by up to 120 degrees and reverse
transmission by 42.54 degrees. Keeping the selected interface fixed instead
gives maximum reverse errors `.0000234` / `.001797` degrees. The large discrepancy
comes from choosing another interface on reversal, not from a missing eta
factor or ordinary float error. These are local-interface tests, not measured
full-dragon reciprocity errors.

## Independent energy failure

The energy control uses a lossless flat interface, IOR 1.5, a smooth normal
tilted 20 degrees, and an exterior unit-radiance cap centered at 60 degrees with
15 degree radius. Its incident irradiance is known analytically. We integrate
the defined camera-radiance operator over **both** outgoing geometric
hemispheres, including the existing radiance-mode eta-squared scaling.

| Integral | Geometric flux / incident flux | Candidate flux / incident flux |
| --- | ---: | ---: |
| GPU outgoing hemisphere, 1024 x 4096 cells | 1.0000761 | 1.3180877 |
| CPU source cap, all analytic preimages, order 256 | 1.0000000 | 1.3182232 |

The methods differ by `.0001355` for the candidate. CPU preimage orders
64/128/256 give `1.3182431`, `1.3182304`, `1.3182232`. A separate outgoing-space
CPU integration independently approaches the same result. The candidate's
roughly 31.8 percent excess is much larger than integration uncertainty.

The source-cap method enumerates both possible constant-normal pieces and
checks which policy is selected at each reflected/transmitted preimage.
Reflection's solid-angle Jacobian is one; the Snell Jacobian is
`abs(w.N)/(ior^2 * abs(v.N))`. The radiance eta-squared term cancels that factor
on transmission. Projected **geometric** area remains in the flux integral.
Normal substitution changes the projected-area transport; fallback can also
create multiple preimages or gaps. They are summed without renormalization.
The geometric control yields one pointwise under this change
of variables, independently of quadrature order.

A more grazing CPU cap, center 80 degrees/radius 5 degrees, gives `2.6078798`
for 20 degree tilt at all three preimage orders. Even the separate 2 degree
tilt case yields `1.2382947` for that cap. These are controlled single-interface
counterexamples, not solar noise statistics or dragon image brightness ratios.

The reduced-radiance local furnace algebra passes to `1.19e-7`. That check only
verifies complementary Fresnel weights and eta scaling; it cannot certify the
directional mapping's energy behavior. No complete closed-body furnace pass is
claimed. This is why the flux test is distinct from checking `F + (1-F) = 1`.

## Smoothing has a measurable benefit but does not clear the gate

CPU centroid-ray tests compare tessellated unit spheres against an analytic
sphere, from camera `(0,0,3)`. The table reports median first-interface direction
error in degrees, excluding a defined silhouette region:

| Sphere triangles | Geometric reflection | Guarded reflection | Geometric transmission | Guarded transmission |
| ---: | ---: | ---: | ---: | ---: |
| 224 | 10.1331 | 6.4625 | 2.9158 | 1.9647 |
| 960 | 4.3085 | 1.3094 | 1.1390 | .3794 |
| 3968 | 2.1753 | .3297 | .6228 | .0937 |

Normal interpolation improves these controlled directions and converges with
refinement. Polygon and analytic hit positions differ, and these are entering
interfaces only; the test does not qualify silhouettes, exits or the dragon.
The benefit does not excuse the discontinuity and flux failures.

## Decision and retained boundary

The policy is rejected at the planned first stage. No native selector,
saved-view setting, scene uniform or history identity is added. No smooth dragon
pilot, full-HD candidate matrix, native orbit or candidate performance claim is
needed to promote this failed interface. Those planned later stages are
explicitly unexecuted; old Q0.1 captures are not relabeled as candidate results.

The full Q0.1 source/build snapshot was preserved before work. Its ordinary
runtime files, executable and compiled shaders remain byte-identical. Models,
certificates, materials, presets and the separate primary SDF worktree remain
preserved. The new code consists of opt-in diagnostic helpers/tools and retained
evidence. No Q2/Q3, commit, push or public-site update is included.

The next research question is how to define a continuous interface adjustment
and its transport weighting together. Reuse these side, seam, reverse-path and
flux witnesses before trying another dragon policy. Do not treat a
reflection-only normal correction as a qualified dielectric solution. Reopening
the accepted mesh is not required by this experiment.

[Evidence and reproduction](../evidence/raytracing-dragon-glass-q1-2026-11/README.md)
separate analytical acceptance, candidate rejection, ordinary-renderer
regressions and report delivery.
