# Smooth normals for the glass dragon demo

2026-11-16. The user selected the smooth appearance after reviewing the
[matched geometric/smooth images](raytracing-dragon-glass-normal-looks.md).
The head grooves and broad body refraction read more clearly. The demo now
defaults to that policy; this does not revise Q1's analytical measurements.

`--no-smooth-normals` restores the geometric interface. The native **Smooth glass
normals** checkbox selects the same setting, and saved views retain it.
The setting is carried in the existing environment uniform and accumulation
identity, so changing it discards the previous normal mode's samples.

Only sharp glass in `dragon-optics` uses the new selection. The guard selects
one normal for both Fresnel branches before the existing dielectric sampler.
Raw mesh normals, interpolation and inverse-transpose transformation match the
reviewed private comparison. Invalid normals or a branch on the wrong geometric
side select the complete geometric interface. Geometric normals still own
entry/exit, medium tracking and origin offsets. IOR 1 stays straight. Materials,
mesh, shared atmosphere, lighting, sample dimensions and transport budgets are
unchanged.

Optics dumps retain geometric and selected interface normals separately, and
first-interface direction diagnostics use the selected normal. The original
Q0.1 spawn qualifier explicitly selects geometric normals for reproducibility.
The historical private comparison still reproduces its frozen source snapshots;
new A/B captures can use the ordinary executable and the normal switch.

Q1's fallback discontinuities and nonconservative flux counterexamples remain
known limitations. The cone still uses the flat-interface approximation and
does not bound varying-normal filtering. The reviewed smooth stills also had
one event-budget pixel in three views; this choice is appearance acceptance,
not a claim that every full-HD transport diagnostic passed. Sampling speckles
remain a separate investigation.

Validation is recorded in
[the integration evidence](../evidence/raytracing-dragon-glass-smooth-default-2026-11/README.md).
