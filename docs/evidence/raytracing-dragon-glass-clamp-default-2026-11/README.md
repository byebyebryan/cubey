# Dragon glass cap-100 default

On 2026-11-16 the user accepted cap 100 as the demo default after the
[matched raw/clamp/OIDN comparison](../raytracing-dragon-glass-noise-controls-2026-11/README.md).
That report and its validation remain a dated record of the opt-in trial.

`dragon-optics` now supplies 100 only when `firefly-clamp` was not assigned.
The canonical `dragon-glass.json` preset also records 100 explicitly. Other
scenes retain zero. CLI/config zero, old raw saved views, and the native
checkbox still provide raw output. The native checkbox now starts checked.
The HDR clamp algorithm, shaders, material recipes and model are unchanged.
OIDN remains offline.

`validate.py` records final-build RT, optical and documentation checks, renders
the default and raw modes at the previously reviewed full-HD seed/sample count,
and checks exact saved raw-view reload. `validation.json` pins the inputs and
results. Raw render products and logs are retained under
`outputs/raytracing/dragon-glass-clamp-default-20261116/`.

No commit, push or public-site update is included.
