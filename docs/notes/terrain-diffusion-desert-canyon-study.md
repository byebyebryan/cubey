# Terrain Diffusion Desert And Canyon Study

Date: 2026-07-25

Status: completed. Desert source variation confirmed; canonical canyon source
not confirmed.

## Question

The first landscape-variation study established that Terrain Diffusion can
produce a strong alpine range and a distinct low-relief source. Its temperate
valley and dry upland were not sufficiently different from the accepted
mountain backdrop.

This study asks two narrower questions:

1. Do the existing pinned Terrain Diffusion worlds contain desert landscapes
   that read as more than a dry material treatment?
2. Do they contain coherent, deeply incised canyon landforms that the coarse
   relief selector failed to find?

The study uses the existing model, seeds, synthetic conditioning defaults, and
natural world coordinates. It does not draw channels, alter heightfields,
apply erosion, or introduce custom conditioning maps.

## Why Another Selection Pass

The existing catalog scans 2,642 non-overlapping 61.44 km windows across seeds
`0`, `9012`, and `12345`. Each window is represented by only 8 x 8 coarse
samples at 7.68 km spacing. That is sufficient for climate, land fraction, and
broad relief, but a canyon or escarpment can disappear inside one coarse cell.

Selection therefore has two stages:

1. Coarse climate and relief filters produce a bounded shortlist.
2. A centered 768 x 768 native-resolution probe is generated for every
   shortlisted window, area-averaged to a 192 x 192 analysis field at 120 m
   spacing, and measured before final candidates are selected.

The probe covers the central 23.04 km of each candidate. Full accepted assets
retain the existing 2048 x 2048, 30 m, 61.44 km field contract.

## Desert Candidates

The current scan contains exactly three windows satisfying:

- at least 95% land;
- median temperature at least 20 C;
- median annual precipitation at most 200 mm.

All three are probed and baked. They intentionally span the available relief
range rather than attempting to define one universal desert shape:

- a high-relief arid candidate;
- an intermediate-relief arid candidate;
- a low-relief arid candidate.

This is a rocky-desert and desert-plain study. Dune fields are explicitly out
of scope because aeolian bedforms are a separate detail process and are not
reliably represented by broad climate statistics.

## Canyon Candidates

The canyon shortlist requires:

- at least 95% land;
- coarse relief of at least 1200 m;
- median temperature of at least 8 C;
- median annual precipitation of at most 800 mm.

The twelve strongest coarse candidates are probed. Coarse ranking favors
relief and a high median within the elevation distribution, but it does not
decide the final result.

Probe analysis uses the following morphology descriptors:

- smoothed p05-p95 relief and hypsometric median;
- mean and p95 slope;
- high, low-slope plateau fraction;
- multiscale valley depth from grayscale morphological closing;
- deep-valley area fraction;
- largest connected deep-valley component;
- deep-valley component count and concentration.

The canyon score rewards deep, spatially concentrated, connected incision
through substantial flatter upland. It penalizes uniformly dissected mountain
fields. Four spatially diverse candidates are retained for full review. No
flow-direction or D8/D-infinity graph participates in selection.

## Evidence

Ignored source assets are written to:

```text
cache/terrain/sources/v1/desert-canyon-study/
```

The matched headless pack is written to:

```text
outputs/terrain/desert-canyon-study-v1/
```

It includes:

- fixed-scale height, slope, and probe-depth views;
- 500 m surface captures from two headings;
- 500 m clay silhouettes and 200 m surface stress views;
- material-weight diagnostics plus numerical vegetation and moisture metrics;
- source, climate, placement, and terrain timing metrics;
- the previous dry upland and temperate mountain valley as controls.

The study source package contains seven complete height/climate bundles and
fifteen intermediate probes. Generation took 432.40 seconds, including 188.86
seconds for intermediate probes, and produced 133,837,424 bytes of ignored
cache data. The final seed, coarse origin, elevation hash, and climate hash for
every retained source are frozen in the generator contract.

