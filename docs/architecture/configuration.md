# Configuration V2

Status: complete.

## Goal

Every executable owns one typed configuration facade composed from the common
host schema and only the project or example options it actually supports.
Configuration remains file-backed and inspectable through JSON, but Cubey does
not depend on a general-purpose object-serialization framework.

The completed checkpoint removed the global `RunConfig`, its option-id and
descriptor registry, the legacy parser/runner, and
`common_run_config_from_legacy`. No executable should accept unrelated project
keys merely because another target happens to use them.

## Current State

The repository now uses one configuration path:

- `cubey::config::Schema` binds metadata to caller-owned typed storage and
  composes independent schema fragments;
- parsing precedence is defaults, `--config`, named CLI flags, then deferred
  `--set path=value` overrides;
- JSON loading, template output, negative bool aliases, ranges, enum choices,
  unknown-key rejection, and assigned-path reporting come from the schema;
- `cubey::host::CommonRunConfig` and `common_run_config_schema()` define only
  the host-owned window, capture, validation, and profiling boundary;
- every active executable owns a typed facade composed from the common host
  schema and its live project schema;
- shared atmosphere, cloud, ocean, PBR, and fluid schema fragments live beside
  their subsystem types, where multiple consumers share the same semantics;
- `cubey::host::run_configured_app()` centralizes the small common
  parse/template/error/launch flow without owning project policy;
- runtime defaults are concrete typed values; optional command-line overrides
  use typed optional state rather than sentinel values.

The clean break migrated 19 executable facades:

- seven examples: `headless_cube`, `spinning_cube`, `textured_cube`,
  `instanced_cubes`, `material_cubes`, `particle_cubes`, and `shadow_cube`;
- twelve projects: `fractal_2d`, `pbr_furnace`, `atmosphere`, `cloud_ref`,
  `ocean`, `gltf_viewer`, `smoke_2d`, `water_2d`, `water_3d`, `fire_3d`,
  `explosion_3d`, and `terrain`.

Planet remains a project-owned facade as well. The retained terrain studies
were not migrated because their useful algorithm, export, generator, and
unit-test targets already had no executable configuration dependency.

## Ownership

### Generic config kernel

`cubey::config` owns only project-independent mechanics:

- option metadata and scalar typed bindings;
- schema composition and duplicate-path/CLI validation;
- JSON application and template emission;
- CLI bootstrap parsing, named flags, bool aliases, and `--set` precedence;
- parse-result evidence such as assigned paths and template requests.

It does not own project defaults, project validation, option taxonomies, or a
base class for every application config.

### Host config

`cubey::host::CommonRunConfig` owns only state consumed by windowed or headless
hosts: title, extent, frame/fps limits, output/capture mode, profiling,
frame-stat output, and validation policy.

Debug views are project-owned. V2 preserves a target's existing
`--debug-view` spelling where it is live, but binds it in that target's facade
instead of the common host schema. Planet's debug-view storage also lives in
its project facade rather than `CommonRunConfig`.

Host normalization remains centralized. A project facade contains one
`CommonRunConfig` member and passes it directly to host configuration; no
conversion from a wider object is allowed after migration.

### Project and subsystem config

Each executable owns a top-level config type, schema composition function,
parser, defaults, and validation. Existing typed runtime configs are bound
directly rather than rebuilt from a global bag of sentinel values.

A shared subsystem may publish a schema fragment beside its typed runtime
config when two or more real consumers use the same paths, defaults, and
semantics. Similar spelling alone is not enough. In particular, grid, camera,
terrain, and debug controls stay project-owned when their meanings differ.

## Compatibility Contract

The overhaul preserves supported behavior, not accidental access to the global
registry:

- retain each executable's live CLI names, JSON paths, defaults, validation,
  and precedence;
- retain `--config`, schema-backed named flags, `--set`, negative bool
  aliases, and `--write-config-template`;
- keep JSON config loading and schema-backed JSON output; arbitrary object
  serialization is not reintroduced;
- reject unknown CLI flags, unknown JSON leaves, duplicate paths, and unrelated
  project keys;
- generate templates containing only common options and options owned by that
  executable;
- remove retired, reference-only, or unconsumed options instead of carrying
  them into V2;
- keep runtime ImGui mutation separate from startup config persistence. UI
  panels may reuse option metadata, but commands, reset behavior, and live
  diagnostics remain project-owned.

Compatibility is proven with behavior tests. The legacy implementation is not
kept as a permanent oracle or fallback once the final cohort lands.

## Implementation Result

The work landed in reviewable cohorts: foundation and Planet boundary cleanup;
seven examples and compact projects; environment/rendering projects; fluid
simulation projects; active terrain; then legacy-system deletion. This order
kept each facade testable while shared schema fragments were promoted only
after multiple real consumers established identical semantics.

The final deletion removed `RunConfig`, `run_config.cpp`, `run_cli_app`,
`parse_run_config`, `ConfigOptionDescriptor`, `RunConfigOptionId`, the
302-option registry, `common_run_config_from_legacy`, legacy sentinels, and
their adapter tests. There is no compatibility fallback or dual config path.

## Review And Acceptance Gates

Each executable facade is covered in proportion to its behavior:

- build with `cmake --preset dev` and pass its focused config/app tests;
- test default, file, named-flag, `--set`, template, and invalid-input behavior
  for each migrated facade;
- preserve headless capture and windowed startup behavior in proportion to the
  affected applications;
- pass `git diff --check` and avoid unrelated runtime or rendering changes.

Repository acceptance requires:

- all 19 executable facades are project-owned and the active Planet facade
  still passes;
- `CommonRunConfig` contains only fields directly consumed by the windowed or
  headless host;
- `RunConfig`, the global registry, and the legacy host conversion are deleted;
- generated templates are target-specific and no target accepts unrelated
  project keys;
- `cmake --preset dev`, `cmake --build --preset dev`, and
  `ctest --preset dev --output-on-failure` pass;
- `cmake --preset dev-terrain-studies`, its full build, and
  `ctest --preset dev-terrain-studies --output-on-failure` pass;
- final legacy-symbol searches and `git diff --check` are clean.

## Non-Goals

- validation framework beyond schema ranges/types and project-owned checks;
- automatic ImGui generation for every project setting;
- runtime hot reload, config watching, or transparent persistence of UI edits;
- arbitrary C++ object serialization or reinstating `lazy-serializable`;
- one universal configuration struct, inheritance tree, or schema for all
  project domains;
- migration of archived source recovered from Git history or the retained
  viewer-free terrain studies.
