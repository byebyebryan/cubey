# Optional shallow-water treatments

Reviewed 2026-10-09 against `61debe18`. Retain these as inexpensive artist
controls, not as a fix for the prominent downhill ribbons.

## Contract

- Existing `film_end_m` and `film_ground_mix` extend the wet-ground appearance.
  This also changes the existing film roughness and fine-detail depth gates;
  it is not an isolated color adjustment.
- `water_shallow_coverage_strength` defaults to zero in every preset. A depth
  smoothstep, reaching full coverage at `water_shallow_coverage_end_m`, exposes
  existing wet terrain. It does not concentrate flow or remove simulated mass.
- Shaded water and the coverage view apply the artist fade. Raw depth,
  film-weight and lighting-component diagnostics ignore it.
- GUI and bounded material JSON controls use the existing water pass and
  reserved uniform slots. No new pass, texture, geometry or hydraulic work.

## Review and evidence

Frozen comparison: `outputs/fluid/shallow-ribbon-v1-20261009-Ce1Vjr/index.html`.
Six variants across streams, lake and overview retain byte-identical native
inputs. The three default images exactly match the pre-change build. All 21
retained captures, including the pre-change controls, were hash-checked during
review; current executable, shaders and renderer source pins match the captures.

The report helper changed after capture to present verdicts and test receipts.
Its capture-time hash remains in the manifests; it is distinct from the reviewed
report helper. Renderer source and input identity are not waived.

Both treatments modestly soften shallow margins, including stronger 60 cm
probes. The long, continuous hillside ribbons remain. Deeper lake interiors are
preserved in the reviewed captures. Defaults are unchanged; no realism, owner
visual acceptance or measured performance claim is made.

Validation: target build, changed-C++ formatting, Python compilation and
`git diff --check`; focused material/Fluid, Scenic helper, mountain launcher,
surface-gradient GPU and shallow-coverage renderer controls. Fresh review-run
receipt: `outputs/fluid/shallow-ribbon-v1-20261009-Ce1Vjr/review-commit-tests.xml`.
Analytic GPU cases cover zero/partial/full fade strength, monotonicity and deep
identity. Synthetic renderer cases cover dry/deep identity, shallow changes,
diagnostic isolation and absence of time-dependent coverage flicker.

The user's marked regions in `outputs/Screenshot_2026-10-09_12-31-27.png` are
the acceptance targets for the next investigation. Do not substitute general
shallow-margin improvement for improvement in those steep-flow regions.
