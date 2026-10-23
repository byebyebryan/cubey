# SDF environment presentation evidence

Date: 2026-10-23

Status: implementation in progress. See the
[reviewed plan](sdf-environment-presentation-plan.md) for scope and gates.
Commits remain local; no push is authorized for this loop.

## Glass quality checkpoint

The runtime now exposes `glass-crossings` 4/8/16 as a render option, with four
retained as the compatibility default. It changes no material/geometry record
or field build. Config write/reload retains it and invalid values reject.

The 24-angle guardian sweep, including the earlier eight views, reports worst
cap fractions of 1.2433% / 0.1409% / 0.1258% for 4/8/16 crossings. There are no
traversal/progress errors; cap incidence is nonincreasing in each view. Sphere
hero/detail images are identical across budgets. Separated-solid/cavity
fixtures pass the unchanged native validation bounds for every budget.

Fresh sequential 1080p/1x/hard-shadow GPU medians for guardian glass are
2.379 / 2.387 / 2.387 ms for 4/8/16. Sphere values are 2.529 / 2.528 / 2.527 ms.
Each case has 148 completed samples after the established warmup. These are
fixed-environment content costs, excluding field/IBL construction and readback.
Eight meets the retained 1%/8ms gates and is selected for new presentation
presets. Sixteen remains available, with little additional benefit here.

Total internal reflection still terminates using the environment approximation;
raising the crossing budget does not implement extra internal reflection
bounces. The prior four-crossing experimental verdict remains historical.
Artifacts: `outputs/sdf/environment-glass-budget/` and its `profiles/`, with
native commands, configs, hashes, diagnostic images and compatibility receipt.