## Results

### Desert

All three strict hot/dry windows are useful source variations:

- `desert-high-relief` contains a broad arid massif and passes the existing
  selected mountain-backdrop placement contract.
- `desert-intermediate-relief` contains rolling, moderately dissected arid
  terrain. It is the strongest general desert-backdrop candidate in the raw
  comparison.
- `desert-low-relief` establishes a genuinely low-relief arid control rather
  than another mountain source.

Their full-field morphology differs from `dry-upland`, so this is more than a
palette-only variation. The current renderer nevertheless undersells the
result: all three retain the shared cool blue-gray surface response and lack a
distinct warm mineral, sediment, or exposed-rock treatment. The low-relief
source also retains sparse cover through the current climate material mapping.
The next desert work belongs in surface interpretation and rendering, not in
another source generator.

### Canyon

The morphology probe found connected incision, but none of the four retained
sources passes as a canonical canyon terrain:

- `canyon-candidate-1` has the clearest connected branching incision in the
  23.04 km probe, but the raw-center scene is locally shallow.
- `canyon-candidate-2` is the best perspective result and reads as a broad
  gorge or mountain valley. Its local surface is wet and partly vegetated, so
  it does not read as an arid canyon.
- `canyon-candidate-3` has the strongest raw-center relief and slope, but reads
  as an escarpment or cliff wall rather than a connected canyon system.
- `canyon-candidate-4` remains a mountain basin and ridge field.

The result is still useful: broad relief plus morphological closing can reject
many ordinary mountain fields and find gorge-like terrain without D8 flow or
authored channels. It is not sufficient to identify the geology, wall
structure, and plateau incision expected from a defined canyon biome.

### Placement And Cost

The review uses `raw-center` for matched morphology comparison and separately
probes compatibility with the existing selected backdrop placement. Seven of
nine sources pass that mountain-oriented selector. The intermediate- and
low-relief deserts correctly fail because they do not provide the required
mountain sectors and arc. That is a placement-policy mismatch, not a malformed
heightfield.

All nine study and control sources have unique geometry hashes. Combined
terrain mean timings range from 0.856 to 0.955 ms and p50 timings range from
0.846 to 0.958 ms at 1600 x 900. Every source remains below the 1.10 ms mean and
p50 gate; observed p95 ranges from 0.922 to 1.003 ms.

## Acceptance

A desert candidate passes as a distinct terrain source only if its morphology
or spatial composition is visibly different from the dry-upland control
before relying on color. A climate-only palette change remains useful material
evidence but does not establish a new terrain type.

A canyon candidate passes only if height, slope, and rendered views show a
dominant or connected incised system with broad walls or plateau contrast. A
uniform field of narrow mountain ravines, isolated pits, or repeated parallel
grooves fails.

Every retained full asset must:

- preserve deterministic seed, origin, payload hashes, and source provenance;
- contain no holes, seams, or source/material registration failures;
- remain within the accepted 1.10 ms combined terrain mean and p50 budget at
  1600 x 900.

P95 is reported rather than gated.

Selected placement is reported as a compatibility result rather than a source
acceptance gate. The current contract intentionally selects mountain-backed
stages and must not reject otherwise valid plains or desert terrain.

## Decision Boundary

The arid sources remain project-local candidates for a focused desert material
pass. `canyon-candidate-2` remains a gorge reference, but the canyon catalog is
not promoted as a biome.

If a dedicated canyon remains a priority, the next source experiment should
vary elevation conditioning strength, elevation frequency, or coarse pooling
one factor at a time and retain this pack as the control. Another selector-only
pass over the same natural worlds is unlikely to produce a qualitatively
different answer.

Custom conditioning is deferred until those natural-source and parameter
studies fail. Any future conditioning must be a broad procedural field, not an
authored canyon centerline.

## Commits

1. `docs(terrain): define desert and canyon study`
2. `feat(terrain): probe diffusion landform candidates`
3. `test(terrain): capture desert and canyon evidence`
