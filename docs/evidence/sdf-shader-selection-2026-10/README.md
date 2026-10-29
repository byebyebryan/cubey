# Source shader selection evidence

The [decision record](../../notes/sdf-shader-selection-results.md) describes the
qualified local source selection and its remaining performance/startup limits.

- [Summary](summary.json), [timing runs](timing.json), [native qualification](qualification.json)
  and [appearance qualification](appearance.json).
- [Completed-frame comparisons](action-pixels.json), full [action receipts](actions/),
  [selected image provenance](images.json), [native window receipt](window.json),
  [negative capture contracts](negative.json).
- [Source/shader context](context.json), [protected baseline](baseline.json),
  [rejected setup/capture attempts](rejected.json). Accepted images live in `images/`.

Regeneration from the repository root, after a Release build:

```sh
rtk proxy python3 projects/sdf/tools/qualify_shader_selection.py qualify --output outputs/sdf/shader-selection-review/qualification-final
rtk proxy python3 projects/sdf/tools/qualify_shader_selection.py appearance --output outputs/sdf/shader-selection-review/appearance-v4
rtk proxy python3 projects/sdf/tools/qualify_shader_selection.py timing --qualification outputs/sdf/shader-selection-review/qualification-final --output outputs/sdf/shader-selection-review/timing
```

Keep native GPU jobs serial. `--resume` reuses verified static PNGs only for an
identical binary, and reruns all action/oracle jobs; use a fresh directory when
changing scene/shader inputs. Raw commands, profiles and remaining captures
are retained at `outputs/sdf/shader-selection-20261029/`. The assembler also
requires the retained baseline, negative contracts and native window receipt;
it is an archive recipe for this complete loop, not a standalone test launcher.

[Interactive report and delivery receipt](report-delivery.json) retain the hosted
URL, per-file HTTP hashes and desktop/mobile interaction acceptance. The focused
[check receipt](checks.json) separates SDF tests, documentation/build checks,
native acceptance and protection of the baseline.
