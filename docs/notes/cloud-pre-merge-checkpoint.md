# Cloud Pre-Merge Checkpoint

This checkpoint records the current production `projects/cloud` state before
syncing cloud work into the terrain and sky/atmo worktrees.

## Capture Pack

Generated review output:

```sh
cmake --build --preset dev --target cubey_project_cloud cubey_project_cloud_tests
projects/cloud/capture_review.sh outputs/cloud-pre-merge-current
```

`outputs/` is ignored by git. The review pack includes final surface, high,
high-oblique, orbit, orbit-terminator, orbit shell, transition, local/far/orbit
alpha, density, weather, and crop contact-sheet diagnostics. The most useful
entrypoints are:

- `outputs/cloud-pre-merge-current/contact-sheet.png`
- `outputs/cloud-pre-merge-current/diagnostic-crops/center-feature-contact.png`
- `outputs/cloud-pre-merge-current/surface-up.png`
- `outputs/cloud-pre-merge-current/high-oblique.png`
- `outputs/cloud-pre-merge-current/orbit-satellite-preview.png`

## Current Read

- Surface/local cloud shape is the visual baseline for foreground volumetric
  clouds.
- High/high-oblique views use the local volume plus a high-view far bridge and
  a surface horizon assist to maintain continuity toward the horizon.
- Orbit views use the `surface-shell` cloud-top representation by default. The
  old volume raymarch remains a comparison path, not the orbit art target.
- Orbit coverage/detail is believable enough for checkpointing, but still needs
  art/model tuning before it should drive planet-scale feature work.
- The pre-merge prep should clarify controls and contracts, not retune the
  cloud model unless a capture shows an obvious regression.

## Known Follow-Ups

- Shared renderer promotion is still deferred; consumers should not copy cloud
  raymarch code.
- Ocean/planet/terrain integration should consume cloud products, metadata, and
  future shadow outputs through a shared contract.
- Quick consumer checks should use the quarter-resolution smoke recipe in
  `projects/cloud/README.md`; full-quality captures are for standalone visual
  review.
- The cached octahedral/hemisphere path from `cloud_ref_2` remains a later
  performance architecture candidate.
- Motion/shimmer checks should be repeated before orbit clouds become a
  foundation dependency.
