# Terrain Diffusion Desert And Canyon Study

Date: 2026-07-25

Status: implementation planned; evidence pending.

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
- material-weight, vegetation, and moisture diagnostics;
- source, climate, placement, and terrain timing metrics;
- the previous dry upland and temperate mountain valley as controls.

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
- pass selected backdrop placement;
- contain no holes, seams, or source/material registration failures;
- remain within the accepted 1.10 ms combined terrain mean and p50 budget at
  1600 x 900.

P95 is reported rather than gated.

## Decision Boundary

If strong examples already exist, they remain project-local source candidates
for later productization. If neither class succeeds, the next experiment may
vary elevation conditioning strength, elevation frequency, or coarse pooling
one factor at a time.

Custom conditioning is deferred until those natural-source and parameter
studies fail. Any future conditioning must be a broad procedural field, not an
authored canyon centerline.

## Planned Commits

1. `docs(terrain): define desert and canyon study`
2. `feat(terrain): probe diffusion landform candidates`
3. `test(terrain): capture desert and canyon evidence`
