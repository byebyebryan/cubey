# Selected SDF optimization investigation

The [findings and trial gates](../../notes/sdf-optimization-opportunities.md)
separate measured controls, hypotheses and production acceptance.

- [Summary](summary.json), [90 ablation runs](ablations.json), [resolution](resolution.json).
- [Controlled shadow runs](shadow-controlled.json), [preliminary runs](shadow-pilot.json),
  [capture qualification](shadow-qualification.json), [compiler statistics](compiler.json).
- [Historical workload](workload.json), [sources](sources.json), [proposed trials](opportunities.json).
- [Baseline](baseline.json), [phase context](context.json), [image provenance](images.json),
  [pinned author-code identity](author-source.json).

Use investigate_selected_shader.py with phases qualify, timing, cull-qualify,
cull, scale and compiler. Timed phases require a matching completed qualification
directory through --qualification; all phases require a fresh --output directory.
Keep GPU jobs serial. Preliminary shadow runs used the earlier two-case runner;
final controlled runs add the order-only comparison. Raw logs/profiles remain
at outputs/sdf/optimization-investigation-20261029/. The assembler is an archive
recipe for this complete pass, not a test launcher. Probes remain static private
diagnostics, without production/editor or broad optical qualification.

[Report delivery and browser acceptance](report-delivery.json) includes the hosted
URL and per-file HTTP hashes. [Focused checks](checks.json) retain the 12-group
result separately from the remaining production adoption gates.
