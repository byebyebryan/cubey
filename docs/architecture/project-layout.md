# Project layout and current authority

Application roots are front doors and runtime source, not chronological work logs.

- `README.md`: what runs, prerequisites, build/run and current limitations. Show
  media from that project, not a different backend's result under a new label.
- C++/headers/shaders and committed catalogues: project implementation.
- `tools/`: supported preparation, capture or profiling APIs, with an index.
- `tests/`: frontend/host checks. Existing C++ tests may stay beside their source;
  shared numerical tests stay with shared sim code.
- `docs/` or a small owned setup/reference document: current contract.
- `studies/`: indexed dated experiments, audits, report generators, fixtures and
  their tests. Preserve rejected results and sealed provenance, not root clutter.
  Retained runnable reference ports also live here and must be explicitly opt-in:
  the [cloud reference](../../studies/cloud/README.md) uses
  `CUBEY_BUILD_CLOUD_STUDIES=OFF` by default, not the active project build graph.
  Production cloud work stays in the shared foundation and Atmosphere.
- `cache/`, `outputs/`, private native builds: ignored generated inputs/evidence,
  never implicit checkout prerequisites or cleanup targets in a source move.

Terrain Water startup imports maintained stdlib host/site utilities, not study
modules or optional CUDA/NumPy. Explicit preparation uses released SynxFlow IO,
not a solver port. Its qualified private extension stays at the old absolute
build path/RPATH; moving source does not qualify a moved native binary.

Keep compatibility entries only when they have an identified user: the old Fluid
terrain launcher is one labelled forwarder, not hundreds of forwarding files.
Historical scripts use the indexed relocation map; sealed media paths remain
capture-time provenance and must not be rewritten.
Several historical names may resolve to one final file after successive moves;
relocation targets must be canonical, not chains through another former name.

`tools/check_project_layout.py` and its tests enforce root script/note placement,
current navigation, relocation targets, protected narratives and builtin media
identity. Capture policy still scans relocated helpers. Static checks, numerical
tests, GPU/headless runs and human/native GUI acceptance remain distinct gates.
