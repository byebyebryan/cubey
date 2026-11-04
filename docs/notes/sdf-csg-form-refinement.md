# Four CSG scenes: form refinement

Date: 2026-11-04. Status: complete locally; recommended art candidates.

This note records checkpoint `2f87ec88`. The later
[junction polish](sdf-csg-junction-polish.md) updates the current candidate JSONs;
the original comparison remains reproducible from this checkpoint and its
immutable report bundle.

The [matched visual report](http://starship:8787/reports/53eb2e9ba92bca61323786057017a112611a79c05432ecdd71c4c53bef9a5aee/)
compares the robot, canal mill, square observatory and terraced courtyard.
The criterion is overall form, spatial clarity and purposeful detail. Repetition
stays where it explains structure or use; decorative grids and dense ornament
are reduced. These are new named candidates; original source scenes, presets
and earlier evidence retain their identities. Artistic selection remains with
the user. Public site media and native application defaults are unchanged.

## Result

| Candidate | Primitive leaves, previous to refined | Saved actions | Main change |
| --- | --- | --- | --- |
| Field-kit robot | 82 to 75 | 75 | Narrower head; fewer fasteners and vents; retain lens, display, wave and joints |
| Canal mill | 243 to 169 | 13 | Broad red roof and cream canopy; eight wheel members; larger bank slabs |
| Square observatory | 536 to 278 | 25 | Clear dome/slit and instrument; simpler masonry, rails and foundation |
| Terraced courtyard | 283 to 180 | 10 | Open paving, plain roofs, one banner and clearer stair approach |
| Observatory section | 537 to 279 | 26 | Same surviving primitives as intact scene, plus one cutter; quieter floor/books |

The robot needs the lightest intervention. The observatory gains the most:
its dome, curved slit guides and telescope now read before the surrounding
ornament. The mill retains enough wheel, framing and delivery detail to explain
its function. The courtyard's arch rhythm and fountain benefit from a calmer
ground plane. Functional stairs, arch bays and wheel paddles remain.

This is a content pass using the existing explicit CSG system. There is no
triangle conversion, shader change or new editing feature. Smaller source
counts do not establish a frame-time improvement. The canal is static geometry.

## Run and reproduce

The five [saved scene JSONs](../../projects/sdf/scenes/csg-showcase-2026-11/)
and [presentation presets](../../projects/sdf/presets/csg-showcase-2026-11/)
carry the selected camera and original subject lighting. These presets explicitly
use Balanced with SS2; the native application default remains Cached.

```sh
build/release/projects/sdf/sdf --config projects/sdf/presets/csg-showcase-2026-11/observatory.json
```

Use `robot.json`, `canal-mill.json`, `courtyard.json` or
`observatory-section.json` in the same preset folder for the other candidates.
The original field-kit robot and its existing build preset remain available.

The [generator](../../projects/sdf/tools/refine_csg_showcase.py) reads the saved
originals, records each removed/changed leaf, prunes obsolete history references
and preserves inherited group transforms. It checks unique names, complete
history coverage and uniform action polarity. It also verifies that the section
adds one cutter and otherwise shares the intact observatory primitives.

```sh
python3 -B projects/sdf/tools/refine_csg_showcase.py --capture-dir outputs/sdf/my-fresh-csg-refinement/captures
python3 -B projects/sdf/tools/qualify_csg_refinement.py --output outputs/sdf/my-fresh-csg-refinement/qualification
python3 -B projects/sdf/tools/csg_form_refinement_report.py --captures outputs/sdf/my-fresh-csg-refinement/captures --qualification outputs/sdf/my-fresh-csg-refinement/qualification --output outputs/sdf/my-fresh-csg-refinement/report
```

Use fresh capture, qualification and report directories. Repeating generation
produces identical scene bytes and preset content apart from the output binding.
The report includes both source generations, exact native PNGs, capture inputs,
commands, logs, qualification and reproduction scripts.

## Verification and limits

- Eighteen native 1440 by 1080 comparison frames pass Vulkan validation. Each
  before/after pair uses identical camera, lighting and Balanced/SS2 settings.
  Both angles of all four candidates and the observatory section were inspected.
- All 169 native history/seek checks pass across the five saved scenes, with
  CPU/GPU field and authored-evaluation oracles at every prefix, backward seek,
  empty reset and final restore. The oracles use their existing advanced sampled
  controls; named rendering modes reject oracle combinations by contract.
- The independent robot audit passes all 66 serialized joint checks. Each
  observatory support audit passes 63 checks covering composed support contacts,
  full conservative stair/landing footprints, tread seating and transformed
  tripod feet on the observing pad, clear of the stairs. It is a bounded audit,
  not a general collision solver.
- Five candidate scenes/presets regenerate deterministically. Five original
  scenes, 780 protected historical evidence/media/study files, ten unrelated
  horse files, the native executable and all 220 compiled shaders retain their
  baseline fingerprints.
- Sixteen scoped SDF, documentation and layout CTest checks pass. The SDF-only
  documented recipe gate and report delivery are recorded with final receipts.
- Fifty report-control and asset assertions pass against its JavaScript with a
  minimal DOM model. Browser navigation fails in the connected adapter with
  `Cannot read properties of undefined (reading 'timeoutMs')`; browser layout,
  CSS and physical interaction acceptance remain unverified. HTTP byte checks
  and JavaScript checks are separate evidence.

The first oracle invocation was rejected for combining a named rendering mode
with validation flags. The qualifier now removes that presentation selector and
uses the documented advanced sampled route; no runtime change was needed.

The [execution plan](sdf-csg-form-refinement-plan.md) and
[evidence packet](../evidence/sdf-csg-form-refinement-2026-11/README.md) bound the
result. Raw iterations and qualification remain under
`outputs/sdf/csg-form-refinement-20261104/`; the retention manifest protects the
final native evidence and delivered report. No deletion or deduplication ran.
Guardian remains retired. The horse conversion study and its pre-existing dirty
files are a separate work unit. No push or public site deployment was performed.
