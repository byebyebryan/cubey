# Notes

Notes are living scratch context: progress logs, gotchas, rough ideas, and
temporary investigation records that are useful but not polished enough to be
current design guidance.

When a decision stabilizes, promote it into the current docs under `docs/` or
the detailed foundation notes under `docs/architecture/`.

## Entries

- [Working notes](working-notes.md): broad implementation history and lessons
  learned.
- [Atmosphere rendering research](atmosphere-rendering-research.md): early
  notes on clear-sky scattering approaches and the first `projects/atmosphere`
  implementation direction.
- [Celestial rendering research](celestial-rendering-research.md): current
  planet-scale pivot from atmosphere-owned sun/moon disks to planet-owned
  celestial bodies with atmosphere as a consumer.
- [Water 3D profiling notes](water-3d-profiling.md): current solver profiling
  captures and optimization candidates.
- [Ocean performance notes](ocean-performance.md): current spectral ocean FFT
  cost model, observed map-size tradeoffs, and optimization guardrails.
