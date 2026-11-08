# Dragon glass review fixes, 2026-11-08

See [the fix notes](../../notes/raytracing-dragon-glass-review-fixes.md).
This evidence follows the historical first-pass receipts; it does not rewrite
the earlier memory values or capture identities.

- `qualification.json`: revised 256-SPP acceptance, including the actual preset
  before any budget overrides, explicit shallow diagnostic policies, clean
  accepted views and allocation-component checks. Default/reference subject HDR
  RMSE is `4.09e-10`; the default budget is 128 / 1024 / 4096.
- `rejected-one-event.json`, `rejected-one-event.log`: the same copied one-event
  preset that previously passed now fails on its first capture, with no pass
  receipt. The production preset was never changed. Rejected counts: 3,657
  event-limit pixels and 226 shadow-limit pixels at 128 SPP.
- `memory-comparison.json`: matched 160 × 120 / one-sample before/after captures.
  Both resident and peak requested totals rise by exactly the existing
  13,928,928-byte adjacency allocation. PNG files are byte-identical.
- `source-sha256.json`: revised executable and relevant source identity.
- `raytracing-ctest.log`, `validation.json`: all 19 scoped CTests passed,
  including seven policy regression cases; full qualification and fix summary.
- `preservation.json`: unchanged certified model, shader sources, 11 protected
  original SDF files and 32 existing recipe/preset files.
- `review-fixes.patch`: focused renderer/qualification delta from the reviewed
  candidate. The added policy/test modules and CTest registration are in project
  source.

The standard-library-only policy tests are registered with CTest and need no GPU,
downloaded model, NumPy or CGAL. Full captures, the pre-fix executable, private
copied-preset workspace and logs remain under
`outputs/raytracing/dragon-glass-review-fixes-20261108/` and the preceding
`outputs/raytracing/dragon-glass-review-20261108/` reproduction directory.

No commit, push or public-site update was performed.
