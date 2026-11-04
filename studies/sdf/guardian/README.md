# Guardian — retired CSG sculpture study

Guardian was retired from the active demo collection on 2026-11-04 after visual
review. Its character design did not justify keeping it as a featured sculpture.
The [robot and material workshop](../../../projects/sdf/README.md) are the current
CSG front doors. This study is maintenance-only, not an active modeling backlog.

The preserved content remains useful for large-source tracing, materials and
edit-service comparisons. The final mirrored form has 1,011 primitive leaves,
2,021 instructions and 65 grouped actions; it is close to the 1,024-leaf cap.
Its performance misses and earlier artistic decisions remain historical results.

## Run explicitly

Use the normal SDF executable; Guardian adds no application, build option,
runtime dependency or default scene:

```sh
rtk proxy build/release/projects/sdf/sdf --config studies/sdf/guardian/presets/sculpture-guardian-limestone.json
rtk proxy build/release/projects/sdf/sdf --config studies/sdf/guardian/presets/sculpture-guardian-mirrored-build.json
```

Other retained [presets](presets/) include marble, earlier blockouts, material
and environment studies. The [scene files](scenes/) preserve the original
geometry/history bytes. Presets change only their authored-scene binding.

## Tools and reproduction

Dedicated generators and inspection/report tools live in [tools/](tools/).
They remain explicitly invoked stdlib scripts, using the shared SDF authoring
helpers. For example:

```sh
rtk proxy python3 -B studies/sdf/guardian/tools/profile_guardian.py --help
rtk proxy python3 -B studies/sdf/guardian/tools/capture_guardian.py --help
```

Generators overwrite their retained source/presets when invoked; do not run
them merely to open the study. Shared Robot/Guardian query controls now use
`projects/sdf/tools/query_study.py` and `qualify_query_study.py`.
Other explicit SDF study tools resolve relocated content through
`projects/sdf/tools/sdf_paths.py`. The [relocation map](../../relocations.json)
records canonical old-to-current files, without a forest of forwarding files.

## Historical evidence

- [Original sculpture closeout](../../../docs/notes/sdf-guardian-closeout.md):
  dated acceptance and material decisions, superseded for current presentation.
- [Tracing comparison](../../../docs/notes/sdf-tracing-comparison-results.md),
  [frame profile](../../../docs/notes/sdf-guardian-frame-profile-results.md) and
  [source-query results](../../../docs/notes/sdf-source-query-optimization-progress.md).
- [Local edits](../../../docs/notes/sdf-local-edit-results.md),
  [rounded segments](../../../docs/notes/sdf-interactive-edit-results.md) and
  [bounded service](../../../docs/notes/sdf-edit-service-results.md).
- [CSG wrap-up](../../../docs/notes/sdf-csg-closeout.md): current scope,
  relocation checks and acceptance limits.

Captured commands, reports, receipts, media, qualified binaries and ignored
outputs retain their capture-time paths and identities. They describe their
original revision; this move does not regenerate or relabel them as fresh data.
Only live reproduction bindings and documentation links move forward.
